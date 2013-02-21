/**
this is the wrapper of the native functions 
**/
/*android specific headers*/
#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include "ffmpeg-jni.h"

//#include <SDL.h>
//#include <SDL_thread.h>

/*for android logs*/
#define LOG_TAG "FFmpegTest"
#define LOG_LEVEL 10
#define LOGI(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__);}
#define LOGE(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);}


/**/
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

static void fill_bitmap(AndroidBitmapInfo*  info, void *pixels, AVFrame *pFrame)
{
    uint8_t *frameLine;

    int  yy;
    for (yy = 0; yy < info->height; yy++) {
        uint8_t*  line = (uint8_t*)pixels;
        frameLine = (uint8_t *)pFrame->data[0] + (yy * pFrame->linesize[0]);

        int xx;
        for (xx = 0; xx < info->width; xx++) {
            int out_offset = xx * 4;
            int in_offset = xx * 3;

            line[out_offset] = frameLine[in_offset];
            line[out_offset+1] = frameLine[in_offset+1];
            line[out_offset+2] = frameLine[in_offset+2];
            line[out_offset+3] = 0;
        }
        pixels = (char*)pixels + info->stride;
    }
}

JNIEXPORT int JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_openVideoFile(JNIEnv * env, jobject this,jstring name) { int ret;
    int err;
    int i;
    AVCodec *pCodec;
    int numBytes;

    av_register_all();
    LOGI(10,"Registered formats");
    gFileName = (char *)(*env)->GetStringUTFChars(env, name, NULL);
    err = av_open_input_file(&pFormatCtx,gFileName , NULL, 0, NULL);
    LOGI(10,"Called open file");
    if(err!=0) {
        LOGI(10,"Couldn't open file");
        return open_file_fail;
    }
    LOGI(10,"Opened file");
    
    if(av_find_stream_info(pFormatCtx)<0) {
        LOGI(10,"Unable to get stream info");
        return get_stream_info_fail;
    }
    
    videoStream = -1;
    audioStream = -1;
    for (i=0; i<pFormatCtx->nb_streams; i++) {
        if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO && videoStream<0) {
            videoStream = i;
        }
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO && audioStream<0) {
			audioStream = i;
		}
    }
    if(videoStream==-1) {
        LOGE(1,"Unable to find video stream");
        return find_video_stream_fail;
    }
    
    if(audioStream==-1) {
        LOGE(1,"Unable to find audio stream");
        return find_audio_stream_fail;
    }
    pCodecCtx=pFormatCtx->streams[videoStream]->codec;
    
    pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec==NULL) {
        LOGE(1,"Unsupported codec");
        return unsurpport_codec;
    }
    if(avcodec_open(pCodecCtx, pCodec)<0) {
        LOGE(1,"Unable to open codec");
        return open_codec_fail;
    }
    
	aCodecCtx=pFormatCtx->streams[audioStream]->codec;   
	aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
	if(!aCodec) {
		LOGE(1,"Unsupported audio codec!");
		return unsurpport_codec;
	}
	if(avcodec_open(aCodecCtx, aCodec)<0){
        LOGE(1,"Unable to open audio codec");
		return open_codec_fail;
	}
    pFrame=avcodec_alloc_frame();
    pFrameRGB=avcodec_alloc_frame();
    LOGI(10,"Video size is [%d x %d]", pCodecCtx->width, pCodecCtx->height);
    //LOGI(10,"Video during = %d", pFormatCtx-);

    numBytes=avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
    buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

    avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
  return  open_file_success;
}

int Java_com_sky_drovik_player_ffmpeg_JniUtils_drawFrame(JNIEnv * env, jobject this, jstring bitmap)
{
    AndroidBitmapInfo  info;
    void*              pixels;
    int                ret;

    int err;
    int i;
    int frameFinished = 0;
    AVPacket packet;
    static struct SwsContext *img_convert_ctx;
    int64_t seek_target;

    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE(1,"AndroidBitmap_getInfo() failed ! error=%d", ret);
        return bitmap_getinfo_error;
    }
   // LOGI(10,"Checked on the bitmap");

    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGE(1,"AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }

    i = 0;
    while((i==0) && (av_read_frame(pFormatCtx, &packet)>=0)) {
  		if(packet.stream_index==videoStream) {
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
    		if(frameFinished) {
                //LOGE(1,"packet pts %llu", packet.pts);
                // This is much different than the tutorial, sws_scale
                // replaces img_convert, but it's not a complete drop in.
                // This version keeps the image the same size but swaps to
                // RGB24 format, which works perfect for PPM output.
                //int target_width = pCodecCtx->width;//320;
                //int target_height = pCodecCtx->height;//240;
                img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, 
                       pCodecCtx->pix_fmt, 
                       pCodecCtx->width, pCodecCtx->height, PIX_FMT_RGB24, SWS_BICUBIC, 
                       NULL, NULL, NULL);
                if(img_convert_ctx == NULL) {
                    LOGI(10,"could not initialize conversion context\n");
                    return initialize_conversion_error;
                }
                sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

                // save_frame(pFrameRGB, target_width, target_height, i);
                fill_bitmap(&info, pixels, pFrameRGB);
                i = 1;
    	    }
        }
        av_free_packet(&packet);
	return decode_next_frame;
    }

   AndroidBitmap_unlockPixels(env, bitmap);
   LOGI(10,"exit\n");
   return stream_read_over;
}

JNIEXPORT jintArray JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_getVideoResolution(JNIEnv *pEnv, jobject pObj) {
    jintArray lRes;
    lRes = (*pEnv)->NewIntArray(pEnv, 4);
    if (lRes == NULL) {
        LOGI(1, "cannot allocate memory for video size");
        return NULL;
    }
    jint lVideoRes[4];
    lVideoRes[0] = pCodecCtx->width;
    lVideoRes[1] = pCodecCtx->height;
    lVideoRes[2] = pCodecCtx->time_base.den;
    lVideoRes[3] = pCodecCtx->time_base.num;
    LOGI(1, "time den  = %d,num  = %d, video duration = %d,",pCodecCtx->time_base.num,pCodecCtx->time_base.den, pCodecCtx->bit_rate);
    (*pEnv)->SetIntArrayRegion(pEnv, lRes, 0, 4, lVideoRes);
    return lRes;
}
/*
int seek_frame(int tsms)
{
    int64_t frame;

    frame = av_rescale(tsms,pFormatCtx->streams[videoStream]->time_base.den,pFormatCtx->streams[videoStream]->time_base.num);
    frame/=1000;
    
    if(avformat_seek_file(pFormatCtx,videoStream,0,frame,frame,AVSEEK_FLAG_FRAME)<0) {
        return 0;
    }

    avcodec_flush_buffers(pCodecCtx);

    return 1;
}
*/
/*
void Java_com_iped_ffmpeg_test_Main_drawFrameAt(JNIEnv * env, jobject this, jstring bitmap, jint secs)
{
    AndroidBitmapInfo  info;
    void*              pixels;
    int                ret;

    int err;
    int i;
    int frameFinished = 0;
    AVPacket packet;
    static struct SwsContext *img_convert_ctx;
    int64_t seek_target;

    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGI(10,"AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }
    LOGI(10,"Checked on the bitmap");

    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGE(1,"AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }
    LOGI(10,"Grabbed the pixels");

    seek_frame(secs * 1000);

    i = 0;
    while ((i== 0) && (av_read_frame(pFormatCtx, &packet)>=0)) {
  		if(packet.stream_index==videoStream) {
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
    
    		if(frameFinished) {
                // This is much different than the tutorial, sws_scale
                // replaces img_convert, but it's not a complete drop in.
                // This version keeps the image the same size but swaps to
                // RGB24 format, which works perfect for PPM output.
                int target_width = 320;
                int target_height = 240;
                img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, 
                       pCodecCtx->pix_fmt, 
                       target_width, target_height, PIX_FMT_RGB24, SWS_BICUBIC, 
                       NULL, NULL, NULL);
                if(img_convert_ctx == NULL) {
                    LOGE(1,"could not initialize conversion context\n");
                    return;
                }
                sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

                // save_frame(pFrameRGB, target_width, target_height, i);
                fill_bitmap(&info, pixels, pFrameRGB);
                i = 1;
    	    }
        }
        av_free_packet(&packet);
    }

    //AndroidBitmap_unlockPixels(env, bitmap);
}
*/
static void get_video_info(char *prFilename);

/*parsing the video file, done by parse thread*/
static void get_video_info(char *prFilename) {
    AVCodec *lVideoCodec;
    int lError;
    /*some global variables initialization*/
    LOGI(10, "get video info starts!");
    /*register the codec*/
    extern AVCodec ff_h263_decoder;
    avcodec_register(&ff_h263_decoder);
    extern AVCodec ff_h264_decoder;
    avcodec_register(&ff_h264_decoder);
    extern AVCodec ff_mpeg4_decoder;
    avcodec_register(&ff_mpeg4_decoder);
    extern AVCodec ff_mjpeg_decoder;
    avcodec_register(&ff_mjpeg_decoder);
    /*register parsers*/
    //extern AVCodecParser ff_h264_parser;
    //av_register_codec_parser(&ff_h264_parser);
    //extern AVCodecParser ff_mpeg4video_parser;
    //av_register_codec_parser(&ff_mpeg4video_parser);
    /*register demux*/
    extern AVInputFormat ff_mov_demuxer;
    av_register_input_format(&ff_mov_demuxer);
    //extern AVInputFormat ff_h264_demuxer;
    //av_register_input_format(&ff_h264_demuxer);
    /*register the protocol*/
    extern URLProtocol ff_file_protocol;
    av_register_protocol2(&ff_file_protocol, sizeof(ff_file_protocol));
    /*open the video file*/
    if ((lError = av_open_input_file(&gFormatCtx, gFileName, NULL, 0, NULL)) !=0 ) {
        LOGE(1, "Error open video file: %d", lError);
        return;	//open file failed
    }
    /*retrieve stream information*/
    if ((lError = av_find_stream_info(gFormatCtx)) < 0) {
        LOGE(1, "Error find stream information: %d", lError);
        return;
    } 
    /*find the video stream and its decoder*/
    gVideoStreamIndex = av_find_best_stream(gFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &lVideoCodec, 0);
    if (gVideoStreamIndex == AVERROR_STREAM_NOT_FOUND) {
        LOGE(1, "Error: cannot find a video stream");
        return;
    } else {
	LOGI(10, "video codec: %s", lVideoCodec->name);
    }
    if (gVideoStreamIndex == AVERROR_DECODER_NOT_FOUND) {
        LOGE(1, "Error: video stream found, but no decoder is found!");
        return;
    }   
    /*open the codec*/
    gVideoCodecCtx = gFormatCtx->streams[gVideoStreamIndex]->codec;
    LOGI(10, "open codec: (%d, %d)", gVideoCodecCtx->height, gVideoCodecCtx->width);
#ifdef SELECTIVE_DECODING
    gVideoCodecCtx->allow_selective_decoding = 1;
#endif
    if (avcodec_open(gVideoCodecCtx, lVideoCodec) < 0) {
	LOGE(1, "Error: cannot open the video codec!");
        return;
    }
    LOGI(10, "get video info ends");
}

JNIEXPORT void JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_close(JNIEnv *pEnv, jobject pObj) {

    /* close the RGB image */
    av_free(buffer);

    av_free(pFrameRGB);
    
    // Free the YUV frame
    av_free(pFrame);

    /*close the video codec*/
    avcodec_close(pCodecCtx);
    /*close the video file*/
    av_close_input_file(pFormatCtx);	
}

JNIEXPORT void JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_init(JNIEnv *pEnv, jobject pObj, jstring pFileName) {
    int l_mbH, l_mbW;
    /*get the video file name*/
    gFileName = (char *)(*pEnv)->GetStringUTFChars(pEnv, pFileName, NULL);
    if (gFileName == NULL) {
        LOGE(1, "Error: cannot get the video file name!");
        return;
    } 
    LOGI(10, "video file name is %s", gFileName);
    get_video_info(gFileName);
    LOGI(10, "initialization done");
}

JNIEXPORT jstring JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_videoCodecName(JNIEnv *pEnv, jobject pObj) {
    char* lCodecName = gVideoCodecCtx->codec->name;
    return (*pEnv)->NewStringUTF(pEnv, lCodecName);
}

JNIEXPORT jstring JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_videoFormatName(JNIEnv *pEnv, jobject pObj) {
    char* lFormatName = gFormatCtx->iformat->name;
    return (*pEnv)->NewStringUTF(pEnv, lFormatName);
}






