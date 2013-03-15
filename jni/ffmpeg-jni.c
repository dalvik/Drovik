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

jstring bitmap;

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

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
	AVPacketList *pkt1;
	int ret;
	//SDL_LockMutex(q->mutex);
	pthread_mutex_lock(&q->mutex);
	for(;;) {
		if(is->quit) {
			ret = -1;
			break;
		}
		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
				q->nb_packets--;
				q->size -= pkt1->pkt.size;
				*pkt = pkt1->pkt;
				av_free(pkt1);
				ret = 1;
				break;
		} else if (!block) {
			ret = 0;
			break;
		} else {
			//SDL_CondWait(q->cond, q->mutex);
			pthread_cond_wait(&q->cond, &q->mutex);
		}
	}
	//SDL_UnlockMutex(q->mutex);
	pthread_mutex_unlock(&q->mutex);
	return ret;
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt){
	AVPacketList *pkt1;
	if(av_dup_packet(pkt) < 0) {
		return -1;
	}
	pkt1 = av_malloc(sizeof(AVPacketList));
	if (!pkt1) {
		return -1;
	}
	pkt1->pkt = *pkt;
	pkt1->next = NULL;
	//SDL_LockMutex(q->mutex);
	pthread_mutex_lock(&q->mutex);
	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	pthread_cond_signal(&q->cond);
	pthread_mutex_unlock(&q->mutex);
	//SDL_CondSignal(q->cond);
	//SDL_UnlockMutex(q->mutex);
	return 0;
}

int our_get_buffer(struct AVCodecContext *c, AVFrame *pic) {
  int ret = avcodec_default_get_buffer(c, pic);
  uint64_t *pts = av_malloc(sizeof(uint64_t));
  *pts = global_video_pkt_pts;
  pic->opaque = pts;
  return ret;
}

void our_release_buffer(struct AVCodecContext *c, AVFrame *pic) {
  if (pic) {
    av_freep(&pic->opaque);
  }
  avcodec_default_release_buffer(c, pic);
}

JNIEXPORT jintArray JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_openVideoFile(JNIEnv * env, jobject this,jstring name, jint d) { 
	jintArray videoInfo;
	int arrLen = 4;
    videoInfo = (*env)->NewIntArray(env, arrLen);
    if (videoInfo == NULL) {
        if(debug)LOGI(1, "cannot allocate memory for video size");
        return NULL;
    }
    jint lVideoRes[arrLen];
    int ret;
	debug = d;
    
	(*env)->GetJavaVM(env, &g_jvm);
	g_obj = (*env)->NewGlobalRef(env,g_obj);
	is = av_mallocz(sizeof(VideoState));
	av_register_all();
    gFileName = (char *)(*env)->GetStringUTFChars(env, name, NULL);
	//is->pictq_mutex = SDL_CreateMutex();
	//is->pictq_cond = SDL_CreateCond();
	pthread_mutex_init(&is->pictq_mutex, NULL);
	pthread_mutex_init(&is->pictq_cond, NULL);
	
	AVFormatContext *pFormatCtx;
	if(av_open_input_file(&pFormatCtx,gFileName , NULL, 0, NULL)!=0) {
		if(debug) LOGI(10,"Couldn't open file");
		lVideoRes[0] = open_file_fail;
		(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
		return videoInfo;
    }
	is->pFormatCtx = pFormatCtx;    
    if(av_find_stream_info(pFormatCtx)<0) {
		if(debug) LOGI(10,"Unable to get stream info");
		lVideoRes[0] = get_stream_info_fail;
		(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
		return videoInfo;
    }
	int i;
	is->videoStream = -1;
	is->audioStream = -1;	
	int videoStream = -1;
    int audioStream = -1;
	AVCodec *pCodec;
	AVCodecContext *pCodecCtx;
	for (i=0; i<pFormatCtx->nb_streams; i++) {
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO && videoStream<0) {
			videoStream = i;
		}
    }
	if(videoStream>=0) {
		pCodecCtx=pFormatCtx->streams[videoStream]->codec;
		is->img_convert_ctx = sws_getContext(pCodecCtx->width,
				   pCodecCtx->height,
				   pCodecCtx->pix_fmt,
				   pCodecCtx->width,
				   pCodecCtx->height,
				   PIX_FMT_RGB24,
				   SWS_BICUBIC,
				   NULL, NULL, NULL);
		pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
		if(!pCodec) {
			if(debug)  LOGE(1,"Unsupported audio codec!");
			lVideoRes[0] = unsurpport_codec;
			(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
			return videoInfo;
		}else {
			if(avcodec_open(pCodecCtx, pCodec)<0){
				if(debug)  LOGE(1,"Unable to open audio codec");
				lVideoRes[0] = open_codec_fail;
				(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
				return videoInfo;
			} else {
				is->videoStream = videoStream;
				is->video_st = pFormatCtx->streams[videoStream];
				is->frame_timer = (double)av_gettime() / 1000000.0;
				is->frame_last_delay = 40e-3;
				packet_queue_init(&is->videoq);
				is->quit = 0; //1 exit
				pCodecCtx->get_buffer = our_get_buffer;
				pCodecCtx->release_buffer = our_release_buffer;
			}
		}
    }
	lVideoRes[0] = pCodecCtx->width;
    lVideoRes[1] = pCodecCtx->height;
    lVideoRes[2] = pCodecCtx->time_base.den;
    lVideoRes[3] = pCodecCtx->time_base.num;
    //LOGI(1, "time den  = %d,num  = %d, video duration = %d,",pCodecCtx->time_base.num,pCodecCtx->time_base.den, pCodecCtx->bit_rate);
	(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
	return  videoInfo;
}

int Java_com_sky_drovik_player_ffmpeg_JniUtils_decodeMedia(JNIEnv * env, jobject this, jstring rect)
{
	int ret;
	pthread_t decode;
	is->decode_tid =  pthread_create(&decode, NULL, &decode_thread, is);
	if(debug)  LOGE(1,"pthread_create  decode_thread");
	if(debug) LOGI(1,"start thread id = %d", is->decode_tid);
	pthread_t video;
	is->video_tid =  pthread_create(&video, NULL, &video_thread, is);
	if(debug)  LOGE(1,"pthread_create  video_thread");
	return is->video_tid;
}

int Java_com_sky_drovik_player_ffmpeg_JniUtils_display(JNIEnv * env, jobject this, jstring bitmap)
{
	AndroidBitmapInfo  info;
	int ret;
     if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE(1,"AndroidBitmap_getInfo() failed ! error=%d", ret);
        return bitmap_getinfo_error;
	}
	while(!is->quit && is->video_st) {
		//fill_bitmap(&info, pixels, pFrameRGB);
		//AndroidBitmap_unlockPixels(env, bitmap);
		usleep(1000000);
		LOGI(1,"display");
	}
	return 0;
}

int queue_picture(VideoState *is, AVFrame *pFrame, double pts) {
  VideoPicture *vp;
  int dst_pix_fmt;
  AVPicture pict;
  
  //pFrameRGB=avcodec_alloc_frame();
  //SDL_LockMutex(is->pictq_mutex);
  pthread_mutex_lock(&is->pictq_mutex);
  while(is->pictq_size>=VIDEO_PICTURE_QUEUE_SIZE &&
	!is->quit) {
    //SDL_CondWait(is->pictq_cond, is->pictq_mutex);
	if(debug) LOGE(10, "picture is full");
	pthread_cond_wait(&is->pictq_cond, &is->pictq_mutex);
  }
  //SDL_UnlockMutex(is->pictq_mutex);
  pthread_mutex_unlock(&is->pictq_mutex);
  if(is->quit) {
    return -1;
  }
  vp = &is->pictq[is->pictq_windex];
 /*
  if (!vp->bmp ||
      vp->width != is->video_st->codec->width ||
      vp->height != is->video_st->codec->height) {
    //SDL_Event event;
    
    vp->allocated = 0;
    //event.type = FF_ALLOC_EVENT;
    //event.user.data1 = is;
    //SDL_PushEvent(&event);
    
    //SDL_LockMutex(is->pictq_mutex);
	pthread_mutex_lock(&is->pictq_mutex);
    while(!vp->allocated && !is->quit) {
      //SDL_CondWait(is->pictq_cond, is->pictq_mutex);
	  pthread_cond_wait(&is->pictq_cond, &is->pictq_mutex);
    }
    //SDL_UnlockMutex(is->pictq_mutex);
	pthread_mutex_unlock(&is->pictq_mutex);
    if (is->quit) {
      return -1;
    }
  }*/
  
  //if (vp->bmp) {
    //SDL_LockYUVOverlay(vp->bmp);
    dst_pix_fmt = PIX_FMT_RGB24;
    //pict.data[0] = vp->bmp->pixels[0];
    //pict.data[1] = vp->bmp->pixels[2];
    //pict.data[2] = vp->bmp->pixels[1];

    //pict.linesize[0] = vp->bmp->pitches[0];
    //pict.linesize[1] = vp->bmp->pitches[2];
    //pict.linesize[2] = vp->bmp->pitches[1];
    //
    sws_scale(is->img_convert_ctx,
	      pFrame->data,
	      pFrame->linesize, 0,
	      is->video_st->codec->height,
	      pict.data,
	      pict.linesize);
    //
    //SDL_UnlockYUVOverlay(vp->bmp);
    vp->pts = pts;

    if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
      is->pictq_windex = 0;
    }
    //SDL_LockMutex(is->pictq_mutex);
	pthread_mutex_lock(&is->pictq_mutex);
    is->pictq_size ++;
    //SDL_UnlockMutex(is->pictq_mutex);
	pthread_mutex_unlock(&is->pictq_mutex);
 // }
  return 0;
}

double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {
  double frame_delay;
  if (pts != 0) {
    is->video_clock = pts;
  } else {
    pts = is->video_clock;
  }
  frame_delay = av_q2d(is->video_st->codec->time_base);
  frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
  is->video_clock += frame_delay;
  return pts;
}

void *decode_thread(void *arg) {
	VideoState *is = (VideoState*)arg;
    AVPacket packet;
    while(!is->quit) {
 		if(is->videoq.size > MAX_VIDEOQ_SIZE) {// 
		   usleep(100000); //50 ms
		   continue;
		}
		if(av_read_frame(is->pFormatCtx, &packet)<0){
			is->quit = 1;
			break;
		}
  		if(packet.stream_index==is->videoStream) {
			packet_queue_put(&is->videoq, &packet);
        } else if (packet.stream_index == is->audioStream) {
		  packet_queue_put(&is->audioq, &packet);
		} else {
			av_free_packet(&packet);
		}
    }
	while (!is->quit) {
		usleep(100000);
	}
	LOGI(10,"exit\n");
	return ((void *)0);
}

void *video_thread(void *arg) {
  VideoState *is = (VideoState*)arg;
  AVPacket pkt1, *packet = &pkt1;
  int len1, frameFinished;
  AVFrame *pFrame;
  AVFrame *pFrameRGB;
  double pts;
  int numBytes;
  pFrame=avcodec_alloc_frame();
  int ret;
  /*
  AndroidBitmapInfo  info;
  void* pixels;
  static struct SwsContext *img_convert_ctx;
  JNIEnv *env;
  if((*g_jvm)->AttachCurrentThread(g_jvm,(void **)&env, NULL)<0) {
		if(debug) LOGE(1,"AttachCurrentThread fail!");
		return;
  }
  if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE(1,"AndroidBitmap_getInfo() failed ! error=%d", ret);
        return bitmap_getinfo_error;
  }*/
  for(;;) {
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
	  if(debug) LOGI(10,"video_thread get packet exit");
      break;
    }
    pts = 0;
    global_video_pkt_pts = packet->pts;
    len1 = avcodec_decode_video2(is->video_st->codec,
				pFrame,
				&frameFinished,
				packet);
    if(packet->dts == AV_NOPTS_VALUE
       && pFrame->opaque
       && *(uint64_t*)pFrame->opaque
       != AV_NOPTS_VALUE) {
      pts = *(uint64_t*) pFrame->opaque;
    } else if (packet->dts != AV_NOPTS_VALUE) {
      pts = packet->dts;
    } else {
      pts = 0;
    }
    pts *= av_q2d(is->video_st->time_base);
    
    if (frameFinished) {
	   LOGI(10, "#### show pic");
	   /*
	   img_convert_ctx = sws_getContext(is->video_st->codec->width, is->video_st->codec->height, 
                       is->video_st->codec->pix_fmt, 
                       is->video_st->codec->width, is->video_st->codec->height, PIX_FMT_RGB24, SWS_BICUBIC, 
                       NULL, NULL, NULL);
		if(img_convert_ctx == NULL) {
			LOGI(10,"could not initialize conversion context\n");
			return;
		}
		sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, is->video_st->codec->height, pFrameRGB->data, pFrameRGB->linesize);
				
		// save_frame(pFrameRGB, target_width, target_height, i);
		fill_bitmap(&info, pixels, pFrameRGB);
		AndroidBitmap_unlockPixels(env, bitmap);
		*/
       pts = synchronize_video(is, pFrame, pts);
       if (queue_picture(is, pFrame, pts) < 0) {
			break;
       }
    }
    av_free_packet(packet);
  }
  av_free(pFrame);
  return ((void *)0);
}

JNIEXPORT jintArray JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_getVideoResolution(JNIEnv *pEnv, jobject pObj) {
    jintArray lRes;
    lRes = (*pEnv)->NewIntArray(pEnv, 4);
    if (lRes == NULL) {
        LOGI(1, "cannot allocate memory for video size");
        return NULL;
    }
    jint lVideoRes[4];
    lVideoRes[0] = 1;//pCodecCtx->width;
    lVideoRes[1] = 1;//pCodecCtx->height;
    lVideoRes[2] = 1;//pCodecCtx->time_base.den;
    lVideoRes[3] = 1;//pCodecCtx->time_base.num;
    //LOGI(1, "time den  = %d,num  = %d, video duration = %d,",pCodecCtx->time_base.num,pCodecCtx->time_base.den, pCodecCtx->bit_rate);
    (*pEnv)->SetIntArrayRegion(pEnv, lRes, 0, 4, lVideoRes);
    return lRes;
}

void packet_queue_init(PacketQueue *q) {
	memset(q, 0, sizeof(PacketQueue));
	pthread_mutex_init(&q->mutex, NULL);
	pthread_mutex_init(&q->cond, NULL);
}

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

/*----------------------*/

JNIEXPORT void JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_close(JNIEnv *pEnv, jobject pObj) {

	av_free(is);
    /* close the RGB image */
    av_free(buffer);

    av_free(pFrameRGB);
    
    // Free the YUV frame
    //av_free(pFrame);

    /*close the video codec*/
    //avcodec_close(pCodecCtx);
    /*close the video file*/
    av_close_input_file(pFormatCtx);

	unRegisterCallBack(pEnv);	
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


int registerCallBack(JNIEnv *env) {
	if(mClass == NULL) {
		mClass = (*env)->FindClass(env, "com/sky/drovik/player/media/MovieView");
		if(mClass == NULL){
			return -1;
		}
		LOGI(10,"register local class OK.");
	}
	if (mObject == NULL) {
		if (GetProviderInstance(env, mClass) != 1) {
			//(*env)->DeleteLocalRef(env, mClass);
			return -1;
		}
		LOGI(10,"register local object OK.");
	}
	if(refresh == NULL) {
		refresh = (*env)->GetMethodID(env, mClass, "callBackRefresh","()V");
		if(refresh == NULL) {
			//(*env)->DeleteLocalRef(env, mClass);
			//(*env)->DeleteLocalRef(env, mObject);
			return -3;
		}
	}
	return 0;
}

int GetProviderInstance(JNIEnv *env,jclass obj_class) {
	jmethodID construction_id = (*env)->GetMethodID(env, obj_class,	"<init>", "()V");
	if (construction_id == 0) {
		return -1;
	}
	mObject = (*env)->NewObject(env, obj_class, construction_id);
	if (mObject == NULL) {
		return -2;
	}
	return 1;
}

void unRegisterCallBack(JNIEnv *env) {
	if(mClass) {
		//(*env)->DeleteLocalRef(env, mClass);
	}
	if(mObject) {
		//(*env)->DeleteLocalRef(env, mObject);
	}
	//(*env)->CallVoidMethod(env,audio_track, method_release);
	LOGI(10,"unregister native OK.");
}

void ffmpeg_lock_init(ffmpeg_lock_t *lock)
{
	#ifndef WIN32
		pthread_mutex_init(lock, NULL);
	#else
		InitializeCriticalSection(lock);
	#endif
}

void ffmpeg_lock_enter(ffmpeg_lock_t *lock)
{
	#ifndef WIN32
		pthread_mutex_lock(lock);
	#else
		EnterCriticalSection(lock);
	#endif
}

void ffmpeg_lock_leave(ffmpeg_lock_t *lock)
{
	#ifndef WIN32
		pthread_mutex_unlock(lock);
	#else
		LeaveCriticalSection(lock);
	#endif
}

void ffmpeg_lock_destroy(ffmpeg_lock_t *lock)
{
	#ifndef WIN32
		pthread_mutex_destroy(lock);
	#else
		DeleteCriticalSection(lock);
	#endif
}

