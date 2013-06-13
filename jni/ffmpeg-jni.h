/*standard library*/
#include <time.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <assert.h>
/*ffmpeg headers*/
#include <libavutil/avstring.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>

#include <libavformat/avformat.h>

#include <libswscale/swscale.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/opt.h>
#include <libavcodec/avfft.h>

#include <pthread.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

const char g_vertextShader[] = {
    "attribute vec4 aPosition;\n"
    "attribute vec2 aTextureCoord;\n"
    "varying vec2 vTextureCoord;\n"
    "void main() {\n"
    "  gl_Position = aPosition;\n"
    "  vTextureCoord = aTextureCoord;\n"
    "}\n"
};

// The fragment shader.
// Do YUV to RGB565 conversion.
const char g_fragmentShader[] = {
    "precision mediump float;\n"
    "uniform sampler2D Ytex;\n"
    "uniform sampler2D Utex,Vtex;\n"
    "varying vec2 vTextureCoord;\n"
    "void main(void) {\n"
    "  float nx,ny,r,g,b,y,u,v;\n"
    "  mediump vec4 txl,ux,vx;"
    "  nx=vTextureCoord[0];\n"
    "  ny=vTextureCoord[1];\n"
    "  y=texture2D(Ytex,vec2(nx,ny)).r;\n"
    "  u=texture2D(Utex,vec2(nx,ny)).r;\n"
    "  v=texture2D(Vtex,vec2(nx,ny)).r;\n"

    "  y=1.1643*(y-0.0625);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"

    "  r=y+1.5958*v;\n"
    "  g=y-0.39173*u-0.81290*v;\n"
    "  b=y+2.017*u;\n"
    "  gl_FragColor=vec4(r,g,b,1.0);\n"
    "}\n"
};

const int MAX_AUDIOQ_SIZE = 5 * 6 * 1024;
const int MAX_VIDEOQ_SIZE = 5 * 256 * 1024;

int s_w = 0;
int s_h = 0;
int w_padding = 0;
int h_padding = 0;
int winClientWidth = 0;
int winClientHeight = 0;
int setUpFlag = 0;
int fullScreenFlag = 0;
int flushComplete = 0;
int imageWidth = 0;
int imageHeight = 0;
unsigned char * yuv_buf = NULL;
int updateFlag = 0;
#define true 1
#define false 0

GLfloat _vertices[20];
int _id;
GLuint _program;
GLuint _textureIds[3]; // Texture id of Y,U and V texture.
const char g_indices[] = {0, 3, 2, 0, 2, 1};


//int  VIDEO_PICTURE_QUEUE_SIZE = 5;
#define VIDEO_PICTURE_QUEUE_SIZE 5
#define AV_SYNC_THRESHOLD 0.01
//#define AV_NOSYNC_THRESHOLD 10000.0//10.0
#define DEFAULT_AV_SYNC_TYPE AV_SYNC_AUDIO_MASTER
 /* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0
/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20
 
const int MSG_REFRESH = 1;
const int MSG_EXIT = 2;

int registerCallBackRes = -1;
int out_size = AVCODEC_MAX_AUDIO_FRAME_SIZE*3/2;  

#ifdef WIN32
typedef  CRITICAL_SECTION ffmpeg_lock_t;
#else
typedef  pthread_mutex_t  ffmpeg_lock_t;
#endif

enum {
AV_SYNC_AUDIO_MASTER,
AV_SYNC_VIDEO_MASTER,
AV_SYNC_EXTERNAL_CLOCK,
};

//pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
//pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t s_vsync_cond;
static pthread_mutex_t s_vsync_mutex;

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	//SDL_mutex *mutex;
	//SDL_cond *cond;
} PacketQueue;

typedef struct VideoPicture {
	//SDL_Overlay *bmp;
	AVPicture *pict;
	int width, height;
	int allocated;
	double pts;
} VideoPicture;


typedef struct VideoState {
  AVFormatContext *pFormatCtx;
  int             videoStream, audioStream;
  double          audio_clock;
  AVStream        *audio_st;
  PacketQueue     audioq;
  int16_t         *audio_buf;
  unsigned int    audio_buf_size;
  unsigned int    audio_buf_index;
  AVPacket        audio_pkt;
  uint8_t         *audio_pkt_data;
  int             audio_pkt_size;
  int             audio_hw_buf_size;
  double          frame_timer;
  double          frame_last_pts;
  double          frame_last_delay;
  double          video_clock;
  AVStream        *video_st;
  PacketQueue     videoq;
  VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
  int             pictq_size, pictq_rindex, pictq_windex;
  pthread_mutex_t pictq_mutex;
  pthread_cond_t pictq_cond;
  //SDL_mutex       *pictq_mutex;
  //SDL_cond        *pictq_cond;
  //SDL_Thread      *parse_tid;
  //SDL_Thread      *video_tid;
  int 				decode_tid;
  int 				video_tid;
  char            filename[1024];
  int             quit;
  struct SwsContext *img_convert_ctx;
  ffmpeg_lock_t 	lock;
  double video_current_pts;
  int64_t video_current_pts_time;
  int  av_sync_type;
  double external_clock;                   
  double external_clock_drift;             
  int64_t external_clock_time;             
  double external_clock_speed;  
  double audio_diff_cum; 
  /* used for AV difference average computation */  
  double audio_diff_avg_coef;
  double audio_diff_threshold;
  int audio_diff_avg_count;
  
} VideoState;

uint64_t global_video_pkt_pts = AV_NOPTS_VALUE;

int debug = 0;
JavaVM  *g_jvm;
jobject g_obj; 
int frequency = 44100;
/* Cheat to keep things simple and just use some globals. */
AVFormatContext *pFormatCtx;
AVCodecContext *aCodecCtx;
AVCodecContext *pCodecCtx;
AVCodec *aCodec;


VideoState    *is;
   
// 程序退出标记 1 退出
//int quit = 1;
	
jclass mClass = NULL;
jobject mObject = NULL;
jmethodID refresh = NULL;


enum {
	open_file_fail = -1,
	open_file_success = 0,
	get_stream_info_fail = -2,
	find_video_stream_fail = -3,
	find_audio_stream_fail = -9,
	unsurpport_codec = -4,
	open_codec_fail = - 5,
	bitmap_getinfo_error = -6,
	bitmap_lockpixels_error = -7,
	initialize_conversion_error = -8,
	decode_next_frame = 0,
	stream_read_over = -1
};

int stream_component_open(VideoState *is, int stream_index);
void *decode_thread(void *arg);
void *video_thread(void *arg);
void *audio_thread(void *arg);

void packet_queue_init(PacketQueue *q);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);

//注册回调函数
int registerCallBack(JNIEnv *env);
int GetProviderInstance(JNIEnv *env, jclass obj_class);
//解除回调函数
void unregisterCallBack(JNIEnv *env);

//lock
void ffmpeg_lock_init(ffmpeg_lock_t *lock);
void ffmpeg_lock_enter(ffmpeg_lock_t *lock);
void ffmpeg_lock_leave(ffmpeg_lock_t *lock);
void ffmpeg_lock_destroy(ffmpeg_lock_t *lock);



static void get_video_info(char *prFilename);