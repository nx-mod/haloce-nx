/*
HOST_MJX.C

The picture decode of the guest's .mjx movie reader (port/switch/guest/platform/mjx.c,
docs/mjx_movies.md), done here because devkitPro's libjpeg is built for
this 64-bit side and the guest is ILP32. The decode itself is mjx.c's:
baseline JPEG read as raw YCbCr planes (jpeg_read_raw_data), no colour
conversion, into storage the guest owns (mjx_host.h).
*/

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <jpeglib.h>

#include "host.h"
#include "../guest/mjx_host.h"

struct mjx_host_errors
{
	struct jpeg_error_mgr manager;
	jmp_buf escape;
	struct mjx_host_layout *layout;
};

/* fatal: back to the setjmp, with the reason in the guest's message */
static void error_exit(j_common_ptr info)
{
	struct mjx_host_errors *errors = (struct mjx_host_errors *)info->err;

	if (info->err->msg_code)
		(*info->err->format_message)(info, errors->layout->message);
	else
		snprintf(errors->layout->message, sizeof(errors->layout->message), "libjpeg gave up for an unknown reason");
	longjmp(errors->escape, 1);
}

/* a warning would otherwise be fatal, turning a slightly damaged picture
into no picture at all (as mjx.c) */
static void no_message(j_common_ptr info)
{
	(void)info;
}

static void fail(struct mjx_host_layout *layout, const char *message)
{
	snprintf(layout->message, sizeof(layout->message), "%s", message);
}

long host_mjx_decode(unsigned int jpeg_address, unsigned int length, unsigned int layout_address)
{
	struct mjx_host_layout *layout = (struct mjx_host_layout *)(uintptr_t)layout_address;
	unsigned char *storage = (unsigned char *)(uintptr_t)layout->storage;
	struct jpeg_decompress_struct jpeg;
	struct mjx_host_errors errors;
	unsigned long needed = 0;
	int component;

	layout->message[0] = 0;
	jpeg.err = jpeg_std_error(&errors.manager);
	errors.manager.error_exit = error_exit;
	errors.manager.output_message = no_message;
	errors.layout = layout;
	if (setjmp(errors.escape))
	{
		jpeg_destroy_decompress(&jpeg);
		return -1;
	}
	jpeg_create_decompress(&jpeg);
	jpeg_mem_src(&jpeg, (const unsigned char *)(uintptr_t)jpeg_address, length);
	if (jpeg_read_header(&jpeg, TRUE) != JPEG_HEADER_OK)
	{
		fail(layout, "the picture's header could not be read");
		jpeg_destroy_decompress(&jpeg);
		return -1;
	}
	/* the three planes it already is: anything else would make libjpeg
	convert, which is both slower and lossy */
	jpeg.out_color_space = JCS_YCbCr;
	jpeg.raw_data_out = TRUE;
	jpeg_start_decompress(&jpeg);
	if (jpeg.num_components < 1 || jpeg.num_components > MJX_HOST_MAXIMUM_COMPONENTS)
	{
		fail(layout, "the picture has an unexpected number of components");
		jpeg_destroy_decompress(&jpeg);
		return -1;
	}
	layout->width = jpeg.output_width;
	layout->height = jpeg.output_height;
	layout->component_count = (uint32_t)jpeg.num_components;
	for (component = 0; component < jpeg.num_components; component++)
	{
		jpeg_component_info *source = &jpeg.comp_info[component];
		struct mjx_host_plane *plane = &layout->planes[component];

		/* the real size, and the size it is stored at: whole blocks */
		plane->offset = (uint32_t)needed;
		plane->width = (int32_t)source->downsampled_width;
		plane->height = (int32_t)source->downsampled_height;
		plane->stride = (int32_t)(source->width_in_blocks * DCTSIZE);
		needed += (unsigned long)plane->stride * (source->height_in_blocks * DCTSIZE);
	}
	if (needed > layout->storage_size || !storage)
	{
		jpeg_destroy_decompress(&jpeg);
		return (long)needed;
	}

	/* one iMCU row at a time, as the raw reader is meant to be driven */
	{
		int rows_per_call = jpeg.max_v_samp_factor * DCTSIZE;
		int mcu_row = 0;

		while (jpeg.output_scanline < jpeg.output_height)
		{
			JSAMPROW rows[MJX_HOST_MAXIMUM_COMPONENTS][DCTSIZE * 4];
			JSAMPARRAY planes[MJX_HOST_MAXIMUM_COMPONENTS];

			for (component = 0; component < jpeg.num_components; component++)
			{
				jpeg_component_info *source = &jpeg.comp_info[component];
				struct mjx_host_plane *plane = &layout->planes[component];
				unsigned char *samples = storage + plane->offset;
				int plane_rows = (int)(source->height_in_blocks * DCTSIZE);
				int first_row = mcu_row * source->v_samp_factor * DCTSIZE;
				int line;

				/* spare pointers past a short last block stay inside the plane */
				for (line = 0; line < DCTSIZE * 4; line++)
				{
					int row = first_row + line;

					rows[component][line] = row < plane_rows ? samples + (unsigned long)row * plane->stride : samples;
				}
				planes[component] = rows[component];
			}
			if (jpeg_read_raw_data(&jpeg, planes, rows_per_call) == 0)
			{
				fail(layout, "the picture ended before all of its rows were read");
				jpeg_destroy_decompress(&jpeg);
				return -1;
			}
			mcu_row++;
		}
	}
	jpeg_finish_decompress(&jpeg);
	jpeg_destroy_decompress(&jpeg);
	return 0;
}
