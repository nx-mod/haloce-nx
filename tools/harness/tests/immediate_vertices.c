/* D3DDevice_DrawVerticesImmediate against Begin, SetVertexData and End: the
real functions of d3d8_dk.c (deko3d, the Switch) or d3d8_vk.c (Vulkan,
Android and the PS5), with the draw they make captured (VULKAN picks). */
#include "harness.h"
#include <stdint.h>

#define WINAPI
#define CONST const
typedef int INT;
typedef unsigned long DWORD;
typedef float FLOAT;
typedef unsigned int UINT;
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef enum { D3DPT_TRIANGLELIST = 5, D3DPT_QUADLIST = 8 } D3DPRIMITIVETYPE;
#define D3DVSDE_VERTEX (-1)
#define XGPU_VERTEX_ATTRIBUTE_COUNT 16

#ifdef VULKAN
static struct
{
	BOOL immediate_active, fixed_function;
	D3DPRIMITIVETYPE immediate_type;
	float *immediate_vertices;
	unsigned long immediate_count, immediate_capacity, immediate_mask;
	unsigned immediate_columns;
	unsigned char immediate_registers[XGPU_VERTEX_ATTRIBUTE_COUNT];
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
} device;

/* the draw, as the vertex shader would read it: each vertex's registers (a
column's, or the register's value) */
static struct
{
	int draws;
	D3DPRIMITIVETYPE type;
	unsigned long count;
	float vertices[64][XGPU_VERTEX_ATTRIBUTE_COUNT][4];
} drawn;
static void draw_make(D3DPRIMITIVETYPE type, unsigned long count, BOOL immediate, const WORD *index_data, unsigned long start)
{
	unsigned long vertex, reg, column;

	drawn.draws++;
	drawn.type = type;
	drawn.count = count;
	for (vertex = 0; vertex < count && vertex < 64; vertex++)
	{
		for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
			memcpy(drawn.vertices[vertex][reg], device.attributes[reg], 4 * sizeof(float));
		for (column = 0; column < device.immediate_columns; column++)
			memcpy(drawn.vertices[vertex][device.immediate_registers[column]],
				device.immediate_vertices + (vertex * device.immediate_columns + column) * 4, 4 * sizeof(float));
	}
}
#else
#include "dk_commands.h"
#define STREAM_SIZE (1024 * 1024)
static struct
{
	BOOL immediate_active, fixed_function;
	D3DPRIMITIVETYPE immediate_type;
	float *immediate_vertices;
	unsigned long immediate_count, immediate_capacity, immediate_mask;
	unsigned long immediate_first[XGPU_VERTEX_ATTRIBUTE_COUNT];
	float immediate_begin[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
} device;
static struct { unsigned long immediate_draws, skipped_size; } stats;

/* the draw, as it goes to the host: its format and its command, bytes and all */
static struct
{
	int draws;
	struct dk_vertex_format format;
	unsigned long size;
	unsigned char command[1 << 16];
} drawn;
static BOOL draw_prepare(BOOL immediate) { return TRUE; }
static void fixed_function_transform(float *position) { position[0] *= 2.0f; position[3] = 0.5f; }
static void draw_format_send(const struct dk_vertex_format *format) { drawn.format = *format; }
static uint32_t primitive_of(D3DPRIMITIVETYPE type) { return (uint32_t)type; }
static void *stream_command(uint32_t type, unsigned long size)
{
	CHECK(size <= sizeof(drawn.command), "a command this test has room for");
	drawn.draws++;
	drawn.size = size;
	memset(drawn.command, 0, sizeof(drawn.command));
	return drawn.command;
}
static void draw_resources_used(BOOL indexed, BOOL streamed) {}
#endif

#include "under_test.inc"

/* a decal's vertices: registers 0 (position), 4 (texture coordinates), 9 (color) */
#define VERTICES 12
static float packed[VERTICES][12];

static void vertex_values(int vertex, float *position, float *texcoord, float *color)
{
	int i;
	for (i = 0; i < 4; i++)
	{
		position[i] = i == 3 ? 1.0f : (float)(vertex * 10 + i);
		texcoord[i] = i < 2 ? (float)(vertex + i) / 16.0f : (float)(i == 3);
		color[i] = (float)((vertex * 37 + i * 11) % 256) / 255.0f;
	}
}

static void draw_one_at_a_time(D3DPRIMITIVETYPE type, int count)
{
	int vertex;
	D3DDevice_Begin(type);
	for (vertex = 0; vertex < count; vertex++)
	{
		float position[4], texcoord[4], color[4];
		vertex_values(vertex, position, texcoord, color);
		/* (the decals' order: color, texture coordinates, then the position completes the vertex) */
		D3DDevice_SetVertexData4f(9, color[0], color[1], color[2], color[3]);
		D3DDevice_SetVertexData4f(4, texcoord[0], texcoord[1], texcoord[2], texcoord[3]);
		D3DDevice_SetVertexData4f(0, position[0], position[1], position[2], position[3]);
	}
	D3DDevice_End();
}

static void draw_packed(D3DPRIMITIVETYPE type, int count)
{
	int vertex;
	for (vertex = 0; vertex < count; vertex++)
		vertex_values(vertex, &packed[vertex][0], &packed[vertex][4], &packed[vertex][8]);
	D3DDevice_DrawVerticesImmediate(type, (1UL << 0) | (1UL << 4) | (1UL << 9), packed[0], count);
}

static void reset(void)
{
	memset(device.attributes, 0, sizeof(device.attributes));
	/* (a register the draw does not feed: a constant of it) */
	device.attributes[3][0] = 0.25f;
	device.attributes[3][3] = 1.0f;
	memset(&drawn, 0, sizeof(drawn));
}

static void compare(D3DPRIMITIVETYPE type, int count, boolean fixed_function)
{
	static float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	static unsigned char first[sizeof(drawn)];

	device.fixed_function = fixed_function;
	reset();
	draw_one_at_a_time(type, count);
	CHECK(drawn.draws == 1, "the calls one at a time draw (%d)", drawn.draws);
	memcpy(first, &drawn, sizeof(drawn));
	memcpy(attributes, device.attributes, sizeof(attributes));
	reset();
	draw_packed(type, count);
	CHECK(drawn.draws == 1, "the packed call draws once (%d)", drawn.draws);
	CHECK(!memcmp(first, &drawn, sizeof(drawn)), "the same draw, %d vertices%s", count,
		fixed_function ? ", fixed function" : "");
	CHECK(!memcmp(attributes, device.attributes, sizeof(attributes)), "the registers left as the last vertex left them");
	CHECK(!device.immediate_active, "and immediate mode ended");
}

int main(int argc, char **argv)
{
	compare(D3DPT_QUADLIST, 8, FALSE);
	/* (a quad list's vertices past its last whole quad are not drawn) */
	compare(D3DPT_QUADLIST, 10, FALSE);
	compare(D3DPT_TRIANGLELIST, 9, FALSE);
	compare(D3DPT_QUADLIST, 8, TRUE);
	/* without register 0 no vertex is complete: nothing is drawn */
	reset();
	D3DDevice_DrawVerticesImmediate(D3DPT_QUADLIST, 1UL << 4, packed[0], 4);
	CHECK(!drawn.draws, "no position, no draw");
	return 0;
}
