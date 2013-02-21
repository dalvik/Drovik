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

