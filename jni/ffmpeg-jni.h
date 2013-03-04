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

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	//SDL_mutex *mutex;
	//SDL_cond *cond;
} PacketQueue;

int debug = 0;
JNIEnv *j_env;
jobject j_obj; 
int frequency = 44100;

char *gFileName;	  //the file name of the video
uint8_t *buffer;
AVFormatContext *gFormatCtx;
int gVideoStreamIndex;    //video stream index

AVCodecContext *gVideoCodecCtx;
AVCodecContext *aCodecCtx;
AVCodec *aCodec;
/* Cheat to keep things simple and just use some globals. */
AVFormatContext *pFormatCtx;
AVCodecContext *pCodecCtx;
AVFrame *pFrame;
AVFrame *pFrameRGB;
int videoStream;
int audioStream;

PacketQueue audioq;
int quit = 0;
	
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


void packet_queue_init(PacketQueue *q);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);

//注册回调函数
int registerCallBack(JNIEnv *env);
int GetProviderInstance(JNIEnv *env, jclass obj_class);
//解除回调函数
void unregisterCallBack(JNIEnv *env);