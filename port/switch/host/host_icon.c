/*
HOST_ICON.C

The game's own icon, for a forwarder: the title image every Xbox game
carries in its executable (default.xbe's $$XTIMAGE section, an XPR texture,
usually 128x128 DXT1), written beside the NRO as a 256x256 icon.jpg, the
size and format Switch icons are.
*/

#include "host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>

#define ICON_SIZE 256

static uint32_t read_u32(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* the $$XTIMAGE section's bytes, or NULL */
static const unsigned char *title_image(const unsigned char *xbe, size_t size, uint32_t *image_size)
{
	uint32_t base, count, headers, index;

	if (size < 0x124 || memcmp(xbe, "XBEH", 4))
		return NULL;
	base = read_u32(xbe + 0x104);
	count = read_u32(xbe + 0x11C);
	headers = read_u32(xbe + 0x120) - base;
	if (headers >= size || count > 256 || headers + (size_t)count * 56 > size)
		return NULL;
	for (index = 0; index < count; index++)
	{
		const unsigned char *header = xbe + headers + index * 56;
		uint32_t name = read_u32(header + 0x14) - base;
		uint32_t raw = read_u32(header + 0x0C), raw_size = read_u32(header + 0x10);

		if (name + 10 <= size && !memcmp(xbe + name, "$$XTIMAGE", 10) && raw + (size_t)raw_size <= size)
		{
			*image_size = raw_size;
			return xbe + raw;
		}
	}
	return NULL;
}

static void colour565(uint16_t value, unsigned char out[3])
{
	out[0] = (unsigned char)(((value >> 11) & 31) * 255 / 31);
	out[1] = (unsigned char)(((value >> 5) & 63) * 255 / 63);
	out[2] = (unsigned char)((value & 31) * 255 / 31);
}

/* a DXT colour block (the first 8 bytes of DXT1's, the last of DXT3/5's) */
static void colour_block(const unsigned char *block, int four_colours, unsigned char out[16][3])
{
	uint16_t c0 = (uint16_t)(block[0] | (block[1] << 8)), c1 = (uint16_t)(block[2] | (block[3] << 8));
	uint32_t bits = read_u32(block + 4);
	unsigned char palette[4][3];
	int index, channel;

	colour565(c0, palette[0]);
	colour565(c1, palette[1]);
	for (channel = 0; channel < 3; channel++)
	{
		if (four_colours || c0 > c1)
		{
			palette[2][channel] = (unsigned char)((2 * palette[0][channel] + palette[1][channel]) / 3);
			palette[3][channel] = (unsigned char)((palette[0][channel] + 2 * palette[1][channel]) / 3);
		}
		else
		{
			palette[2][channel] = (unsigned char)((palette[0][channel] + palette[1][channel]) / 2);
			palette[3][channel] = 0;
		}
	}
	for (index = 0; index < 16; index++)
		memcpy(out[index], palette[(bits >> (2 * index)) & 3], 3);
}

/* Morton (swizzled) order of an ARGB texture */
static uint32_t swizzle(uint32_t x, uint32_t y)
{
	uint32_t result = 0, bit;

	for (bit = 0; bit < 16; bit++)
		result |= ((x >> bit) & 1) << (2 * bit) | ((y >> bit) & 1) << (2 * bit + 1);
	return result;
}

/* the XPR texture as RGB pixels; NULL if its format is not one of these */
static unsigned char *decode_xpr(const unsigned char *xpr, uint32_t size, uint32_t *width, uint32_t *height)
{
	uint32_t data_offset, format, kind, x, y;
	const unsigned char *data;
	unsigned char *pixels;

	if (size < 32 || memcmp(xpr, "XPR0", 4))
		return NULL;
	data_offset = read_u32(xpr + 8);
	format = read_u32(xpr + 12 + 12);
	kind = (format >> 8) & 0xff;
	*width = 1u << ((format >> 20) & 0xf);
	*height = 1u << ((format >> 24) & 0xf);
	if (*width > 1024 || *height > 1024 || data_offset >= size)
		return NULL;
	data = xpr + data_offset;
	pixels = calloc((size_t)*width * *height, 3);
	if (!pixels)
		return NULL;
	if (kind == 0x0C || kind == 0x0E || kind == 0x0F)
	{
		/* DXT1, DXT3, DXT5: 4x4 blocks in rows */
		size_t block_size = kind == 0x0C ? 8 : 16, offset = 0;

		for (y = 0; y < *height; y += 4)
		{
			for (x = 0; x < *width; x += 4, offset += block_size)
			{
				unsigned char block[16][3];
				int index;

				if (data_offset + offset + block_size > size)
					return pixels;
				colour_block(data + offset + (block_size - 8), kind != 0x0C, block);
				for (index = 0; index < 16; index++)
				{
					uint32_t px = x + (index & 3), py = y + (index >> 2);

					if (px < *width && py < *height)
						memcpy(pixels + (py * *width + px) * 3, block[index], 3);
				}
			}
		}
		return pixels;
	}
	if (kind == 0x06 || kind == 0x07)
	{
		/* A8R8G8B8 / X8R8G8B8, swizzled */
		for (y = 0; y < *height; y++)
		{
			for (x = 0; x < *width; x++)
			{
				size_t at = (size_t)swizzle(x, y) * 4;

				if (data_offset + at + 4 > size)
					continue;
				pixels[(y * *width + x) * 3 + 0] = data[at + 2];
				pixels[(y * *width + x) * 3 + 1] = data[at + 1];
				pixels[(y * *width + x) * 3 + 2] = data[at + 0];
			}
		}
		return pixels;
	}
	host_logf(HOST_LOG_WARN, "icon: the title image's format 0x%02x is not read", (unsigned)kind);
	free(pixels);
	return NULL;
}

static int write_jpeg(const char *path, const unsigned char *rgb, int size)
{
	struct jpeg_compress_struct compress;
	struct jpeg_error_mgr error;
	FILE *file = fopen(path, "wb");
	int row;

	if (!file)
		return 0;
	compress.err = jpeg_std_error(&error);
	jpeg_create_compress(&compress);
	jpeg_stdio_dest(&compress, file);
	compress.image_width = (JDIMENSION)size;
	compress.image_height = (JDIMENSION)size;
	compress.input_components = 3;
	compress.in_color_space = JCS_RGB;
	jpeg_set_defaults(&compress);
	jpeg_set_quality(&compress, 92, TRUE);
	jpeg_start_compress(&compress, TRUE);
	for (row = 0; row < size; row++)
	{
		JSAMPROW line = (JSAMPROW)(rgb + (size_t)row * size * 3);

		jpeg_write_scanlines(&compress, &line, 1);
	}
	jpeg_finish_compress(&compress);
	jpeg_destroy_compress(&compress);
	fclose(file);
	return 1;
}

/* default.xbe's title image as <icon_path>; returns nonzero on success */
int host_icon_write(const char *xbe_path, const char *icon_path)
{
	FILE *file = fopen(xbe_path, "rb");
	unsigned char *xbe = NULL, *pixels, *icon;
	const unsigned char *image;
	uint32_t image_size = 0, width, height, x, y;
	long size;
	int result = 0;

	if (!file)
		return 0;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size > 0 && size < 64 * 1024 * 1024 && (xbe = malloc((size_t)size)) != NULL &&
		fread(xbe, 1, (size_t)size, file) != (size_t)size)
	{
		free(xbe);
		xbe = NULL;
	}
	fclose(file);
	if (!xbe)
		return 0;
	image = title_image(xbe, (size_t)size, &image_size);
	pixels = image ? decode_xpr(image, image_size, &width, &height) : NULL;
	if (!pixels)
	{
		host_logf(HOST_LOG_WARN, "icon: no title image in %s", xbe_path);
		free(xbe);
		return 0;
	}
	/* scaled to 256x256, bilinear */
	icon = malloc(ICON_SIZE * ICON_SIZE * 3);
	if (icon)
	{
		for (y = 0; y < ICON_SIZE; y++)
		{
			float fy = ((float)y + 0.5f) * (float)height / ICON_SIZE - 0.5f;
			int y0 = fy < 0 ? 0 : (int)fy, y1 = y0 + 1 < (int)height ? y0 + 1 : y0;
			float ty = fy < 0 ? 0.0f : fy - (float)y0;

			for (x = 0; x < ICON_SIZE; x++)
			{
				float fx = ((float)x + 0.5f) * (float)width / ICON_SIZE - 0.5f;
				int x0 = fx < 0 ? 0 : (int)fx, x1 = x0 + 1 < (int)width ? x0 + 1 : x0;
				float tx = fx < 0 ? 0.0f : fx - (float)x0;
				int channel;

				for (channel = 0; channel < 3; channel++)
				{
					float a = pixels[(y0 * width + x0) * 3 + channel], b = pixels[(y0 * width + x1) * 3 + channel];
					float c = pixels[(y1 * width + x0) * 3 + channel], d = pixels[(y1 * width + x1) * 3 + channel];
					float top = a + (b - a) * tx, bottom = c + (d - c) * tx;

					icon[(y * ICON_SIZE + x) * 3 + channel] = (unsigned char)(top + (bottom - top) * ty + 0.5f);
				}
			}
		}
		result = write_jpeg(icon_path, icon, ICON_SIZE);
		free(icon);
	}
	if (result)
		host_logf(HOST_LOG_INFO, "icon: %ux%u title image written as %s", width, height, icon_path);
	free(pixels);
	free(xbe);
	return result;
}
