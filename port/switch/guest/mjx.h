#ifndef MJX_H
#define MJX_H

/* A reader for the ".mjx" movies that tools/mjx_pack.py builds.
 *
 * A ".mjx" file holds an original Bink movie that has been re-encoded as
 * baseline JPEG, one whole frame per picture, plus the Bink audio as plain
 * PCM. The Bink video format is proprietary, so rather than reimplement its
 * decoder we replace the whole movie with pictures that libjpeg already knows
 * how to decode, and present them through the Bink entry points that
 * source/bink/bink_playback.c already calls.
 *
 * The frames are still YUV 4:2:0, exactly as Bink delivers them, so the same
 * conversion to the game's X8R8G8B8 texture applies.
 */

#include <setjmp.h>
#include <stdio.h>

#ifdef HALO_SWITCH
/* The Switch guest never calls libjpeg - the host decodes (mjx_host.h) - and
jpeglib.h's own "boolean" clashes with the game's, so only the names the
struct below carries: */
struct jpeg_decompress_struct;
struct jpeg_error_mgr
{
	int unused;
};
#define JMSG_LENGTH_MAX 200
#else
#include <jpeglib.h>
#endif

/* Bink movies use at most Y, Cb and Cr, and every one of them is 4:2:0. */
#define MJX_MAXIMUM_COMPONENTS 3

/* One decoded frame, as separate planes. Component 0 is luma at the full
 * picture size; components 1 and 2 are the chroma planes at half size. */
struct mjx_component {
	unsigned char *samples;
	int stride;    /* bytes a row of this plane takes */
	int width;     /* the plane's real width, without any row padding */
	int height;
};

struct mjx_frame {
	struct mjx_component components[MJX_MAXIMUM_COMPONENTS];
	int component_count;
};

/* A picture index entry, resolved to a place in the file. */
struct mjx_entry {
	unsigned long offset;
	unsigned long length;
};

struct mjx_movie {
	FILE *file;
	const char *name;

	unsigned long width;
	unsigned long height;
	unsigned long frame_count;
	unsigned long fps_numerator;
	unsigned long fps_denominator;

	/* the audio, if the packer found any: signed 16 bit little endian,
	 * interleaved, at the rate and channel count below */
	unsigned long audio_offset;
	unsigned long audio_size;
	unsigned long audio_rate;
	unsigned short audio_channels;
	unsigned long audio_frames;  /* sample frames, so rate and size agree */

	struct mjx_entry *entries;
	unsigned char *scratch;      /* holds the frame being decoded */
	unsigned long scratch_size;

	/* the component planes of the most recent mjx_decode_frame */
	unsigned char *plane_storage;
	unsigned long plane_storage_size;
	struct mjx_frame frame;

	/* libjpeg insists on a large chunk of state, and the caller should not
	 * have to provide it, so it is kept here and reached through a pointer */
	struct jpeg_decompress_struct *jpeg;
	struct jpeg_error_mgr errors;
	jmp_buf escape;              /* libjpeg reports errors by longjmp */
	char message[JMSG_LENGTH_MAX];
	int jpeg_created;
	int opened;                 /* a picture's header has been read and not finished */
#ifdef HALO_SWITCH
	long host_bik;              /* a .bik the host plays (host_bik.c), or 0 */
#endif
};

#ifdef __cplusplus
extern "C" {
#endif

/* Opens a movie and reads its index. Returns 0 and leaves *error pointing at a
 * description when the file is missing or malformed. */
int mjx_open(struct mjx_movie *movie, const char *name, const char **error);

/* Decodes one frame into movie->frame, which stays valid until the next call.
 * Returns 0 on failure with *error describing why. */
int mjx_decode_frame(struct mjx_movie *movie, unsigned long index, const char **error);

/* Reads up to length bytes of the movie's audio, starting at offset bytes in.
 * Returns the number of bytes actually read. */
unsigned long mjx_read_audio(struct mjx_movie *movie, unsigned long offset,
	unsigned char *destination, unsigned long length);

void mjx_close(struct mjx_movie *movie);

#ifdef __cplusplus
}
#endif

#endif