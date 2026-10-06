/*
HOST_BIK.C

The disc's Bink movies, played as they are: devkitPro's switch-ffmpeg has
Bink's video and audio decoders and its demuxer. The guest's mjx.c opens a
.bik through these when there is no .mjx transcode beside it, and hands the
pictures and sound to bink_mjx.c exactly as a .mjx's (docs/mjx_movies.md):

- pictures as Y, U and V planes in the guest's storage (mjx_host.h), the
  same full-range BT.601 the .mjx path converts;
- sound as 44100 hertz stereo signed 16-bit PCM, read by byte offset, the
  only format bink_mjx.c accepts - swresample converts Bink's.

Video and audio each have their own demuxer, so reading one never moves
the other's place in the file. Playback is sequential: a frame or sample
before the current place starts that stream again from the beginning.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>

#include "host.h"
#include "../guest/mjx_host.h"


#define AUDIO_RATE 44100
#define AUDIO_CHANNELS 2
#define AUDIO_FRAME_BYTES (AUDIO_CHANNELS * 2)
#define MAXIMUM_MOVIES 4
/* decoded sound kept behind the reader before it is dropped */
#define AUDIO_KEEP_BEHIND (1u << 20)

struct bik_stream
{
	AVFormatContext *format;
	AVCodecContext *codec;
	AVPacket *packet;
	AVFrame *frame;
	int index;
	int ended;
};

struct bik_movie
{
	int used;
	char path[256];
	struct bik_stream video;
	long next_frame; /* the frame the video decoder gives next */
	struct bik_stream audio;
	SwrContext *resampler;
	unsigned char *pcm;  /* decoded sound from pcm_base on */
	unsigned long pcm_base, pcm_length, pcm_capacity;
};

static struct bik_movie s_movies[MAXIMUM_MOVIES];

static void stream_close(struct bik_stream *stream)
{
	av_frame_free(&stream->frame);
	av_packet_free(&stream->packet);
	avcodec_free_context(&stream->codec);
	avformat_close_input(&stream->format);
	memset(stream, 0, sizeof(*stream));
}

/* the file's stream of the type asked for, with its decoder open */
static int stream_open(struct bik_stream *stream, const char *path, enum AVMediaType type, char *message,
	size_t message_size)
{
	const AVCodec *decoder = NULL;

	char url[300];

	memset(stream, 0, sizeof(*stream));
	/* "file:" first: FFmpeg reads what is before the first colon as a
	protocol, and "sdmc" is none it knows */
	snprintf(url, sizeof(url), "file:%s", path);
	if (avformat_open_input(&stream->format, url, NULL, NULL) < 0)
	{
		snprintf(message, message_size, "could not open %s", path);
		return 0;
	}
	if (avformat_find_stream_info(stream->format, NULL) < 0)
	{
		snprintf(message, message_size, "could not read %s's streams", path);
		stream_close(stream);
		return 0;
	}
	stream->index = av_find_best_stream(stream->format, type, -1, -1, &decoder, 0);
	if (stream->index < 0 || !decoder)
	{
		stream_close(stream);
		return -1; /* (no such stream: not an error for sound) */
	}
	stream->codec = avcodec_alloc_context3(decoder);
	stream->packet = av_packet_alloc();
	stream->frame = av_frame_alloc();
	if (!stream->codec || !stream->packet || !stream->frame ||
		avcodec_parameters_to_context(stream->codec, stream->format->streams[stream->index]->codecpar) < 0 ||
		avcodec_open2(stream->codec, decoder, NULL) < 0)
	{
		snprintf(message, message_size, "could not start %s's %s decoder", path,
			type == AVMEDIA_TYPE_VIDEO ? "picture" : "sound");
		stream_close(stream);
		return 0;
	}
	return 1;
}

/* the next decoded frame of the stream into stream->frame; 0 at the end */
static int stream_next(struct bik_stream *stream)
{
	for (;;)
	{
		int result = avcodec_receive_frame(stream->codec, stream->frame);

		if (result == 0)
			return 1;
		if (result != AVERROR(EAGAIN) || stream->ended)
			return 0;
		for (;;)
		{
			result = av_read_frame(stream->format, stream->packet);
			if (result < 0)
			{
				/* the end: drain what the decoder still holds */
				stream->ended = 1;
				avcodec_send_packet(stream->codec, NULL);
				break;
			}
			if (stream->packet->stream_index == stream->index)
			{
				avcodec_send_packet(stream->codec, stream->packet);
				av_packet_unref(stream->packet);
				break;
			}
			av_packet_unref(stream->packet);
		}
	}
}

static void stream_rewind(struct bik_stream *stream)
{
	av_seek_frame(stream->format, stream->index, 0, AVSEEK_FLAG_BACKWARD);
	avcodec_flush_buffers(stream->codec);
	stream->ended = 0;
}

static struct bik_movie *movie_get(long handle)
{
	return handle >= 1 && handle <= MAXIMUM_MOVIES && s_movies[handle - 1].used ? &s_movies[handle - 1] : NULL;
}

void host_bik_close(long handle)
{
	struct bik_movie *movie = movie_get(handle);

	if (!movie)
		return;
	stream_close(&movie->video);
	stream_close(&movie->audio);
	swr_free(&movie->resampler);
	free(movie->pcm);
	memset(movie, 0, sizeof(*movie));
}

long host_bik_open(unsigned int path_address, unsigned int info_address)
{
	const char *path = (const char *)(uintptr_t)path_address;
	struct mjx_host_movie_info *info = (struct mjx_host_movie_info *)(uintptr_t)info_address;
	struct bik_movie *movie = NULL;
	AVStream *video;
	long handle;
	int audio;

	memset(info, 0, sizeof(*info));
	for (handle = 1; handle <= MAXIMUM_MOVIES && s_movies[handle - 1].used; handle++)
		;
	if (handle > MAXIMUM_MOVIES)
	{
		snprintf(info->message, sizeof(info->message), "too many movies open");
		return 0;
	}
	movie = &s_movies[handle - 1];
	memset(movie, 0, sizeof(*movie));
	movie->used = 1;
	snprintf(movie->path, sizeof(movie->path), "%s", path);
	if (stream_open(&movie->video, path, AVMEDIA_TYPE_VIDEO, info->message, sizeof(info->message)) <= 0)
	{
		if (!info->message[0])
			snprintf(info->message, sizeof(info->message), "%s has no pictures", path);
		host_bik_close(handle);
		return 0;
	}
	video = movie->video.format->streams[movie->video.index];
	info->width = (uint32_t)movie->video.codec->width;
	info->height = (uint32_t)movie->video.codec->height;
	{
		AVRational rate = video->avg_frame_rate.num ? video->avg_frame_rate : video->r_frame_rate;

		info->fps_numerator = (uint32_t)rate.num;
		info->fps_denominator = (uint32_t)(rate.den ? rate.den : 1);
	}
	info->frame_count = (uint32_t)video->nb_frames;
	if (!info->frame_count && video->duration > 0 && info->fps_denominator)
		info->frame_count = (uint32_t)(av_rescale_q(video->duration, video->time_base,
			(AVRational){(int)info->fps_denominator, (int)info->fps_numerator}));

	audio = stream_open(&movie->audio, path, AVMEDIA_TYPE_AUDIO, info->message, sizeof(info->message));
	if (audio > 0)
	{
		AVStream *sound = movie->audio.format->streams[movie->audio.index];
		AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
		double seconds = sound->duration > 0 ? sound->duration * av_q2d(sound->time_base) :
			(info->fps_numerator ? (double)info->frame_count * info->fps_denominator / info->fps_numerator : 0);

		if (swr_alloc_set_opts2(&movie->resampler, &stereo, AV_SAMPLE_FMT_S16, AUDIO_RATE,
			&movie->audio.codec->ch_layout, movie->audio.codec->sample_fmt, movie->audio.codec->sample_rate, 0,
			NULL) < 0 || swr_init(movie->resampler) < 0)
		{
			host_logf(HOST_LOG_INFO, "bik: %s: no sound (the resampler would not start)", path);
			stream_close(&movie->audio);
		}
		else
		{
			info->audio_rate = AUDIO_RATE;
			info->audio_channels = AUDIO_CHANNELS;
			info->audio_frames = (uint32_t)(seconds * AUDIO_RATE);
			info->audio_size = info->audio_frames * AUDIO_FRAME_BYTES;
		}
	}
	info->message[0] = 0;
	host_logf(HOST_LOG_INFO, "bik: %s, %ux%u, %u frames at %u/%u, %s", path, info->width, info->height, info->frame_count,
		info->fps_numerator, info->fps_denominator, info->audio_size ? "with sound" : "no sound");
	return handle;
}

long host_bik_decode(long handle, unsigned int index, unsigned int layout_address)
{
	struct bik_movie *movie = movie_get(handle);
	struct mjx_host_layout *layout = (struct mjx_host_layout *)(uintptr_t)layout_address;
	unsigned char *storage = (unsigned char *)(uintptr_t)layout->storage;
	AVFrame *frame;
	unsigned long needed = 0;
	int plane;

	layout->message[0] = 0;
	if (!movie)
	{
		snprintf(layout->message, sizeof(layout->message), "the movie is not open");
		return -1;
	}
	frame = movie->video.frame;
	/* sequential: back to the start for an earlier frame, then forward */
	if ((long)index < movie->next_frame)
	{
		stream_rewind(&movie->video);
		movie->next_frame = 0;
	}
	while (movie->next_frame <= (long)index)
	{
		if (!stream_next(&movie->video))
		{
			snprintf(layout->message, sizeof(layout->message), "the movie ended before frame %u", index);
			return -1;
		}
		movie->next_frame++;
	}
	if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVA420P)
	{
		snprintf(layout->message, sizeof(layout->message), "unexpected picture format %d", frame->format);
		return -1;
	}
	layout->width = (uint32_t)frame->width;
	layout->height = (uint32_t)frame->height;
	layout->component_count = 3;
	for (plane = 0; plane < 3; plane++)
	{
		struct mjx_host_plane *destination = &layout->planes[plane];
		int width = plane ? (frame->width + 1) / 2 : frame->width;
		int height = plane ? (frame->height + 1) / 2 : frame->height;

		destination->offset = (uint32_t)needed;
		destination->width = width;
		destination->height = height;
		destination->stride = (width + 15) & ~15;
		needed += (unsigned long)destination->stride * (unsigned long)height;
	}
	if (needed > layout->storage_size || !storage)
		return (long)needed;
	for (plane = 0; plane < 3; plane++)
	{
		struct mjx_host_plane *destination = &layout->planes[plane];
		int row;

		for (row = 0; row < destination->height; row++)
			memcpy(storage + destination->offset + (unsigned long)row * destination->stride,
				frame->data[plane] + (long)row * frame->linesize[plane], (size_t)destination->width);
	}
	return 0;
}

/* more decoded sound onto the end of movie->pcm; 0 at the end */
static int audio_decode_more(struct bik_movie *movie)
{
	AVFrame *frame = movie->audio.frame;
	int out_frames, got;
	unsigned long bytes;

	if (!stream_next(&movie->audio))
		return 0;
	out_frames = swr_get_out_samples(movie->resampler, frame->nb_samples);
	if (out_frames <= 0)
		return 1;
	bytes = (unsigned long)out_frames * AUDIO_FRAME_BYTES;
	if (movie->pcm_length + bytes > movie->pcm_capacity)
	{
		unsigned long capacity = (movie->pcm_length + bytes) * 2;
		unsigned char *grown = realloc(movie->pcm, capacity);

		if (!grown)
			return 0;
		movie->pcm = grown;
		movie->pcm_capacity = capacity;
	}
	{
		uint8_t *out = movie->pcm + movie->pcm_length;

		got = swr_convert(movie->resampler, &out, out_frames, (const uint8_t **)frame->extended_data,
			frame->nb_samples);
	}
	if (got > 0)
		movie->pcm_length += (unsigned long)got * AUDIO_FRAME_BYTES;
	return 1;
}

long host_bik_read_audio(long handle, unsigned int offset, unsigned int destination_address, unsigned int length)
{
	struct bik_movie *movie = movie_get(handle);
	unsigned char *destination = (unsigned char *)(uintptr_t)destination_address;
	unsigned long available;

	if (!movie || !movie->audio.codec || !length)
		return 0;
	/* behind what is kept: the sound again from the start */
	if (offset < movie->pcm_base)
	{
		stream_rewind(&movie->audio);
		swr_free(&movie->resampler);
		{
			AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;

			swr_alloc_set_opts2(&movie->resampler, &stereo, AV_SAMPLE_FMT_S16, AUDIO_RATE,
				&movie->audio.codec->ch_layout, movie->audio.codec->sample_fmt, movie->audio.codec->sample_rate,
				0, NULL);
			swr_init(movie->resampler);
		}
		movie->pcm_base = 0;
		movie->pcm_length = 0;
	}
	/* drop what is well behind the reader */
	if (offset - movie->pcm_base > AUDIO_KEEP_BEHIND)
	{
		unsigned long drop = (offset - movie->pcm_base - AUDIO_KEEP_BEHIND) & ~(unsigned long)(AUDIO_FRAME_BYTES - 1);

		if (drop > movie->pcm_length)
			drop = movie->pcm_length;
		memmove(movie->pcm, movie->pcm + drop, movie->pcm_length - drop);
		movie->pcm_length -= drop;
		movie->pcm_base += drop;
	}
	while (offset + length > movie->pcm_base + movie->pcm_length)
	{
		if (!audio_decode_more(movie))
			break;
	}
	if (offset >= movie->pcm_base + movie->pcm_length)
		return 0;
	available = movie->pcm_base + movie->pcm_length - offset;
	if (available > length)
		available = length;
	memcpy(destination, movie->pcm + (offset - movie->pcm_base), available);
	return (long)available;
}
