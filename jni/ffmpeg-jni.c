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


static void checkGlError(const char* op)
{
	GLint error;
	for (error = glGetError(); error; error = glGetError())
		LOGI(1,"after %s() glError (0x%x)\n", op, error);
}

jstring bitmap;
GLuint loadShader(GLenum shaderType, const char* pSource)
{
	GLuint shader = glCreateShader(shaderType);
	checkGlError("glCreateShader");
	if (shader)
    {
		glShaderSource(shader, 1, &pSource, NULL);
		glCompileShader(shader);
        GLint compiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled)
        {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen)
            {
                char* buf = (char*) av_malloc(infoLen);
                if (buf)
                {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    av_free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
	}	
	return shader;
}

GLuint createProgram(const char * pVertexSource, const char * pFragmentSource)
{
	GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader)
    {
        return 0;
    }
	GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader)
    {
        return 0;
    }
	GLuint program = glCreateProgram();
    if (program)
    {
        glAttachShader(program, vertexShader);
		checkGlError("glAttachShader");
        glAttachShader(program, pixelShader);
		checkGlError("glAttachShader");
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE)
        {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength)
            {	
                char* buf = (char*) av_malloc(bufLength);
                if (buf)
                {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    av_free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

void SetupTextures()
{
	glDeleteTextures(3, _textureIds);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST); 
	glDisable(GL_BLEND);
	glDisable(GL_DITHER);
	glDisable(GL_STENCIL_TEST);
    glGenTextures(3, _textureIds); //Generate  the Y, U and V texture
    GLuint currentTextureId = _textureIds[0]; // Y
    glBindTexture(GL_TEXTURE_2D, currentTextureId);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, imageWidth, imageHeight, 0,GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);     

    currentTextureId = _textureIds[1]; // U
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, currentTextureId);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, imageWidth / 2, imageHeight / 2, 0,
            GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

    currentTextureId = _textureIds[2]; // V
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, currentTextureId);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, imageWidth / 2, imageHeight / 2, 0,
            GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
}

void UpdateTextures()
{
	flushComplete = 0;
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 
    // Y
    GLuint currentTextureId = _textureIds[0];
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, currentTextureId);
	//glBegin(GL_TRAINGLE_STRIP);
    glTexSubImage2D(GL_TEXTURE_2D, 0, w_padding, h_padding, imageWidth, imageHeight, GL_LUMINANCE,GL_UNSIGNED_BYTE, yuv_buf);

    // U
    currentTextureId = _textureIds[1];
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, currentTextureId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, w_padding/2, h_padding/2, imageWidth / 2, imageHeight / 2,
            GL_LUMINANCE, GL_UNSIGNED_BYTE, yuv_buf + imageWidth * imageHeight);
    // V
    currentTextureId = _textureIds[2];
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, currentTextureId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, w_padding/2, h_padding/2, imageWidth / 2, imageHeight / 2,
            GL_LUMINANCE, GL_UNSIGNED_BYTE, yuv_buf + (imageHeight * imageWidth * 5) / 4);
	flushComplete = 1;
}
 
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
		bytes_per_sec = frequency * n;
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
	} else if(is->av_sync_type == AV_SYNC_AUDIO_MASTER && is->audioStream>0) {
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
      data_size = AVCODEC_MAX_AUDIO_FRAME_SIZE*3/2;
      len1 = avcodec_decode_audio3(aCodecCtx,
                                   audio_buf, 
                                  &data_size,
                                   pkt);
      if (len1 < 0) {
		is->audio_pkt_size = 0;
		break;
      }
      is->audio_pkt_data += len1;
      is->audio_pkt_size -= len1;
      if (data_size < 0) {
		continue;
      }
      pts = is->audio_clock;
      *pts_ptr = pts;
      n = 2 * aCodecCtx->channels;
      is->audio_clock += (double)data_size / (double)(n*aCodecCtx->sample_rate);
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

int queue_picture(VideoState *is, AVFrame *pFrame, double pts) {
  VideoPicture *vp;
  AVPicture pict;
  int dst_pix_fmt;
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
	if(debug) LOGE(10,"### queue_picture exit");
    return -1;
  }
    vp = &is->pictq[is->pictq_windex];
    vp->pts = pts;	
	vp->pict = pFrame;
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
  frame_delay = av_q2d(pCodecCtx->time_base);
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
n = 2 * aCodecCtx->channels;
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
 
void *dispatch_data_thread(void *arg) {
	JNIEnv *env;
	if((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != JNI_OK) {
		LOGE(1, "### start decode thead error");
		return;
	}
	VideoState *is = (VideoState*)arg;
    AVPacket packet;
    while(1) {
 		if(is->audioq.size > MAX_AUDIOQ_SIZE || is->videoq.size > MAX_VIDEOQ_SIZE) {// 
			if(is->quit) {
			  break;
			}
		   usleep(5000); //5 ms
		   continue;
		}
		if(av_read_frame(pFormatCtx, &packet)<0){
			LOGE(10,"av_read_frame over !!! ");
			is->quit = 2;
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
		usleep(10000);
	}
	LOGI(10,"dispatch_data_thread exit\n");
	if((*g_jvm)->DetachCurrentThread(g_jvm) != JNI_OK) {
		LOGE(1,"### detach decode thread error");
	}
	pthread_exit(0);
	if(debug) {
		LOGE(1, "### dispatch_data_thread exit");
	}
	return ((void *)0);
}

void *video_thread(void *arg) {
  JNIEnv *env;
  if((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != JNI_OK) {
       LOGE(1, "### start video thead error");
	   return;
  }
  VideoState *is = (VideoState*)arg;
  AVPacket pkt1, *packet = &pkt1;
  int len1, frameFinished;
  AVFrame *pFrame;
  double pts;
  int numBytes;
  pFrame=avcodec_alloc_frame();
  int ret;
  for(;;) {
	if(is->quit == 1 || is->quit == 2) {
		break;
	}
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
	  if(debug) LOGI(10,"video_thread get packet exit");
      break;
    }
    pts = 0;
    global_video_pkt_pts = packet->pts;
    len1 = avcodec_decode_video2(pCodecCtx,
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
    //pts *= av_q2d(is->video_st->time_base);
	pts *= av_q2d(pCodecCtx->time_base);
    if (frameFinished) {
       pts = synchronize_video(is, pFrame, pts);
       if (queue_picture(is, pFrame, pts) < 0) {
			break;
       }
    }
    av_free_packet(packet);
  }
  av_free(pFrame);
  if((*g_jvm)->DetachCurrentThread(g_jvm) != JNI_OK) {
	LOGE(1,"### detach video thread error");
  }
  pthread_exit(0);
  if(debug) {
		LOGI(1,"### video_thread exit");
  }
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
	double pts;
	while(!is->quit) {
		if(is->audio_buf_index >= is->audio_buf_size) {//audio_buf中的数据已经转移完毕了
		    audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf), &pts);
			//LOGI(1, "----> audio_decode_frame --- pts = %d", &pts);
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
		is->audio_buf_index += remain;	
	}
	(*env)->CallVoidMethod(env,audio_track, method_release);
	if(debug) LOGI(1, "### decode audio thread exit.");
	if((*g_jvm)->DetachCurrentThread(g_jvm) != JNI_OK) {
		LOGE(1,"### detach audio thread error");
	} 
	pthread_exit(0);
	((void *)0);
}

void packet_queue_init(PacketQueue *q) {
	memset(q, 0, sizeof(PacketQueue));
	pthread_mutex_init(&q->mutex, NULL);
	pthread_mutex_init(&q->cond, NULL);
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
    setUpFlag = 0;
	(*env)->GetJavaVM(env, &g_jvm);
	g_obj = (*env)->NewGlobalRef(env,g_obj);
	is = av_mallocz(sizeof(VideoState));
	av_register_all();
    char * gFileName = (char *)(*env)->GetStringUTFChars(env, name, NULL);	
	if(av_open_input_file(&pFormatCtx,gFileName , NULL, 0, NULL)!=0) {
		if(debug) LOGI(10,"Couldn't open file");
		lVideoRes[0] = open_file_fail;
		(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
		return videoInfo;
    }
	//is->pFormatCtx = pFormatCtx;    
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
				if(debug)  LOGI(1,"### is->video_current_pts_time = %d",is->video_current_pts_time);
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
		/* put sample parameters */          
		frequency = aCodecCtx->sample_rate;       
		//is->video_current_pts_time = av_gettime();
		is->audio_buf = (int16_t *)av_malloc(out_size); 
		is->audio_buf_size = 0;
		is->audio_buf_index = 0;
		memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		packet_queue_init(&is->audioq);
		LOGE(10,"### get audio info : bite_rate= %d, sample_rate = %d, channels = %d, sample_fmt = %d, frame_size = %d",aCodecCtx->bit_rate, aCodecCtx->sample_rate, aCodecCtx->channels, aCodecCtx->sample_fmt,aCodecCtx->frame_size);
	}
	lVideoRes[0] = pCodecCtx->width;
    lVideoRes[1] = pCodecCtx->height;
    lVideoRes[2] = pCodecCtx->time_base.den;
    lVideoRes[3] = pCodecCtx->time_base.num;
    LOGI(1, "video info time base = %d, time_base.den = %d,time_base.num  = %d, bit_rate = %d",is->video_st->time_base,is->video_st->time_base.num,is->video_st->time_base.den, pCodecCtx->bit_rate);//, pCodecCtx->duration
	imageWidth = pCodecCtx->width;
	imageHeight = pCodecCtx->height;
	pthread_cond_init(&s_vsync_cond, NULL);
	pthread_mutex_init(&s_vsync_mutex, NULL);
	yuv_buf = (unsigned char *) av_malloc(imageWidth * imageHeight * 3/2);
	(*env)->SetIntArrayRegion(env, videoInfo, 0, 4, lVideoRes);
	pthread_mutex_init(&is->pictq_mutex, NULL);
	pthread_mutex_init(&is->pictq_cond, NULL);
	return  videoInfo;
}

int Java_com_sky_drovik_player_ffmpeg_JniUtils_decodeMedia(JNIEnv * env, jobject this)
{
	int ret;
	pthread_t decode;
	is->decode_tid =  pthread_create(&decode, NULL, &dispatch_data_thread, is);
	if(debug)  LOGE(1,"pthread_create  dispatch_data_thread");
	if(debug) LOGI(1,"start thread id = %d", is->decode_tid);
	pthread_t video;
	is->video_tid =  pthread_create(&video, NULL, &video_thread, is);
	if(debug)  LOGE(1,"pthread_create  video_thread");
	if(is->audioStream >0){
		pthread_t audio;
		pthread_create(&audio, NULL, &audio_thread, is);
		if(debug)  LOGE(1,"pthread_create  audio_thread");
	}
	return 0;
}

void *show_image_thread(void *arg) {
	JNIEnv *env;
	if((*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL) != JNI_OK) {
		LOGE(1, "### start decode thead error");
		return;
	}
	VideoPicture *vp;
	double actual_delay, delay, sync_threshold, ref_clock, diff;
	while(!is->quit && is->video_st) {
		if(is->pictq_size == 0) {
			if(is->quit == 2) {
				is->quit = 1;
				break;
			}
			usleep(5000);
			//LOGI(1,"no image, wait.");
		} else {
			// 取出图像
			vp = &is->pictq[is->pictq_rindex];
			/*
			is->video_current_pts = vp->pts;
			is->video_current_pts_time = av_gettime();
			delay = vp->pts - is->frame_last_pts;
			//LOGE(1, "is->video_current_pts = %d, delay = %d",is->video_current_pts,delay);
			if (delay <= 0 || delay >= 1.0) {
				delay = is->frame_last_delay;
			}
			is->frame_last_delay = delay;
			is->frame_last_pts = vp->pts;
			is->frame_timer += delay;
			actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
			if(is->av_sync_type != AV_SYNC_VIDEO_MASTER) {
				ref_clock = get_master_clock(is);
				LOGE(1, "ref_clock = %d " , ref_clock);
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
			if (actual_delay < 0.010) {
			  actual_delay = 0.010;
			}*/
			//LOGE(10, "### refresh delay =  %d",(int)(actual_delay * 1000 + 0.5));
			//usleep(10000*(int)(actual_delay * 1000 + 0.5));
			for (int i = 0, off_set = 0; i < 3; i++) {
				int nShift = (i == 0) ? 0 : 1;
				uint8_t *pYUVData = (uint8_t *)vp->pict->data[i];
				for (int j = 0; j < (imageHeight >> nShift); j++) {
					memcpy(yuv_buf + off_set, pYUVData, (imageWidth>> nShift));
					pYUVData += vp->pict->linesize[i];
					off_set += (imageWidth >> nShift);
				}
			}	
			if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
				is->pictq_rindex = 0;
			}
			pthread_mutex_lock(&is->pictq_mutex);
			is->pictq_size--;
			pthread_mutex_unlock(&is->pictq_mutex);
			pthread_cond_signal(&s_vsync_cond);
			//if(mClass == NULL || mObject == NULL || refresh == NULL) {
			//	registerCallBackRes = registerCallBack(env);
			//	LOGI(10,"registerCallBack == %d", registerCallBackRes);	
				//if(registerCallBackRes != 0) {
				//	is->quit = 0;				
				///	continue;
				//}
			//}/**/
			//(*env)->CallVoidMethod(env, mObject, refresh, MSG_REFRESH);
		}
	}
	//if(registerCallBackRes == 0) {
	//	(*env)->CallVoidMethod(env, mObject, refresh, MSG_EXIT);
	//}	
	(*g_jvm)->DetachCurrentThread(g_jvm);
	exit();
}

int Java_com_sky_drovik_player_ffmpeg_JniUtils_display(JNIEnv * env, jobject this){
	pthread_t show;
	pthread_create(&show, NULL, &show_image_thread, NULL);
	return -1;
}


JNIEXPORT jintArray JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_getVideoResolution(JNIEnv *pEnv, jobject pObj) {
    jintArray lRes;
    lRes = (*pEnv)->NewIntArray(pEnv, 4);
    if (lRes == NULL) {
        LOGI(1, "cannot allocate memory for video size");
        return NULL;
    }
    jint lVideoRes[4];
    (*pEnv)->SetIntArrayRegion(pEnv, lRes, 0, 4, lVideoRes);
    return lRes;
}


JNIEXPORT void JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_close(JNIEnv *pEnv, jobject pObj) {

	av_free(is);
    /*close the video codec*/
    //avcodec_close(pCodecCtx);
    /*close the video file*/
    av_close_input_file(pFormatCtx);
	
	unRegisterCallBack(pEnv);	
}

JNIEXPORT jint JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_ffmpegGLResize(JNIEnv *pEnv, jobject pObj, int w, int h) {
	s_w = w;
	s_h = h;
	winClientWidth = w;
	winClientHeight = h;
	const GLfloat vertices[20] = {
        // X, Y, Z, U, V
        -1, -1, 0, 0, 1, // Bottom Left
        1, -1, 0, 1, 1, //Bottom Right
        1, 1, 0, 1, 0, //Top Right
        -1, 1, 0, 0, 0 //Top Left
    };
    memcpy(_vertices, vertices, sizeof (_vertices));
	int maxTextureImageUnits[2];
    int maxTextureSize[2];
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, maxTextureImageUnits);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, maxTextureSize);
	_program = createProgram(g_vertextShader, g_fragmentShader);
    if (!_program)
    {	
		LOGI(10, "### error createProgram");
		return -3;
	}
	int positionHandle = glGetAttribLocation(_program, "aPosition");
	checkGlError("glGetAttribLocation aPosition");
	if (positionHandle == -1)
    {
		LOGI(10, "### error positionHandle glGetAttribLocation");
		return -4;
	}
	int textureHandle = glGetAttribLocation(_program, "aTextureCoord");
	checkGlError("glGetAttribLocation aTextureCoord");
    if (textureHandle == -1)
    {
		LOGI(10, "### error  textureHandle glGetAttribLocation");
		return -5;
	}
	// set the vertices array in the shader
    // _vertices contains 4 vertices with 5 coordinates. 3 for (xyz) for the vertices and 2 for the texture
    glVertexAttribPointer(positionHandle, 3, GL_FLOAT, false, 5
            * sizeof (GLfloat), _vertices);
	checkGlError("glVertexAttribPointer aPosition");

	glEnableVertexAttribArray(positionHandle);
    checkGlError("glEnableVertexAttribArray positionHandle");

	glVertexAttribPointer(textureHandle, 2, GL_FLOAT, false, 5
            * sizeof (GLfloat), &_vertices[3]);
	checkGlError("glVertexAttribPointer maTextureHandle");
	glEnableVertexAttribArray(textureHandle);
	checkGlError("glEnableVertexAttribArray textureHandle");
	glUseProgram(_program);
    int i = glGetUniformLocation(_program, "Ytex");
	glUniform1i(i, 0); /* Bind Ytex to texture unit 0 */
	checkGlError("glUniform1i Ytex");
	i = glGetUniformLocation(_program, "Utex");
    glUniform1i(i, 1); /* Bind Utex to texture unit 1 */
	checkGlError("glGetUniformLocation Utex");
    i = glGetUniformLocation(_program, "Vtex");
	checkGlError("glGetUniformLocation");
    glUniform1i(i, 2); /* Bind Vtex to texture unit 2 */	
	checkGlError("glUniform1i Vtex");
	LOGI("native_gl_resize %d %d", w, h);
	glViewport(0, 0, w, h);
	checkGlError("glViewport");
	//SetupTextures();
	flushComplete = 0;
	return 1;
}

JNIEXPORT void JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_ffmpegGLRender(JNIEnv *pEnv, jobject pObj) {
	if(setUpFlag ==0) { // un set up
		setUpFlag = 1;
		SetupTextures();
		LOGI(10, "### render imageWidth = %d, setUpFlag = %d ", imageWidth, setUpFlag);
	}else {
		if(fullScreenFlag) {
			fullScreenFlag = 0;
			glViewport(w_padding, h_padding, winClientWidth, winClientHeight);
		}
		glUseProgram(_program);
		pthread_mutex_lock(&s_vsync_mutex);
		UpdateTextures();
		glDrawElements(GL_TRIANGLE_STRIP, 6, GL_UNSIGNED_BYTE, g_indices);
		if(flushComplete == 0) {
			pthread_cond_wait(&s_vsync_cond, &s_vsync_mutex);
		}
		pthread_mutex_unlock(&s_vsync_mutex);
	}
}

JNIEXPORT jstring JNICALL Java_com_sky_drovik_player_ffmpeg_JniUtils_ffmpegGLClose(JNIEnv *pEnv, jobject pObj) {
	is->quit = 1;
}

void exit() {
	flushComplete = 1;
	pthread_cond_signal(&s_vsync_cond);
	glDeleteTextures(3, _textureIds);
	glDeleteProgram(_program);
	//av_free(is);
	while(1) {
		if(updateFlag == 1) {
			if(yuv_buf) {
				av_free(yuv_buf);
				yuv_buf = NULL;
			}
			break;
		}else {
			usleep(5);
		}
	}
	pthread_mutex_destroy(&s_vsync_cond);
	pthread_mutex_destroy(&s_vsync_mutex);
    /*av_close_input_file(pFormatCtx);
	if(pFormatCtx) {
		avcodec_close(pFormatCtx);
		av_free(pFormatCtx);
	}
	if(pCodecCtx) {
		avcodec_close(pCodecCtx);
		av_free(pCodecCtx);
	}*/
}

int registerCallBack(JNIEnv *env) {
	if(mClass == NULL) {
		mClass = (*env)->FindClass(env, "com/sky/drovik/player/media/VideoActivity");
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
