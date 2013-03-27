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
			if(debug) LOGE(10,"packet_queue_get over.");
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

double get_video_clock(VideoState *is) {
	double delta;
	delta = (av_gettime() - is->video_current_pts_time) / 1000000.0;
	return is->video_current_pts + delta;
}

double get_audio_clock(VideoState *is) {
	double pts;
	int hw_buf_size, bytes_per_sec, n;

	pts = is->audio_clock;
	hw_buf_size = is->audio_buf_size - is->audio_buf_index;
	bytes_per_sec = 0;
	n = is->audio_st->codec->channels * 2;
	if (is->audio_st) {
		bytes_per_sec = is->audio_st->codec->sample_rate * n;
	}
	if (bytes_per_sec) {
		pts -= (double)hw_buf_size / bytes_per_sec;
	}
	return pts;
}

static double get_external_clock(VideoState *is) {
	 double time = av_gettime() / 1000000.0;
	 return is->external_clock_drift + time - (time - is->external_clock_time / 1000000.0) * (1.0 - is->external_clock_speed);
}

double get_master_clock(VideoState *is) {
	if(is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
		return get_video_clock(is);
	} else if(is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
		return get_audio_clock(is);
	} else {
		return get_external_clock(is);
	}
}

int audio_decode_frame(VideoState*is, int16_t *audio_buf, int buf_size, double *pts_ptr) {
  int len1, data_size, n;
  AVPacket *pkt = &is->audio_pkt;
  double pts;  
  for (;;) {
    while (is->audio_pkt_size > 0) {
      data_size = AVCODEC_MAX_AUDIO_FRAME_SIZE*10;
      len1 = avcodec_decode_audio3(is->audio_st->codec,
                                   audio_buf, 
                                  &data_size,
                                   pkt);
      if (len1 < 0) {
		is->audio_pkt_size = 0;
		break;
      }
      is->audio_pkt_data += len1;
      is->audio_pkt_size -= len1;
      if (data_size <= 0) {
		continue;
      }
      pts = is->audio_clock;
      *pts_ptr = pts;
      n = 2 * is->audio_st->codec->channels;
      is->audio_clock += (double)data_size / (double)(n*is->audio_st->codec->sample_rate);
	  if(debug) {
		static double last_clock;
		LOGE(10, "audio: delay=%0.3f clock=%0.3f pts=%0.3f\n",
                        is->audio_clock - last_clock,
                        is->audio_clock, pts);
		last_clock = is->audio_clock;
	  }
      return data_size;
    }
    if (pkt->data) {
      av_free_packet(pkt);
    }
    if (is->quit) {
      return -1;
    }
    if (packet_queue_get(&is->audioq, pkt, 1) < 0) {
      return -1;
    }
    is->audio_pkt_data = pkt->data;
    is->audio_pkt_size = pkt->size;
    if (pkt->pts != AV_NOPTS_VALUE) {
      is->audio_clock = av_q2d(is->audio_st->time_base)*pkt->pts;
    }
  }
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
	dump_format(pFormatCtx, 0, is->filename, 0);
	for (i=0; i<pFormatCtx->nb_streams; i++) {
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO && videoStream<0) {
			videoStream = i;
		}
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO && audioStream<0) {
			audioStream = i;
		}
    }
	if(videoStream>=0) {
		pCodecCtx=pFormatCtx->streams[videoStream]->codec;
		gVideoCodecCtx = pCodecCtx;
		is->img_convert_ctx = sws_getContext(pCodecCtx->width,
				   pCodecCtx->height,
				   pCodecCtx->pix_fmt,
				   pCodecCtx->width,
				   pCodecCtx->height,
				   PIX_FMT_RGB24,
				   SWS_BICUBIC,
				   NULL, NULL, NULL);
		pFrameRGB=avcodec_alloc_frame();		   
		int numBytes;
		numBytes=avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
		buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
		avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
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
	if(audioStream>=0) {
		is->audioStream = audioStream;
		aCodecCtx=pFormatCtx->streams[audioStream]->codec; 
		is->audio_st = pFormatCtx->streams[audioStream];
		aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
		 
		if(!aCodec) {
			if(debug)  LOGE(1,"Unsupported audio codec!");
			lVideoRes[0] = unsurpport_codec;
			(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
			return videoInfo;
		}
		if(avcodec_open(aCodecCtx, aCodec)<0){
			if(debug)  LOGE(1,"Unable to open audio codec");
			lVideoRes[0] = open_codec_fail;
			(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
			return videoInfo;
		}


		LOGE(10,"### get audio info : bite_rate= %d, sample_rate = %d, channels = %d, sample_fmt = %d, frame_size = %d",pFormatCtx->streams[audioStream]->codec->bit_rate, pFormatCtx->streams[audioStream]->codec->sample_rate, pFormatCtx->streams[audioStream]->codec->channels, aCodecCtx->sample_fmt,aCodecCtx->frame_size);

		/* put sample parameters */    
		aCodecCtx->bit_rate = pFormatCtx->streams[audioStream]->codec->bit_rate;       
		frequency = pFormatCtx->streams[audioStream]->codec->sample_rate;       
		aCodecCtx->channels = pFormatCtx->streams[audioStream]->codec->channels;

		is->video_current_pts_time = av_gettime();
		is->audio_buf = (int16_t *)av_malloc(out_size); 
		is->audio_buf_size = 0;
		is->audio_buf_index = 0;
		memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		packet_queue_init(&is->audioq);
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
	pthread_t audio;
	pthread_create(&audio, NULL, &audio_thread, is);
	if(debug)  LOGE(1,"pthread_create  audio_thread");
	return is->video_tid;
}

int Java_com_sky_drovik_player_ffmpeg_JniUtils_display(JNIEnv * env, jobject this, jstring bitmap)
{
	AndroidBitmapInfo  info;
	void*              pixels;
	int ret;
     if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE(1,"AndroidBitmap_getInfo() failed ! error=%d", ret);
        return bitmap_getinfo_error;
	}
	VideoPicture *vp;
	double actual_delay, delay, sync_threshold, ref_clock, diff;
	while(!is->quit && is->video_st) {
		if(is->pictq_size == 0) {
			usleep(1000);
			//LOGI(1,"no image, wait.");
		} else {
			if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
				LOGE(1,"AndroidBitmap_lockPixels() failed ! error=%d", ret);
				continue;
			}
			// 取出图像
			vp = &is->pictq[is->pictq_rindex];
			delay = vp->pts - is->frame_last_pts;
			if (delay <= 0 || delay >= 1.0) {
				delay = is->frame_last_delay;
			}
			is->frame_last_delay = delay;
		    is->frame_last_pts = vp->pts;
		    ref_clock = get_audio_clock(is);
		    diff = vp->pts - ref_clock;
		    sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
			if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
				if (diff <= -sync_threshold) {
				  delay = 0;
				} else if (diff >= sync_threshold) {
				  delay = 2 * delay;
				}
		    }
		    is->frame_timer += delay;
            actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
		    if (actual_delay < 0.010) {
			  actual_delay = 0.010;
		    }
			LOGE(10, "### refresh delay =  %d",(int)(actual_delay * 1000000 + 500));
			usleep((int)(actual_delay * 1000000 + 500));
			fill_bitmap(&info, pixels, vp->pict);
			if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
				is->pictq_rindex = 0;
			}
			pthread_mutex_lock(&is->pictq_mutex);
			is->pictq_size--;
			pthread_cond_signal(&is->pictq_cond);
			pthread_mutex_unlock(&is->pictq_mutex);
			AndroidBitmap_unlockPixels(env, bitmap);
			if(mClass == NULL || mObject == NULL || refresh == NULL) {
				registerCallBackRes = registerCallBack(env);
				LOGI(10,"registerCallBack == %d", registerCallBackRes);	
				if(registerCallBackRes != 0) {
					is->quit = 0;				
					continue;
				}
			}
			(*env)->CallVoidMethod(env, mObject, refresh, MSG_REFRESH);
		}
	}
	if(registerCallBackRes == 0) {
		(*env)->CallVoidMethod(env, mObject, refresh, MSG_EXIT);
	}
	return 0;
}


int queue_picture(VideoState *is, AVFrame *pFrame, double pts) {
  VideoPicture *vp;
  AVPicture pict;
  int dst_pix_fmt;
	
  //SDL_LockMutex(is->pictq_mutex);
  pthread_mutex_lock(&is->pictq_mutex);
  while(is->pictq_size>=VIDEO_PICTURE_QUEUE_SIZE &&
	!is->quit) {
	pthread_cond_wait(&is->pictq_cond, &is->pictq_mutex);
  }
  //SDL_UnlockMutex(is->pictq_mutex);
  pthread_mutex_unlock(&is->pictq_mutex);
  if(is->quit) {
	if(debug) LOGE(10,"### queue_picture exit");
    return -1;
  }
    vp = &is->pictq[is->pictq_windex];
	dst_pix_fmt = PIX_FMT_RGB24;
    sws_scale(is->img_convert_ctx,
	      pFrame->data,
	      pFrame->linesize, 0,
	      is->video_st->codec->height,
	      pFrameRGB->data,
	      pFrameRGB->linesize);
    vp->pts = pts;	
	vp->pict = pFrameRGB;
    if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
      is->pictq_windex = 0;
    }
    //SDL_LockMutex(is->pictq_mutex);
	pthread_mutex_lock(&is->pictq_mutex);
    is->pictq_size ++;
    //SDL_UnlockMutex(is->pictq_mutex);
	pthread_mutex_unlock(&is->pictq_mutex);
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

static int get_master_sync_type(VideoState *is) {
     if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
         if (is->video_st)
             return AV_SYNC_VIDEO_MASTER;
         else
             return AV_SYNC_AUDIO_MASTER;
     } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
         if (is->audio_st)
             return AV_SYNC_AUDIO_MASTER;
         else
             return AV_SYNC_EXTERNAL_CLOCK;
     } else {
         return AV_SYNC_EXTERNAL_CLOCK;
     }
}

int synchronize_audio(VideoState *is, short *samples, int samples_size, double  pts) {

int n;
double ref_clock;
n = 2 * is->audio_st->codec->channels;
if(is->av_sync_type != AV_SYNC_AUDIO_MASTER) {
	double diff, avg_diff;
	int wanted_size, min_size, max_size, nb_samples;
	ref_clock = get_master_clock(is);
	diff = get_audio_clock(is) - ref_clock;
	if(diff < AV_NOSYNC_THRESHOLD) {
		// accumulate the diffs
		is->audio_diff_cum = diff + is->audio_diff_avg_coef* is->audio_diff_cum;
		if(is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
			is->audio_diff_avg_count++;
		} else {
			avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);
		}
	} else {
		is->audio_diff_avg_count = 0;
		is->audio_diff_cum = 0;
	}
}
return samples_size;
}
 
void *decode_thread(void *arg) {
	VideoState *is = (VideoState*)arg;
    AVPacket packet;
    while(1) {
		if(is->quit) {
		  break;
		}
 		if(is->audioq.size > MAX_AUDIOQ_SIZE || is->videoq.size > MAX_VIDEOQ_SIZE) {// 
		   usleep(10000); //10 ms
		   continue;
		}
		if(av_read_frame(is->pFormatCtx, &packet)<0){
			/*if(url_ferror(pFormatCtx->pb) == 0) {
				usleep(100000);
				LOGE(10,"------1111");
				continue;
			} else {
				LOGE(10,"------2222");
				break;
			}*/
			usleep(100000);
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
  pFrameRGB=avcodec_alloc_frame();
  int ret;
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

void *audio_thread(void *arg) {
	JNIEnv* env; 
	if((*g_jvm)->AttachCurrentThread(g_jvm, (void**)&env, NULL) != JNI_OK) 
	{
		LOGE("%s: AttachCurrentThread() failed", __FUNCTION__);
		return ((void *)-1);;
	}
	VideoState *is = (VideoState*)arg;
	int remain, audio_size;//remain 解码出的音频缓冲区剩余的数据长度
	double pts;
	int pcmBufferLen;//音频数据写入的缓冲区的长度
	jclass audio_track_cls = (*env)->FindClass(env,"android/media/AudioTrack");
	jmethodID min_buff_size_id = (*env)->GetStaticMethodID(
										 env,
										 audio_track_cls,
										"getMinBufferSize",
										"(III)I");
	int buffer_size = (*env)->CallStaticIntMethod(env,audio_track_cls,min_buff_size_id, 		frequency,
			    12,			/*CHANNEL_IN_STEREO*/
				2);         /*ENCODING_PCM_16BIT*/
	LOGI(10,"buffer_size=%i",buffer_size);	
	pcmBufferLen = (AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2;
	jbyteArray buffer = (*env)->NewByteArray(env,pcmBufferLen);
	jmethodID constructor_id = (*env)->GetMethodID(env,audio_track_cls, "<init>",
			"(IIIIII)V");
	jobject audio_track = (*env)->NewObject(env,audio_track_cls,
			constructor_id,
			3, 			  /*AudioManager.STREAM_MUSIC*/
			frequency,        /*sampleRateInHz*/
			12,			  /*CHANNEL_IN_STEREO*/
			2,			  /*ENCODING_PCM_16BIT*/
			buffer_size*10,  /*bufferSizeInBytes*/
			1			  /*AudioTrack.MODE_STREAM*/
	);	
	//setvolume
	jmethodID setStereoVolume = (*env)->GetMethodID(env,audio_track_cls,"setStereoVolume","(FF)I");
	(*env)->CallIntMethod(env,audio_track,setStereoVolume,1.0,1.0);
	//play
    jmethodID method_play = (*env)->GetMethodID(env,audio_track_cls, "play",
			"()V");
    (*env)->CallVoidMethod(env,audio_track, method_play);
    //write
    jmethodID method_write = (*env)->GetMethodID(env,audio_track_cls,"write","([BII)I");
	//release
	jmethodID method_release = (*env)->GetMethodID(env,audio_track_cls,"release","()V");
	//double ref_clock, sync_threshold, diff;
	while(!is->quit) {
		if(is->audio_buf_index >= is->audio_buf_size) {//audio_buf中的数据已经转移完毕了
		    audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf), &pts);
		    if (audio_size < 0) {
				is->audio_buf_size = (AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2;
				memset(is->audio_buf, 0, is->audio_buf_size);
		    }else {
				audio_size = synchronize_audio(is, (int16_t *)is->audio_buf,audio_size,pts);
				is->audio_buf_size = audio_size;
		    } 
		    //每次解码出音频之后，就把音频的索引audio_buf_index值0 从头开始索引
		    is->audio_buf_index = 0;	
		}
	
		//剩余的数据长度超过音频数据写入的缓冲区的长度
		remain = is->audio_buf_size - is->audio_buf_index;
		if(remain > pcmBufferLen) {
		  remain = pcmBufferLen;
		}
		(*env)->SetByteArrayRegion(env,buffer, 0, remain, (jbyte *)is->audio_buf);

		(*env)->CallIntMethod(env,audio_track,method_write,buffer,0,remain);
		//LOGI(10,"ttt audio_buf_index = %d, audio_buf_size = %d,remain = %d, clock = %d", is->audio_buf_index,is->audio_buf_size, remain, is->audio_clock);

		//(*env)->CallIntMethod(env,audio_track,method_write,buffer,0,out_size);
		//LOGI(10,"ttt audio_buf_index = %d, audio_buf_size = %d,len1 = %d, len = %d", is->audio_buf_index,is->audio_buf_size, len1, len);

		//len -= len1;
		is->audio_buf_index += remain;	
	}
	(*env)->CallVoidMethod(env,audio_track, method_release);
	if(debug) LOGI(1, "### decode audio thread exit.");
	((void *)0);
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
	dump_format(pFormatCtx, 0, is->filename, 0);
	for (i=0; i<pFormatCtx->nb_streams; i++) {
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO && videoStream<0) {
			videoStream = i;
		}
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO && audioStream<0) {
			audioStream = i;
		}
    }
	is->av_sync_type = DEFAULT_AV_SYNC_TYPE;
	if(videoStream>=0) {
		pCodecCtx=pFormatCtx->streams[videoStream]->codec;
		gVideoCodecCtx = pCodecCtx;
		is->img_convert_ctx = sws_getContext(pCodecCtx->width,
				   pCodecCtx->height,
				   pCodecCtx->pix_fmt,
				   pCodecCtx->width,
				   pCodecCtx->height,
				   PIX_FMT_RGB24,
				   SWS_BICUBIC,
				   NULL, NULL, NULL);
		pFrameRGB=avcodec_alloc_frame();		   
		int numBytes;
		numBytes=avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
		buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
		avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
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
				is->video_current_pts_time = av_gettime();
				is->frame_timer = (double)av_gettime() / 1000000.0;
				is->frame_last_delay = 40e-3;
				packet_queue_init(&is->videoq);
				is->quit = 0; //1 exit
				pCodecCtx->get_buffer = our_get_buffer;
				pCodecCtx->release_buffer = our_release_buffer;
			}
		}
    }
	if(audioStream>=0) {
		is->audioStream = audioStream;
		aCodecCtx=pFormatCtx->streams[audioStream]->codec; 
		is->audio_st = pFormatCtx->streams[audioStream];
		aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
		 
		if(!aCodec) {
			if(debug)  LOGE(1,"Unsupported audio codec!");
			lVideoRes[0] = unsurpport_codec;
			(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
			return videoInfo;
		}
		if(avcodec_open(aCodecCtx, aCodec)<0){
			if(debug)  LOGE(1,"Unable to open audio codec");
			lVideoRes[0] = open_codec_fail;
			(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
			return videoInfo;
		}


		LOGE(10,"### get audio info : bite_rate= %d, sample_rate = %d, channels = %d, sample_fmt = %d, frame_size = %d",pFormatCtx->streams[audioStream]->codec->bit_rate, pFormatCtx->streams[audioStream]->codec->sample_rate, pFormatCtx->streams[audioStream]->codec->channels, aCodecCtx->sample_fmt,aCodecCtx->frame_size);

		/* put sample parameters */    
		aCodecCtx->bit_rate = pFormatCtx->streams[audioStream]->codec->bit_rate;       
		frequency = pFormatCtx->streams[audioStream]->codec->sample_rate;       
		aCodecCtx->channels = pFormatCtx->streams[audioStream]->codec->channels;

		is->video_current_pts_time = av_gettime();
		is->audio_buf = (int16_t *)av_malloc(out_size); 
		is->audio_buf_size = 0;
		is->audio_buf_index = 0;
		memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		packet_queue_init(&is->audioq);
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
	pthread_t audio;
	pthread_create(&audio, NULL, &audio_thread, is);
	if(debug)  LOGE(1,"pthread_create  audio_thread");
	return is->video_tid;
}

int Java_com_sky_drovik_player_ffmpeg_JniUtils_display(JNIEnv * env, jobject this, jstring bitmap)
{
	AndroidBitmapInfo  info;
	void*              pixels;
	int ret;
     if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE(1,"AndroidBitmap_getInfo() failed ! error=%d", ret);
        return bitmap_getinfo_error;
	}
	VideoPicture *vp;
	double actual_delay, delay, sync_threshold, ref_clock, diff;
	while(!is->quit && is->video_st) {
		if(is->pictq_size == 0) {
			usleep(50000);
			//LOGI(1,"no image, wait.");
		} else {
			if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
				LOGE(1,"AndroidBitmap_lockPixels() failed ! error=%d", ret);
				continue;
			}
			// 取出图像
			vp = &is->pictq[is->pictq_rindex];
			is->video_current_pts = vp->pts;
			is->video_current_pts_time = av_gettime();
			delay = vp->pts - is->frame_last_pts;
			if (delay <= 0 || delay >= 1.0) {
				delay = is->frame_last_delay;
			}
			is->frame_last_delay = delay;
		    is->frame_last_pts = vp->pts;
		    ref_clock = get_audio_clock(is);
		    diff = vp->pts - ref_clock;
		    sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
			if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
				if (diff <= -sync_threshold) {
				  delay = 0;
				} else if (diff >= sync_threshold) {
				  delay = 2 * delay;
				}
		    }
		    is->frame_timer += delay;
            actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
		    if (actual_delay < 0.010) {
			  actual_delay = 0.010;
		    }
			LOGE(10, "### refresh delay =  %d",(int)(actual_delay * 1000 + 0.5));
			usleep(10000*(int)(actual_delay * 1000 + 0.5));
			fill_bitmap(&info, pixels, vp->pict);
			if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
				is->pictq_rindex = 0;
			}
			pthread_mutex_lock(&is->pictq_mutex);
			is->pictq_size--;
			pthread_cond_signal(&is->pictq_cond);
			pthread_mutex_unlock(&is->pictq_mutex);
			if(is->av_sync_type != AV_SYNC_VIDEO_MASTER) {
			ref_clock = get_master_clock(is);
			diff = vp->pts - ref_clock;
			sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay :	AV_SYNC_THRESHOLD;
			if(fabs(diff) < AV_NOSYNC_THRESHOLD) {
				if(diff <= -sync_threshold) {
					delay = 0;
				} else if(diff >= sync_threshold) {
					delay = 2 * delay;
				}
			}
		}
			AndroidBitmap_unlockPixels(env, bitmap);
			if(mClass == NULL || mObject == NULL || refresh == NULL) {
				registerCallBackRes = registerCallBack(env);
				LOGI(10,"registerCallBack == %d", registerCallBackRes);	
				if(registerCallBackRes != 0) {
					is->quit = 0;				
					continue;
				}
			}
			(*env)->CallVoidMethod(env, mObject, refresh, MSG_REFRESH);
		}
	}
	if(registerCallBackRes == 0) {
		(*env)->CallVoidMethod(env, mObject, refresh, MSG_EXIT);
	}
	return 0;
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
		refresh = (*env)->GetMethodID(env, mClass, "callBackRefresh","(I)V");
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

static void packet_queue_flush(PacketQueue *q)
 {
     AVPacketList *pkt, *pkt1;
 
     pthread_mutex_lock(&q->mutex);
     for (pkt = q->first_pkt; pkt != NULL; pkt = pkt1) {
         pkt1 = pkt->next;
         av_free_packet(&pkt->pkt);
         av_freep(&pkt);
     }
     q->last_pkt = NULL;
     q->first_pkt = NULL;
     q->nb_packets = 0;
     q->size = 0;
     pthread_mutex_unlock(&q->mutex);
 }
 static void packet_queue_destroy(PacketQueue *q)
 {
     packet_queue_flush(q);
	 pthread_mutex_destroy(&q->mutex);
	 pthread_cond_destroy(&q->cond);
 }
