/* A reader for the ".mjx" movies that tools/mjx_pack.py builds. See mjx.h for
 * why the movies are re-encoded instead of decoded from Bink.
 *
 * The layout of a ".mjx" file is:
 *
 *   offset  size  contents
 *        0     4  "MJX1"
 *        4     4  the width in pixels
 *        8     4  the height in pixels
 *       12     4  how many pictures there are
 *       16     4  the frame rate's numerator
 *       20     4  the frame rate's denominator
 *       24     4  where the picture index starts
 *       28     4  where the first picture's bytes start
 *       32     4  where the audio starts, or 0 when there is none
 *       36     4  how many bytes of audio there are
 *       40     4  the flags
 *       44     4  reserved
 *       48    ...  the picture index, then the pictures, then the audio
 *
 * Every number is little endian. The index is one record a picture, each a
 * pair of unsigned 32 bit numbers: where that picture's bytes start, and how
 * many there are. A picture is a whole baseline JPEG file.
 */

#include "mjx.h"

#include <stdlib.h>
#include <string.h>

#ifdef HALO_SWITCH
/* The picture decode is the host's (port/switch/host/host_mjx.c):
devkitPro's libjpeg is built for its 64-bit ABI, and this guest is ILP32 and
cannot link it. Reading the file, the index and the audio stays here. */
#include "mjx_host.h"

extern long host_mjx_decode(unsigned int jpeg, unsigned int length, unsigned int layout);
extern long host_bik_open(unsigned int path, unsigned int info);
extern long host_bik_decode(long handle, unsigned int index, unsigned int layout);
extern long host_bik_read_audio(long handle, unsigned int offset, unsigned int destination, unsigned int length);
extern void host_bik_close(long handle);

/* No .mjx: the disc's .bik beside where it would be, played by the host
through FFmpeg (host_bik.c), which answers everything the .mjx header and
pictures would have. */
static int mjx_open_bik(struct mjx_movie *movie, const char *name, const char **error)
{
	struct mjx_host_movie_info info;
	char path[256];
	char *extension;

	snprintf(path, sizeof(path), "%s", name);
	extension = strrchr(path, '.');
	if (!extension || strcmp(extension, ".mjx"))
		return 0;
	strcpy(extension, ".bik");
	movie->host_bik = host_bik_open((unsigned int)(unsigned long)path, (unsigned int)(unsigned long)&info);
	if (!movie->host_bik)
	{
		snprintf(movie->message, sizeof(movie->message), "%s", info.message);
		if (error)
			*error = movie->message;
		return 0;
	}
	movie->width = info.width;
	movie->height = info.height;
	movie->frame_count = info.frame_count;
	movie->fps_numerator = info.fps_numerator;
	movie->fps_denominator = info.fps_denominator;
	movie->audio_rate = info.audio_rate;
	movie->audio_channels = (unsigned short)info.audio_channels;
	movie->audio_frames = info.audio_frames;
	movie->audio_size = info.audio_size;
	return 1;
}
#endif

#define MJX_HEADER_SIZE 48
#define MJX_MAXIMUM_FRAMES 100000

static unsigned long mjx_read_u32(const unsigned char *at)
{
	return (unsigned long)at[0] | ((unsigned long)at[1] << 8) |
		((unsigned long)at[2] << 16) | ((unsigned long)at[3] << 24);
}

#ifndef HALO_SWITCH
/* libjpeg reports a fatal error by calling this, so it must not return: the
 * caller is waiting in setjmp. Anything fatal here means the picture is
 * unusable, so there is nothing sensible to carry on with. */
static void mjx_error_exit(j_common_ptr info)
{
	struct mjx_movie *movie = (struct mjx_movie *)info->client_data;

	if (info->err->msg_code)
	{
		(*info->err->format_message)(info, movie->message);
	}
	else
	{
		snprintf(movie->message, sizeof(movie->message), "libjpeg gave up for an unknown reason");
	}
	longjmp(movie->escape, 1);
}

static void mjx_no_message(j_common_ptr info)
{
	/* libjpeg treats a warning as fatal by default, which would turn a
	 * slightly damaged picture into no picture at all */
	(void)info;
}
#endif

int mjx_open(struct mjx_movie *movie, const char *name, const char **error)
{
	unsigned char header[MJX_HEADER_SIZE];
	unsigned long flags;
	unsigned long index_at;
	unsigned long data_at;
	long file_size;
	unsigned long video_end;
	unsigned long index;

	memset(movie, 0, sizeof(*movie));
	movie->name = name;
	if (error)
		*error = NULL;

	movie->file = fopen(name, "rb");
	if (!movie->file)
	{
#ifdef HALO_SWITCH
		if (mjx_open_bik(movie, name, error))
			return 1;
#endif
		if (error && !*error)
			*error = "the movie could not be opened";
		return 0;
	}
	if (fread(header, 1, MJX_HEADER_SIZE, movie->file) != MJX_HEADER_SIZE ||
		memcmp(header, "MJX1", 4))
	{
		if (error)
			*error = "the movie is not in the expected format";
		mjx_close(movie);
		return 0;
	}

	movie->width = mjx_read_u32(header + 4);
	movie->height = mjx_read_u32(header + 8);
	movie->frame_count = mjx_read_u32(header + 12);
	movie->fps_numerator = mjx_read_u32(header + 16);
	movie->fps_denominator = mjx_read_u32(header + 20);
	index_at = mjx_read_u32(header + 24);
	data_at = mjx_read_u32(header + 28);
	movie->audio_offset = mjx_read_u32(header + 32);
	movie->audio_size = mjx_read_u32(header + 36);
	flags = mjx_read_u32(header + 40);

	if (movie->width == 0 || movie->width > 8192 || movie->height == 0 ||
		movie->height > 8192)
	{
		if (error)
			*error = "the movie's picture size is out of range";
		mjx_close(movie);
		return 0;
	}
	if (movie->frame_count == 0 || movie->frame_count > MJX_MAXIMUM_FRAMES ||
		movie->fps_numerator == 0 || movie->fps_denominator == 0)
	{
		if (error)
			*error = "the movie's frame count or frame rate is out of range";
		mjx_close(movie);
		return 0;
	}

	/* bit 0 says the audio is signed 16 bit little endian at 44100 hertz,
	 * interleaved, which is what ffmpeg writes and what every Bink movie
	 * in this game happens to use */
	if (!(flags & 1))
	{
		/* The flag is the whole truth: a stale offset and size left in the
		 * header describe no audio, and a caller that asks the movie how much
		 * soundtrack it has should not be told about bytes it cannot play. */
		movie->audio_offset = 0;
		movie->audio_size = 0;
	}
	else
	{
		if (movie->audio_offset == 0 || movie->audio_size < 4)
		{
			if (error)
				*error = "the movie claims to have audio but does not";
			mjx_close(movie);
			return 0;
		}
		movie->audio_rate = 44100;
		movie->audio_channels = 2;
		movie->audio_frames = movie->audio_size / (2 * movie->audio_channels);
	}

	/* the pictures sit between the index and the audio, so everything the
	 * index points at has to fit inside the file and stop before any audio */
	if (fseek(movie->file, 0, SEEK_END))
	{
		if (error)
			*error = "the movie's length could not be found";
		mjx_close(movie);
		return 0;
	}
	file_size = ftell(movie->file);
	video_end = movie->audio_offset ? movie->audio_offset : (unsigned long)file_size;
	if (index_at < MJX_HEADER_SIZE ||
		index_at + movie->frame_count * 8 > (unsigned long)file_size ||
		data_at < index_at + movie->frame_count * 8 ||
		data_at > video_end ||
		(movie->audio_offset != 0 && movie->audio_offset > (unsigned long)file_size) ||
		(movie->audio_offset != 0 && movie->audio_size > (unsigned long)file_size - movie->audio_offset))
	{
		if (error)
			*error = "the movie's index does not fit inside the file";
		mjx_close(movie);
		return 0;
	}

	movie->entries = malloc(sizeof(struct mjx_entry) * movie->frame_count);
	if (!movie->entries)
	{
		if (error)
			*error = "there was not enough memory for the movie's index";
		mjx_close(movie);
		return 0;
	}
	if (fseek(movie->file, (long)index_at, SEEK_SET))
	{
		if (error)
			*error = "the movie's index could not be read";
		mjx_close(movie);
		return 0;
	}
	for (index = 0; index < movie->frame_count; index++)
	{
		unsigned char record[8];

		if (fread(record, 1, 8, movie->file) != 8)
		{
			if (error)
				*error = "the movie's index is truncated";
			mjx_close(movie);
			return 0;
		}
		movie->entries[index].offset = mjx_read_u32(record);
		movie->entries[index].length = mjx_read_u32(record + 4);

		/* every picture is a whole JPEG, and the pictures appear in order,
		 * so a picture may not start before the one before it ends */
		if (movie->entries[index].length < 4 ||
			movie->entries[index].offset < data_at ||
			movie->entries[index].offset > video_end ||
			movie->entries[index].length > video_end - movie->entries[index].offset ||
			(index != 0 && movie->entries[index].offset <
				movie->entries[index - 1].offset + movie->entries[index - 1].length))
		{
			if (error)
				*error = "the movie's index points outside the pictures";
			mjx_close(movie);
			return 0;
		}
	}

#ifdef HALO_SWITCH
	/* (no decoder state here: the host decodes) */
	movie->jpeg = NULL;
	if (0)
#else
	movie->jpeg = malloc(sizeof(struct jpeg_decompress_struct));
	if (!movie->jpeg)
#endif
	{
		if (error)
			*error = "there was not enough memory for the decoder";
		mjx_close(movie);
		return 0;
	}
	return 1;
}

#ifdef HALO_SWITCH
/* this frame's planes, decoded by the host into plane_storage; the storage
 * grows, once, to what the host says the picture needs */
static int mjx_decode_frame_on_host(struct mjx_movie *movie, const struct mjx_entry *entry, unsigned long index,
	const char **error)
{
	struct mjx_host_layout layout;
	long result;
	int attempt, component;

	for (attempt = 0; attempt < 2; attempt++)
	{
		memset(&layout, 0, sizeof(layout));
		layout.storage = (uint32_t)(unsigned long)movie->plane_storage;
		layout.storage_size = (uint32_t)movie->plane_storage_size;
		result = movie->host_bik ?
			host_bik_decode(movie->host_bik, (unsigned int)index, (unsigned int)(unsigned long)&layout) :
			host_mjx_decode((unsigned int)(unsigned long)movie->scratch, (unsigned int)entry->length,
				(unsigned int)(unsigned long)&layout);
		if (result <= 0)
			break;
		free(movie->plane_storage);
		movie->plane_storage = malloc((unsigned long)result);
		movie->plane_storage_size = movie->plane_storage ? (unsigned long)result : 0;
		if (!movie->plane_storage)
		{
			if (error)
				*error = "there was not enough memory for the picture";
			return 0;
		}
	}
	if (result != 0)
	{
		snprintf(movie->message, sizeof(movie->message), "%s",
			result > 0 ? "the picture kept needing more room" : layout.message);
		if (error)
			*error = movie->message;
		return 0;
	}
	if (layout.width != movie->width || layout.height != movie->height)
	{
		if (error)
			*error = "the picture's size does not match the movie's";
		return 0;
	}
	movie->frame.component_count = (int)layout.component_count;
	for (component = 0; component < movie->frame.component_count; component++)
	{
		struct mjx_component *destination = &movie->frame.components[component];

		destination->samples = movie->plane_storage + layout.planes[component].offset;
		destination->stride = layout.planes[component].stride;
		destination->width = layout.planes[component].width;
		destination->height = layout.planes[component].height;
	}
	return 1;
}
#else
/* Works out how large each of the planes needs to be, and points the frame at
 * storage big enough for all of them. */
static int mjx_prepare_planes(struct mjx_movie *movie, const char **error)
{
	struct jpeg_decompress_struct *jpeg = movie->jpeg;
	unsigned long needed = 0;
	int component;

	movie->frame.component_count = jpeg->num_components;
	if (movie->frame.component_count < 1 ||
		movie->frame.component_count > MJX_MAXIMUM_COMPONENTS)
	{
		if (error)
			*error = "the picture has an unexpected number of components";
		return 0;
	}

	for (component = 0; component < movie->frame.component_count; component++)
	{
		jpeg_component_info *source = &jpeg->comp_info[component];
		struct mjx_component *destination = &movie->frame.components[component];

		/* libjpeg reports each plane's real size and the size it is
		 * actually stored at, which is rounded up to whole blocks */
		destination->width = source->downsampled_width;
		destination->height = source->downsampled_height;
		destination->stride = source->width_in_blocks * DCTSIZE;
		needed += (unsigned long)destination->stride *
			(source->height_in_blocks * DCTSIZE);
	}

	if (needed > movie->plane_storage_size)
	{
		free(movie->plane_storage);
		movie->plane_storage = malloc(needed);
		if (!movie->plane_storage)
		{
			movie->plane_storage_size = 0;
			if (error)
				*error = "there was not enough memory for the picture";
			return 0;
		}
		movie->plane_storage_size = needed;
	}
	{
		unsigned char *at = movie->plane_storage;

		for (component = 0; component < movie->frame.component_count; component++)
		{
			jpeg_component_info *source = &jpeg->comp_info[component];
			struct mjx_component *destination = &movie->frame.components[component];

			destination->samples = at;
			at += (unsigned long)destination->stride * (source->height_in_blocks * DCTSIZE);
		}
	}
	return 1;
}

#endif

int mjx_decode_frame(struct mjx_movie *movie, unsigned long index, const char **error)
{
	struct jpeg_decompress_struct *jpeg = movie->jpeg;
	struct mjx_entry *entry;

	if (error)
		*error = NULL;
	if (index >= movie->frame_count)
	{
		if (error)
			*error = "the frame number is past the end of the movie";
		return 0;
	}
#ifdef HALO_SWITCH
	/* (a .bik: no index or file here, the host reads it) */
	if (movie->host_bik)
		return mjx_decode_frame_on_host(movie, NULL, index, error);
#endif
	entry = &movie->entries[index];

	/* read this picture's bytes into a buffer that is reused from frame to
	 * frame, since no two are needed at once */
	if (entry->length > movie->scratch_size)
	{
		free(movie->scratch);
		movie->scratch = malloc(entry->length);
		if (!movie->scratch)
		{
			movie->scratch_size = 0;
			if (error)
				*error = "there was not enough memory for the picture";
			return 0;
		}
		movie->scratch_size = entry->length;
	}
	if (fseek(movie->file, (long)entry->offset, SEEK_SET) ||
		fread(movie->scratch, 1, entry->length, movie->file) != entry->length)
	{
		if (error)
			*error = "the picture could not be read";
		return 0;
	}

#ifdef HALO_SWITCH
	(void)jpeg;
	return mjx_decode_frame_on_host(movie, entry, index, error);
#else
	if (setjmp(movie->escape))
	{
		/* a longjmp from libjpeg lands here, with the reason in the
		 * message buffer and any picture we had half filled thrown away */
		if (error)
			*error = movie->message;
		return 0;
	}

	if (!movie->jpeg_created)
	{
		jpeg->client_data = movie;
		jpeg->err = jpeg_std_error(&movie->errors);
		movie->errors.error_exit = mjx_error_exit;
		movie->errors.output_message = mjx_no_message;
		jpeg_create_decompress(jpeg);
		movie->jpeg_created = 1;
	}
	else if (movie->opened)
	{
		/* the previous picture was not finished with, so put the decoder
		 * back to the start and let it read a new header */
		jpeg_abort_decompress(jpeg);
		movie->opened = 0;
	}

	jpeg_mem_src(jpeg, movie->scratch, entry->length);
	if (jpeg_read_header(jpeg, TRUE) != JPEG_HEADER_OK)
	{
		if (error)
			*error = "the picture's header could not be read";
		return 0;
	}
	movie->opened = 1;

	/* keep the picture as the three planes it already is: asking for anything
	 * else would make libjpeg convert, which is both slower and lossy */
	jpeg->out_color_space = JCS_YCbCr;
	jpeg->raw_data_out = TRUE;
	jpeg_start_decompress(jpeg);

	if (!mjx_prepare_planes(movie, error))
	{
		jpeg_abort_decompress(jpeg);
		return 0;
	}
	if ((unsigned long)jpeg->output_width != movie->width ||
		(unsigned long)jpeg->output_height != movie->height)
	{
		if (error)
			*error = "the picture's size does not match the movie's";
		jpeg_abort_decompress(jpeg);
		return 0;
	}

	/* libjpeg hands back one iMCU row at a time: luma gets twice as many
	 * lines as chroma, because a 4:2:0 minimum coded unit is 16x16. Passing
	 * a whole block's worth of lines and repeating until it stops is how
	 * the raw reader is meant to be driven. */
	{
		int rows_per_call = jpeg->max_v_samp_factor * DCTSIZE;
		int mcu_row = 0;

		while (jpeg->output_scanline < jpeg->output_height)
		{
			JSAMPROW rows[MJX_MAXIMUM_COMPONENTS][DCTSIZE * 4];
			JSAMPROW *planes[MJX_MAXIMUM_COMPONENTS];
			int component;

			for (component = 0; component < movie->frame.component_count; component++)
			{
				jpeg_component_info *source = &jpeg->comp_info[component];
				struct mjx_component *destination = &movie->frame.components[component];
				int plane_rows = (int)(source->height_in_blocks * DCTSIZE);
				int first_row = mcu_row * source->v_samp_factor * DCTSIZE;
				int line;

				/* libjpeg may hand back a whole block's lines even where
				 * the picture ends part way through one, so the spare
				 * pointers have to stay inside the plane */
				for (line = 0; line < DCTSIZE * 4; line++)
				{
					int row = first_row + line;

					rows[component][line] = (row < plane_rows) ?
						&destination->samples[row * destination->stride] :
						&destination->samples[0];
				}
				planes[component] = rows[component];
			}

			if (jpeg_read_raw_data(jpeg, (JSAMPIMAGE)planes, rows_per_call) == 0)
			{
				if (error)
					*error = "the picture ended before all of its rows were read";
				jpeg_abort_decompress(jpeg);
				movie->opened = 0;
				return 0;
			}
			mcu_row++;
		}
	}

	jpeg_finish_decompress(jpeg);
	movie->opened = 0;
	return 1;
#endif
}

unsigned long mjx_read_audio(struct mjx_movie *movie, unsigned long offset,
	unsigned char *destination, unsigned long length)
{
#ifdef HALO_SWITCH
	if (movie->host_bik)
	{
		long got = host_bik_read_audio(movie->host_bik, (unsigned int)offset,
			(unsigned int)(unsigned long)destination, (unsigned int)length);

		return got > 0 ? (unsigned long)got : 0;
	}
#endif
	if (!movie->audio_size || offset >= movie->audio_size)
		return 0;
	if (length > movie->audio_size - offset)
		length = movie->audio_size - offset;
	if (fseek(movie->file, (long)(movie->audio_offset + offset), SEEK_SET))
		return 0;
	return fread(destination, 1, length, movie->file);
}

void mjx_close(struct mjx_movie *movie)
{
#ifdef HALO_SWITCH
	if (movie->host_bik)
	{
		host_bik_close(movie->host_bik);
		movie->host_bik = 0;
	}
#endif
#ifndef HALO_SWITCH
	if (movie->jpeg_created)
	{
		/* the decoder must not longjmp out of its own teardown */
		if (setjmp(movie->escape) == 0)
			jpeg_destroy_decompress(movie->jpeg);
		movie->jpeg_created = 0;
	}
#endif
	if (movie->file)
	{
		fclose(movie->file);
		movie->file = NULL;
	}
	free(movie->jpeg);
	movie->jpeg = NULL;
	free(movie->entries);
	movie->entries = NULL;
	free(movie->scratch);
	movie->scratch = NULL;
	free(movie->plane_storage);
	movie->plane_storage = NULL;
}