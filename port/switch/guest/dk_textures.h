/*
DK_TEXTURES.H

The deko3d renderer's texture cache (xbox_textures_dk.c, a copy of
port/linux/src/xbox_textures.c) and what it needs of the device
(d3d8_dk.c), whose command stream it writes into.
*/

#ifndef __DK_TEXTURES_H
#define __DK_TEXTURES_H

#include "xgpu.h"

/* The image number of the Xbox texture whose header is resource (and the
palette its stages' palettized textures use), sending its texels to the host
if it is new or has been written since; 0 if there is none to draw. *kind
receives DK_TEXTURE_2D, _3D or _CUBE, and description what xbox_textures
knows of it. */
uint32_t dk_texture_get(const DWORD *resource, const D3DCOLOR *palette, int *kind,
	struct xgpu_texture_description *description);

/* How far from its centre a 3D texture lets anything through, in its
coordinates (0 to 1 across it): the farthest texel not all zero, at its two
finest levels, and a texel's diagonal more for its filtering; -1 if it is not
known (a texture not a 3D one, compressed, or not yet sent). The texture is
found by its texels' address. The dynamic lights' distance attenuation is a
ball in its box: their passes let the pixels past it go (d3d8_dk.c). */
float dk_texture_reach(unsigned long address);

/* d3d8_dk.c: room for a command in the stream (dk_commands.h), its header
filled in */
void *dk_stream_command(uint32_t type, unsigned long size);
/* d3d8_dk.c: buffer is the guest's own copy of data a command names; it is
freed once the stream has been handed to the host, which reads it then. A
great deal of it waiting hands the stream over. Call after the command is
written: it may do that. */
void dk_stream_free_after_handover(void *buffer, unsigned long size);
/* a buffer of size bytes for a command to name, maybe one the GPU is done with:
   hand it to dk_stream_free_after_handover with the same size */
void *dk_stream_buffer(unsigned long size);

#endif
