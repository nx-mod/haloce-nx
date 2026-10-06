#ifndef MJX_HOST_H
#define MJX_HOST_H

/* The Switch's split of mjx.c's picture decode (docs/mjx_movies.md).

The guest is ILP32 and devkitPro's libjpeg is built for the host's 64-bit
ABI, so the guest cannot link it. Guest and host share one address space,
so the guest reads a picture's JPEG bytes and the host decodes them, with
the same raw YUV read mjx.c does on the other ports, straight into plane
storage the guest owns. This record is how the two describe it: every field
32 bits, the same on both sides. */

#include <stdint.h>

#define MJX_HOST_MAXIMUM_COMPONENTS 3

struct mjx_host_plane
{
	uint32_t offset; /* from storage */
	int32_t stride;  /* bytes a row takes, rounded up to whole blocks */
	int32_t width;   /* the plane's real size */
	int32_t height;
};

struct mjx_host_layout
{
	/* in: the guest's plane storage */
	uint32_t storage;
	uint32_t storage_size;
	/* out */
	uint32_t width;
	uint32_t height;
	uint32_t component_count;
	struct mjx_host_plane planes[MJX_HOST_MAXIMUM_COMPONENTS];
	char message[160];
};

/* a .bik the host plays through FFmpeg (host_bik.c), when no .mjx is there:
what mjx.c's header would have said */
struct mjx_host_movie_info
{
	uint32_t width, height;
	uint32_t frame_count;
	uint32_t fps_numerator, fps_denominator;
	uint32_t audio_rate, audio_channels; /* 44100 and 2, or 0 without sound */
	uint32_t audio_frames, audio_size;   /* sample frames, and their bytes */
	char message[160];
};

/* host_bik_open(path, info) -> a handle, or 0 with info->message;
host_bik_decode(handle, frame, layout) as host_mjx_decode;
host_bik_read_audio(handle, offset, destination, length) -> bytes read;
host_bik_close(handle). Addresses are the guest's. */

/* host_mjx_decode(jpeg, length, layout): 0 decoded; a positive number is
the storage the picture needs, larger than what was given, with nothing
decoded; negative failed, with the reason in layout->message. The three
arguments are guest addresses. */

#endif
