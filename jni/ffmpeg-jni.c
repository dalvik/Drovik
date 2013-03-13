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
			if(debug) LOGI(10,"packet_queue_get  quit!");
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
				LOGI(10,"packet get info: pts = %d, packet size = %d", pkt->pts, pkt->size);
				break;
		} else if (!block) {
			ret = 0;
			break;
		} else {
			//SDL_CondWait(q->cond, q->mutex);
			if(debug) LOGI(10,"^^^^^^^^^^^^^^");
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
		LOGI(10,"av_dup_packet < 0");
		return -1;
	}
	pkt1 = av_malloc(sizeof(AVPacketList));
	if (!pkt1) {
		return -1;
	}
	//LOGI(10,"packet put info: packet size = %d, duration = %d, pos = %d", pkt->size, pkt->duration, pkt->pos);
	//LOGI(10, "### packet ------  data size = %d", pkt->size);
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
	//LOGI(10, "### packet ------ pkt1->pkt.size = %d", pkt1->pkt.size);
	pthread_cond_signal(&q->cond);//pthread_cond_signal
	pthread_mutex_unlock(&q->mutex);
	//SDL_CondSignal(q->cond);
	//SDL_UnlockMutex(q->mutex);
	return 0;
}

JNIEXPORT int JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_openVideoFile(JNIEnv * env, jobject this,jstring name, jint d) { 
	int ret;
	debug = d;
    
    int i;
    
	(*env)->GetJavaVM(env, &g_jvm);
	g_obj = (*env)->NewGlobalRef(env,g_obj);
	is = av_mallocz(sizeof(VideoState));
	av_register_all();
    gFileName = (char *)(*env)->GetStringUTFChars(env, name, NULL);
		
	pthread_t decode;
	is->decode_tid =  pthread_create(&decode, NULL, &decode_thread, is);
	if(debug)  LOGE(1,"pthread_create  decode_thread");
	
	///pstrcpy(is->filename, sizeof(is->filename), gFileName);	
	//if(debug) LOGI(1,"audio rate = %d, channel = %d, sample_fmt = %d, frame_size = %d", aCodecCtx->sample_rate, aCodecCtx->channels, aCodecCtx->sample_fmt, aCodecCtx->frame_size);
	//if(!is->video_tid) {
	//	av_free(is);
	//	return open_codec_fail;
	//}
	
	return  open_file_success;
}

void *decode_thread(void *arg) {
	VideoState *is = (VideoState*)arg;
	AVFormatContext *pFormatCtx;
	AVPacket pkt1, *packet = &pkt1;
    AVCodec *pCodec;
	
	AVCodecContext *pCodecCtx;

	int i;

	is->videoStream = -1;
	is->audioStream = -1;
	int err;		
	int videoStream = -1;
    int audioStream = -1;
    err = av_open_input_file(&pFormatCtx,gFileName , NULL, 0, NULL);
    if(err!=0) {
		if(debug) LOGI(10,"Couldn't open file");
        return open_file_fail;
    }
	is->pFormatCtx = pFormatCtx;
    if(av_find_stream_info(pFormatCtx)<0) {
		if(debug) LOGI(10,"Unable to get stream info");
        return get_stream_info_fail;
    }
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
			//return unsurpport_codec;
		}else {
			if(avcodec_open(pCodecCtx, pCodec)<0){
				if(debug)  LOGE(1,"Unable to open audio codec");
				//return open_codec_fail;
			} else {
				is->videoStream = videoStream;
				is->video_st = pFormatCtx->streams[videoStream];
				is->frame_timer = (double)av_gettime() / 1000000.0;
				is->frame_last_delay = 40e-3;
				packet_queue_init(&is->videoq);
				is->quit = 0; //1 exit
				
				pthread_t video;
				is->video_tid =  pthread_create(&video, NULL, &video_thread, is);
				if(debug)  LOGE(1,"pthread_create  video_thread");	
			}
		}
    }
    //AVPacket packet;
	// int frameFinished = 0;
    while(!is->quit) {
		//LOGI(10, "### audioq size = %d, videoq size = %d", is->audioq.size, is->videoq.size);
 		if(is->videoq.size > MAX_VIDEOQ_SIZE) {// 
		   usleep(100000); //50 ms
		   continue;
		}
		if(av_read_frame(is->pFormatCtx, packet)<0){
			is->quit = 1;
			break;
		}
  		if(packet->stream_index==is->videoStream) {
			packet_queue_put(&is->videoq, packet);
        } else {
			av_free_packet(packet);
		}
    }
	LOGI(10,"exit\n");
	return ((void *)0);
}

void *video_thread(void *arg) {
  VideoState *is = (VideoState*)arg;
  AVPacket pkt1, *packet = &pkt1;
  int len1, frameFinished;
  AVFrame *pFrame;
  double pts;
  int numBytes;
  pFrame=avcodec_alloc_frame();
  for(;;) {
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
	  if(debug) LOGI(10,"video_thread get packet exit");
      break;
    }
	LOGI(10,"packet get ------  result : pts = %d, packet size = %d", packet->pts, packet->size);
    pts = 0;
    global_video_pkt_pts = packet->pts;
	//LOGI(10,"packet info: pts = %d, dts = %d, packet size = %d, stream_index = %d, flags = %d, duration = %d, pos = %d", packet->pts , packet->dts, packet->size, packet->stream_index, packet->flags, packet->duration, packet->pos);
    len1 = avcodec_decode_video2(is->video_st->codec,
				pFrame,
				&frameFinished,
				packet);
	if(debug) LOGI(10,"video_decode length = %d, packet size = %d", len1 , packet->size);
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
      //pts = synchronize_video(is, pFrame, pts);
      // if (queue_picture(is, pFrame, pts) < 0) {
	  //	break;
      // }
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

