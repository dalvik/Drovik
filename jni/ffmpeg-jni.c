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
	pthread_mutex_lock(&mut);
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
				break;
		} else if (!block) {
			ret = 0;
			break;
		} else {
			//SDL_CondWait(q->cond, q->mutex);
			if(debug) LOGI(10,"^^^^^^^^^^^^^^");
			pthread_cond_wait(&cond, &mut);
		}
	}
	//SDL_UnlockMutex(q->mutex);
	pthread_mutex_unlock(&mut);
	return ret;
}

JNIEXPORT int JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_openVideoFile(JNIEnv * env, jobject this,jstring name, jint d) { 
	int ret;
	debug = d;
    int err;
    int i;
    AVCodec *pCodec;
    av_register_all();
	(*env)->GetJavaVM(env, &g_jvm);
	g_obj = (*env)->NewGlobalRef(env,g_obj);
    gFileName = (char *)(*env)->GetStringUTFChars(env, name, NULL);
    err = av_open_input_file(&pFormatCtx,gFileName , NULL, 0, NULL);
    if(err!=0) {
		if(debug) LOGI(10,"Couldn't open file");
        return open_file_fail;
    }
    if(av_find_stream_info(pFormatCtx)<0) {
		if(debug) LOGI(10,"Unable to get stream info");
        return get_stream_info_fail;
    }
	is = av_mallocz(sizeof(VideoState));
	is->pFormatCtx = pFormatCtx;
    int videoStream = -1;
    int audioStream = -1;
	///pstrcpy(is->filename, sizeof(is->filename), gFileName);
    for (i=0; i<pFormatCtx->nb_streams; i++) {
        if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO && videoStream<0) {
            videoStream = i;
        }
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO && audioStream<0) {
			audioStream = i;
		}
    }
	
    if(videoStream>=0) {
		ffmpeg_lock_init(&is->lock);
		pCodecCtx=pFormatCtx->streams[videoStream]->codec;
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
			}
		}
    }
    if(audioStream>=0) {
		aCodecCtx=pFormatCtx->streams[audioStream]->codec;   
		aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
		if(!aCodec) {
			if(debug)  LOGE(1,"Unsupported audio codec!");
			//return unsurpport_codec;
		} else {
			if(avcodec_open(aCodecCtx, aCodec)<0){
				if(debug)  LOGE(1,"Unable to open audio codec");
				//return open_codec_fail;
			} else {
				is->audioStream = audioStream;
				is->audio_st = pFormatCtx->streams[audioStream];
				is->audio_buf_size = 0;
				is->audio_buf_index = 0;
				is->quit = 0;
				memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
				packet_queue_init(&is->audioq);
			}
		}		
    }
	if(is->videoStream < 0 || is->audioStream < 0) {
		LOGI(10, "%s: could not open codecs", gFileName);
		return open_file_fail;
	}	
	pthread_t decode;
	is->decode_tid =  pthread_create(&decode, NULL, &decode_thread, is);
	if(debug)  LOGE(1,"pthread_create  decode_thread");
	pthread_t video;
	is->video_tid =  pthread_create(&video, NULL, &video_thread, is);
	if(debug)  LOGE(1,"pthread_create  video_thread");
	
	if(debug) LOGI(1,"audio rate = %d, channel = %d, sample_fmt = %d, frame_size = %d", aCodecCtx->sample_rate, aCodecCtx->channels, aCodecCtx->sample_fmt, aCodecCtx->frame_size);
	//if(!is->video_tid) {
	//	av_free(is);
	//	return open_codec_fail;
	//}
	
	return  open_file_success;
}

int stream_component_open(VideoState *is, int stream_index) {
  //AVFormatContext *pFormatCtx = is->pFormatCtx;
  AVCodecContext *codecCtx;
  AVCodec *codec;
  int numBytes;
  if(stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
    return -1;
  }

	
  if(debug) LOGI(10,"55555 stream_index= %d, %p", stream_index,pFormatCtx);
  codecCtx = pFormatCtx->streams[stream_index]->codec;
  //is->img_convert_ctx = sws_getContext(codecCtx->width,codecCtx->height,codecCtx->pix_fmt,codecCtx->width,codecCtx->height,PIX_FMT_RGB24,SWS_BICUBIC,NULL, NULL, NULL);

  if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
  /*
    wanted_spec.freq = codecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = codecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = is;
 */
    //is->audio_hw_buf_size = spec.size;
  }
  if(debug) LOGI(10,"6666");
  codec = avcodec_find_decoder(codecCtx->codec_id);

  if(!codec || (avcodec_open(codecCtx, codec)) < 0) {
    if(debug) LOGE(1,"Unsupported codec");
	av_free(is);
    return -1;
  }
  /*
  switch(codecCtx->codec_type) {
  case AVMEDIA_TYPE_AUDIO:
    is->audioStream = stream_index;
    is->audio_st = pFormatCtx->streams[stream_index];
    is->audio_buf_size = 0;
    is->audio_buf_index = 0;
    memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
    packet_queue_init(&is->audioq);
    //SDL_PauseAudio(0);
    break;
  case AVMEDIA_TYPE_VIDEO:
    is->videoStream = stream_index;
    is->video_st = pFormatCtx->streams[stream_index];
    is->frame_timer = (double)av_gettime() / 1000000.0;
    is->frame_last_delay = 40e-3;
    packet_queue_init(&is->videoq);
    //is->video_tid = SDL_CreateThread(video_thread, is);
	pthread_t pt;
	is->video_tid =  pthread_create(&pt, NULL, &decode_thread, is);
    //codecCtx->get_buffer = our_get_buffer;
    //codecCtx->release_buffer = our_release_buffer;
    break;
  default:
    break;
  }*/
}

int Java_com_sky_drovik_player_ffmpeg_JniUtils_decodeMedia(JNIEnv * env, jobject this, jstring bitmap)
{

	pthread_t pt;
	return 0;//pthread_create(&pt, NULL, &decode_thread, is);
    //stream_read_over;
}

void *decode_thread(void *arg) {
	JNIEnv *env;
	if((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != JNI_OK) {
		return -1;
	}
	VideoState *is = (VideoState*)arg;
    AVPacket packet;
    while(!is->quit) {
		LOGI(10, "### audioq size = %d, videoq size = %d", is->audioq.size, is->videoq.size);
 		if(is->audioq.size > MAX_AUDIOQ_SIZE || is->videoq.size > MAX_VIDEOQ_SIZE) {
		   usleep(50000); //50 ms
		   continue;
		}
		if(av_read_frame(is->pFormatCtx, &packet)<0){
			if(url_ferror(&pFormatCtx->pb) == 0) {
				usleep(50000);
				continue;
			}else{
				is->quit = 1;
				break;
			}
		}
  		if(packet.stream_index==is->videoStream) {
			packet_queue_put(&is->videoq, &packet);
			/**************************/
			/*
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
    		if(frameFinished) {
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
				AndroidBitmap_unlockPixels(env, bitmap);
                i = 1;
				//TODO callback refresh
				if(mClass == NULL || mObject == NULL || refresh == NULL) {
					int res = registerCallBack(env);
					LOGI(10,"registerCallBack == %d", res);	
					if(res != 0) {
						return decode_next_frame;
					}
				}
				(*env)->CallVoidMethod(env, mObject, refresh);
    	    }*/
        } else if(packet.stream_index==is->audioStream) {
			packet_queue_put(&is->audioq, &packet);
		} else {
			av_free_packet(&packet);
		}
    }
	LOGI(10,"exit\n");
	return ((void *)0);
}

void *video_thread(void *arg) {
  JNIEnv *env;
  if((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != JNI_OK) {
	  return ((void *)-1);
  }
  VideoState *is = (VideoState*)arg;
  AVPacket pkt1, *packet = &pkt1;
  int len1, frameFinished;
  AVFrame *pFrame;
  double pts;
  int numBytes;
  pFrame=avcodec_alloc_frame();
  pFrameRGB=avcodec_alloc_frame();
  if(debug) LOGI(10,"Video size is [%d x %d]", pCodecCtx->width, pCodecCtx->height);
  numBytes=avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
  buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
  avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);

  for(;;) {
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
	  if(debug) LOGI(10,"video_thread get packet exit");
      break;
    }
	if(debug) LOGI(10,"video_thread get packet ====================");
	usleep(1000000);
    pts = 0;
    global_video_pkt_pts = packet->pts;//is->video_st->codec
    len1 = avcodec_decode_video2(pCodecCtx,
				pFrame,
				&frameFinished,
				&packet);
	if(debug) LOGI(10,"video_thread get packet ====================%d", len1 );
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
	  usleep(10000);
    }
    av_free_packet(packet);
  }
  av_free(pFrame);
  LOGI(10, "#### ------------ video_thread exit");
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


void packet_queue_init(PacketQueue *q) {
	memset(q, 0, sizeof(PacketQueue));
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
    av_free(pFrame);

    /*close the video codec*/
    avcodec_close(pCodecCtx);
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
			(*env)->DeleteLocalRef(env, mClass);
			return -1;
		}
		LOGI(10,"register local object OK.");
	}
	if(refresh == NULL) {
		refresh = (*env)->GetMethodID(env, mClass, "callBackRefresh","()V");
		if(refresh == NULL) {
			(*env)->DeleteLocalRef(env, mClass);
			(*env)->DeleteLocalRef(env, mObject);
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
	pkt1->pkt = *pkt;
	pkt1->next = NULL;
	//SDL_LockMutex(q->mutex);
	pthread_mutex_lock(&mut);

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;

	pthread_cond_broadcast(&cond);//pthread_cond_signal
	pthread_mutex_unlock(&mut);
	//SDL_CondSignal(q->cond);
	//SDL_UnlockMutex(q->mutex);
	return 0;
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

