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
	packet_queue_init(&audioq);
	LOGI(1,"audio info rate = %d", aCodecCtx->sample_rate);
	LOGI(1,"audio info channel = %d", aCodecCtx->channels);
	LOGI(1,"audio info sample_fmt = %d,  %d", aCodecCtx->sample_fmt, AV_SAMPLE_FMT_FLT);
	LOGI(1,"audio info frame_size = %d", aCodecCtx->frame_size);
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

	/*****/
	uint8_t *pktdata;  
    int pktsize;  
    int out_size = AVCODEC_MAX_AUDIO_FRAME_SIZE*10;  
    int16_t * inbuf = (int16_t *)av_malloc(out_size);  
    FILE* pcm;  
    pcm = fopen("/mnt/sdcard/result.wav","wb");  

	//register jni play audio
	jobject audio_track;
	jbyteArray output_buffer;
	jclass audio_track_cls;
	jmethodID min_buff_size_id;
	jint buffer_size;
	jmethodID method_write;
	jmethodID method_release;
	
	audio_track_cls = (*env)->FindClass(env,"android/media/AudioTrack");
	min_buff_size_id = (*env)->GetStaticMethodID(
										 env,
										 audio_track_cls,
										"getMinBufferSize",
										"(III)I");
	buffer_size = (*env)->CallStaticIntMethod(env,audio_track_cls,min_buff_size_id,
			    frequency,
			    2,			/*CHANNEL_CONFIGURATION_MONO*/
				2);         /*ENCODING_PCM_16BIT*/
	LOGI(1,"buffer_size=%i",buffer_size);			
	output_buffer = (*env)->NewByteArray(env,(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2);

	jmethodID constructor_id = (*env)->GetMethodID(env,audio_track_cls, "<init>",
			"(IIIIII)V");
	audio_track = (*env)->NewObject(env,audio_track_cls,
			constructor_id,
			3, 			  /*AudioManager.STREAM_MUSIC*/
			frequency,        /*sampleRateInHz*/
			2,			  /*CHANNEL_CONFIGURATION_MONO*/
			2,			  /*ENCODING_PCM_16BIT*/
			buffer_size*10,  /*bufferSizeInBytes*/
			1			  /*AudioTrack.MODE_STREAM*/
	);
	
	//setvolume
	LOGI(1,"setStereoVolume 1");
	jmethodID setStereoVolume = (*env)->GetMethodID(env,audio_track_cls,"setStereoVolume","(FF)I");
	(*env)->CallIntMethod(env,audio_track,setStereoVolume,5.0,5.0);
	LOGI(1,"setStereoVolume 2");
	//play
    jmethodID method_play = (*env)->GetMethodID(env,audio_track_cls, "play",
			"()V");
    (*env)->CallVoidMethod(env,audio_track, method_play);
	//write
    method_write = (*env)->GetMethodID(env,audio_track_cls,"write","([BII)I");
	//release
	method_release = (*env)->GetMethodID(env,audio_track_cls,"release","()V");
    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE(1,"AndroidBitmap_getInfo() failed ! error=%d", ret);
        return bitmap_getinfo_error;
    }
   // LOGI(10,"Checked on the bitmap");
    i = 0;
    while((av_read_frame(pFormatCtx, &packet)>=0)) {
  		if(packet.stream_index==videoStream) {
			if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
				LOGE(1,"AndroidBitmap_lockPixels() failed ! error=%d", ret);
			}
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
    	    }
        } else if(packet.stream_index == audioStream) {
			//LOGI(10,"audio --------");	
			//int len1, audio_size;
			//packet_queue_put(&audioq, &packet);
			pktdata = packet.data;  
            pktsize = packet.size;
			while(pktsize>0)  
            {  
				out_size = AVCODEC_MAX_AUDIO_FRAME_SIZE*10;
				//int len = avcodec_decode_audio2(aCodecCtx, (int16_t *)inbuf, &out_size,pktdata, pktsize);
                int len = avcodec_decode_audio3(aCodecCtx, inbuf, &out_size, &packet);  
                if (len < 0)  
                {  
                    //printf("Error while decoding.\n");  
					LOGI(10,"Error while decoding. %d", len);	
                    break;  
                }  
                if(out_size > 0)  
                {  
                    fwrite(inbuf,1,out_size,pcm);//pcm¼ÇÂ¼   
                    fflush(pcm);  
					(*env)->SetByteArrayRegion(env,output_buffer, 0,out_size, (jbyte *)inbuf);
					//LOGI(10,"decoding. %d" ,len);	
					(*env)->CallIntMethod(env,audio_track,method_write,output_buffer,0,out_size);
					
                }  
                pktsize -= len;  
                pktdata += len;  
            } 
		}else {
			av_free_packet(&packet);
		}
	//return decode_next_frame;
    }
	av_free(inbuf);  
    fclose(pcm);  
	(*env)->CallVoidMethod(env,audio_track, method_release);
   //AndroidBitmap_unlockPixels(env, bitmap);
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
		return -1;
	}
	pkt1 = av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;
	//SDL_LockMutex(q->mutex);
	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	//SDL_CondSignal(q->cond);
	//SDL_UnlockMutex(q->mutex);
	return 0;
}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
	AVPacketList *pkt1;
	int ret;
	//SDL_LockMutex(q->mutex);
	for(;;) {
		if(quit) {
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
		}
	}
	//SDL_UnlockMutex(q->mutex);
	return ret;
}

