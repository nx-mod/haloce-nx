/*
RASTERIZER_XBOX_SCREEN_EFFECT.C

symbols in this file:
0015F720 01b0:
	_code_0015f720 (0000)
0015F8D0 0050:
	_code_0015f8d0 (0000)
0015F920 0090:
	_code_0015f920 (0000)
0015F9B0 0220:
	_code_0015f9b0 (0000)
0015FBD0 0060:
	_code_0015fbd0 (0000)
0015FC30 0010:
	_code_0015fc30 (0000)
0015FC40 0020:
	_code_0015fc40 (0000)
0015FC60 0010:
	_code_0015fc60 (0000)
0015FC70 0010:
	_code_0015fc70 (0000)
0015FC80 0010:
	_code_0015fc80 (0000)
0015FC90 0850:
	_rasterizer_screen_effect_set_texture_transforms (0000)
001604E0 0f30:
	__rasterizer_screen_effect (0000)
00161410 0540:
	__rasterizer_screen_flash (0000)
00292390 0019:
	??_C@_0BJ@BEKEHHEJ@v?9?$DOi?$CB?$DN0?40f?5?$CG?$CG?5v?9?$DOj?$CB?$DN0?40f?$AA@ (0000)
002923AC 0002:
	??_C@_01MHEDDDHA@v?$AA@ (0000)
002923B0 003f:
	??_C@_0DP@GDHJCEBP@c?3?2halo?2SOURCE?2rasterizer?2xbox?2r@ (0000)
002923F0 001b:
	??_C@_0BL@MEELOPHA@main_get_window_count?$CI?$CJ?$DM?$DN1?$AA@ (0000)
0029240C 0016:
	??_C@_0BG@OBFLACOO@?$CBparameters?9?$DOvideo_on?$AA@ (0000)
00292424 002a:
	??_C@_0CK@GCKMEOHN@?$CD?$CD?$CD?5ERROR?5rasterizer_screen_effe@ (0000)
00292450 0065:
	??_C@_0GF@LMEHBMJP@IDirect3DDevice8_SetVertexData2f@ (0000)
002924B8 0065:
	??_C@_0GF@CDJNJPAB@IDirect3DDevice8_SetVertexData2f@ (0000)
00292520 004a:
	??_C@_0EK@DDGAKMME@IDirect3DDevice8_SetVertexData2s@ (0000)
00292570 0065:
	??_C@_0GF@CCFPPFDG@IDirect3DDevice8_SetVertexData2f@ (0000)
002925D8 0058:
	??_C@_0FI@MCHFKFGN@IDirect3DDevice8_SetVertexData2s@ (0000)
00292630 0065:
	??_C@_0GF@LNIFHGKI@IDirect3DDevice8_SetVertexData2f@ (0000)
00292698 004b:
	??_C@_0EL@FFKPDIEL@IDirect3DDevice8_SetVertexData2s@ (0000)
002926E4 0004:
	__real@3b088889 (0000)
002926E8 0004:
	__real@3acccccd (0000)
002926EC 0033:
	??_C@_0DD@IJEPJHCB@combiner_count?$DM?$DNRASTERIZER_MAXIM@ (0000)
00292720 003a:
	??_C@_0DK@EJBEKMFA@?$CD?$CD?$CD?5ERROR?5video?5effect?5tried?5to?5@ (0000)
00292760 0084:
	??_C@_0IE@LDDMKLNC@parameters?9?$DOvideo_overbright_mod@ (0000)
002927E8 0044:
	??_C@_0EE@MEDEOJLI@?$CD?$CD?$CD?5ERROR?5non?9convolution?5effect@ (0000)
0029282C 0032:
	??_C@_0DC@CMADNDPH@video?5effect?5noise?5map?5cannot?5be@ (0000)
00292860 0008:
	??_C@_07MACHECCK@pass?$DN?$DN1?$AA@ (0000)
00292868 0008:
	??_C@_07NJDMHDGL@pass?$DN?$DN0?$AA@ (0000)
00292870 002c:
	??_C@_0CM@OFCEIJKA@blur?5effect?5cannot?5specify?5convo@ (0000)
0029289C 002d:
	??_C@_0CN@EJCBMOOJ@video?5effect?5cannot?5specify?5conv@ (0000)
002928CC 0037:
	??_C@_0DH@DCBDBJNG@video?5effect?5cannot?5specify?5nonz@ (0000)
00292904 002d:
	??_C@_0CN@CNLJNHLN@video?5effect?5cannot?5specify?5conv@ (0000)
00292934 0035:
	??_C@_0DF@FNIELJAM@video?5effect?5cannot?5specify?5extr@ (0000)
0029296C 0024:
	??_C@_0CE@MIPIKB@video?5effect?5must?5specify?5noise?5@ (0000)
00292990 0027:
	??_C@_0CH@KIKPHJLK@video?5effect?5must?5specify?5scanli@ (0000)
002929B8 0028:
	??_C@_0CI@PFHEHAHJ@?$CD?$CD?$CD?5ERROR?5unsupported?5screen?5fla@ (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "cseries/errors.h"
#include "interface/hud_draw.h"
#include "bitmaps/bitmaps_inlines.h"
#include "main/main.h"
#include "math/integer_math.h"
#include "math/real_math.h"
#include "render/render_cameras.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_cinematics.h"
#include "rasterizer/rasterizer_console_vars.h"
#include <stddef.h>
#include <xtl.h>
#include "rasterizer_xbox.h"
#include "rasterizer_xbox_pixel_shader.h"

/* ---------- constants */

enum
{
	_render_screen_flash_type_none = 0,
	_render_screen_flash_type_lighten,
	_render_screen_flash_type_darken,
	_render_screen_flash_type_max,
	_render_screen_flash_type_min,
	_render_screen_flash_type_invert,
	_render_screen_flash_type_tint,
	NUMBER_OF_SCREEN_FLASH_TYPES
};

enum
{
	_rasterizer_screen_effect_convolution_type_none = 0,
	_rasterizer_screen_effect_convolution_type_blur,
	_rasterizer_screen_effect_convolution_type_warp,
	NUMBER_OF_RASTERIZER_SCREEN_EFFECT_CONVOLUTION_TYPES
};

enum
{
	NUMBER_OF_RASTERIZER_SCREEN_EFFECT_VIDEO_OVERBRIGHT_MODES = 3
};

enum
{
	RASTERIZER_MAXIMUM_COMBINER_STAGES = 8
};

enum
{
	_rasterizer_target_render_primary = 0,
	_rasterizer_target_screen_effect = 7,
	NUMBER_OF_RASTERIZER_TARGETS = 8
};

enum
{
	_bitmap_type_2d = 0,
	_bitmap_linear_bit = 4
};

enum
{
	_rasterizer_vertex_shader_screen = 4,
	_rasterizer_vertex_shader_screen_effect = 38,
	_rasterizer_vertex_type_screen = 8,
	_rasterizer_vertex_shader_permutation_default = 0,
	_rasterizer_vertex_register_position = 0
};

/* ---------- macros */

/* ---------- structures */

typedef char rasterizer_screen_effect_debug_options_flashes_offset_assert[
	offsetof(struct rasterizer_debug_options, screen_flash_enabled) == 0x47 ? 1 : -1];
typedef char rasterizer_screen_effect_debug_options_effects_offset_assert[
	offsetof(struct rasterizer_debug_options, screen_effects_enabled) == 0x48 ? 1 : -1];
typedef char rasterizer_screen_effect_parameters_mask_offset_assert[
	offsetof(struct rasterizer_cinematic_screen_effect_parameters, convolution_mask) == 0x08 ? 1 : -1];
typedef char rasterizer_screen_effect_parameters_tint_offset_assert[
	offsetof(struct rasterizer_cinematic_screen_effect_parameters, filter_desaturation_tint) == 0x14 ? 1 : -1];
typedef char rasterizer_screen_effect_parameters_video_offset_assert[
	offsetof(struct rasterizer_cinematic_screen_effect_parameters, video_on) == 0x23 ? 1 : -1];
typedef char rasterizer_screen_effect_parameters_scanline_offset_assert[
	offsetof(struct rasterizer_cinematic_screen_effect_parameters, video_scanline_map) == 0x28 ? 1 : -1];
typedef char rasterizer_screen_effect_parameters_noise_offset_assert[
	offsetof(struct rasterizer_cinematic_screen_effect_parameters, video_noise_map) == 0x34 ? 1 : -1];
typedef char rasterizer_screen_effect_window_viewport_offset_assert[
	offsetof(struct rasterizer_window_begin_parameters, camera.viewport_bounds) == 0x34 ? 1 : -1];
typedef char rasterizer_screen_effect_window_bounds_offset_assert[
	offsetof(struct rasterizer_window_begin_parameters, camera.window_bounds) == 0x3C ? 1 : -1];
typedef char rasterizer_screen_effect_window_flash_offset_assert[
	offsetof(struct rasterizer_window_begin_parameters, screen_flash) == 0x238 ? 1 : -1];
typedef char rasterizer_screen_effect_pixel_shader_size_assert[
	sizeof(struct pixel_shader_definition) == 0xF0 ? 1 : -1];

/* ---------- globals */


/* ---------- private code */

static real_vector2d vector2d_reciprocal(
	real_vector2d const *v)
{
	real_vector2d reciprocal;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
		30,
		v);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
		31,
		v->i!=0.0f && v->j!=0.0f);

	reciprocal.i = 1.0f / v->i;
	reciprocal.j = 1.0f / v->j;

	return reciprocal;
}

static void rasterizer_screen_effect_set_texture_transforms(
	struct rasterizer_cinematic_screen_effect_parameters const *parameters,
	short pass,
	short pass_count,
	real (*port_constants)[4])
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
		47,
		parameters);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
		48,
		global_d3d_device);

	{
		struct bitmap_data viewport_map =
		{
			BITMAP_GROUP_TAG,
			global_window_parameters.camera.viewport_bounds.x1 - global_window_parameters.camera.viewport_bounds.x0,
			global_window_parameters.camera.viewport_bounds.y1 - global_window_parameters.camera.viewport_bounds.y0,
			1,
			_bitmap_type_2d,
			NONE,
			FLAG(_bitmap_linear_bit)
		};
		real constants[8][4];
		real_vector2d convolution_size;
		real_vector2d screen_size;
		real_vector2d scanline_size;
		real_vector2d noise_size;
		real_vector2d screen_scale;
		real_vector2d default_scale;
		real_vector2d noise_scale;
		real_vector2d scanline_scale;
		real_vector2d convolution_scale;
		struct bitmap_data const *scanline_bitmap;
		struct bitmap_data const *noise_bitmap;
		struct bitmap_data const *convolution_bitmap;
		short x_offset;
		short y_offset;

		if (parameters->convolution_mask &&
			(pass > 0 ||
			pass_count == 1 ||
			parameters->convolution_type != _rasterizer_screen_effect_convolution_type_none))
			convolution_bitmap = parameters->convolution_mask;
		else
			convolution_bitmap = &viewport_map;

		if (parameters->video_on)
			scanline_bitmap = parameters->video_scanline_map;
		else
			scanline_bitmap = &viewport_map;

		if (parameters->video_on)
			noise_bitmap = parameters->video_noise_map;
		else
			noise_bitmap = &viewport_map;

		convolution_size.i = convolution_bitmap->width;
		convolution_size.j = convolution_bitmap->height;
		scanline_size.i = scanline_bitmap->width;
		scanline_size.j = scanline_bitmap->height;
		noise_size.i = noise_bitmap->width;
		noise_size.j = noise_bitmap->height;
		screen_size.i = viewport_map.width;
		screen_size.j = viewport_map.height;

		default_scale.i = 1.0f;
		default_scale.j = 1.0f;

		convolution_scale = TEST_FLAG(convolution_bitmap->flags, _bitmap_linear_bit) ?
			default_scale : vector2d_reciprocal(&convolution_size);

		scanline_scale = TEST_FLAG(scanline_bitmap->flags, _bitmap_linear_bit) ?
			default_scale : vector2d_reciprocal(&scanline_size);

		noise_scale = TEST_FLAG(noise_bitmap->flags, _bitmap_linear_bit) ?
			default_scale : vector2d_reciprocal(&noise_size);

		screen_scale = TEST_FLAG(viewport_map.flags, _bitmap_linear_bit) ?
			default_scale : vector2d_reciprocal(&screen_size);

		constants[0][0] = convolution_scale.i;
		constants[0][1] = 0.0f;
		constants[0][2] = 0.0f;
		constants[0][3] = (convolution_size.i + 1.0f - viewport_map.width) * convolution_scale.i * 0.5f;
		constants[1][0] = 0.0f;
		constants[1][1] = convolution_scale.j;
		constants[1][2] = 0.0f;
		constants[1][3] = (convolution_size.j + 1.0f - viewport_map.height) * convolution_scale.j * 0.5f;
		constants[2][0] = scanline_scale.i;
		constants[2][1] = 0.0f;
		constants[2][2] = 0.0f;
		constants[2][3] = (scanline_size.i + 1.0f - viewport_map.width) * scanline_scale.i * 0.5f;
		constants[3][0] = 0.0f;
		constants[3][1] = scanline_scale.j;
		constants[3][2] = 0.0f;
		constants[3][3] = (scanline_size.j + 1.0f - viewport_map.height) * scanline_scale.j * 0.5f;
		constants[4][0] = noise_scale.i;
		constants[4][1] = 0.0f;
		constants[4][2] = 0.0f;
		constants[4][3] = (noise_size.i + 1.0f - viewport_map.width) * noise_scale.i * 0.5f;
		constants[5][0] = 0.0f;
		constants[5][1] = noise_scale.j;
		constants[5][2] = 0.0f;
		constants[5][3] = (noise_size.j + 1.0f - viewport_map.height) * noise_scale.j * 0.5f;
		constants[6][0] = screen_scale.i;
		constants[6][1] = 0.0f;
		constants[6][2] = 0.0f;
		constants[6][3] = (screen_size.i + 1.0f - viewport_map.width) * screen_scale.i * 0.5f;
		constants[7][0] = 0.0f;
		constants[7][1] = screen_scale.j;
		constants[7][2] = 0.0f;
		constants[7][3] = (screen_size.j + 1.0f - viewport_map.height) * screen_scale.j * 0.5f;

		x_offset = (global_window_parameters.camera.viewport_bounds.x0 +
			global_window_parameters.camera.viewport_bounds.x1) / 2 -
			(global_window_parameters.camera.window_bounds.x1 +
			global_window_parameters.camera.window_bounds.x0) / 2;
		y_offset = (global_window_parameters.camera.viewport_bounds.y0 +
			global_window_parameters.camera.viewport_bounds.y1) / 2 -
			(global_window_parameters.camera.window_bounds.y1 +
			global_window_parameters.camera.window_bounds.y0) / 2;

		if (convolution_bitmap == &viewport_map)
		{
			constants[0][3] += global_window_parameters.camera.viewport_bounds.x0;
			constants[1][3] += global_window_parameters.camera.viewport_bounds.y0;
		}
		else if (convolution_bitmap == parameters->convolution_mask)
		{
			constants[0][3] += x_offset * convolution_scale.i;
			constants[1][3] += y_offset * convolution_scale.j;
		}

		if (scanline_bitmap == &viewport_map)
		{
			constants[2][3] += global_window_parameters.camera.viewport_bounds.x0;
			constants[3][3] += global_window_parameters.camera.viewport_bounds.y0;
		}
		else if (scanline_bitmap == parameters->convolution_mask)
		{
			constants[2][3] += x_offset * scanline_scale.i;
			constants[3][3] += y_offset * scanline_scale.j;
		}

		if (parameters->convolution_type == _rasterizer_screen_effect_convolution_type_blur)
		{
			real convolution_radius = parameters->convolution_radius;

			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				176,
				!parameters->video_on);
			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				177,
				main_get_window_count()<=1);

			constants[0][3] += parameters->convolution_mask ?
				0.0f : convolution_scale.i * convolution_radius;
			constants[1][3] += parameters->convolution_mask ?
				0.0f : convolution_scale.j * convolution_radius;
			constants[2][3] -= scanline_scale.i * convolution_radius;
			constants[3][3] -= scanline_scale.j * convolution_radius;
			constants[4][3] += noise_scale.i * convolution_radius;
			constants[5][3] -= noise_scale.j * convolution_radius;
			constants[6][3] -= screen_scale.i * convolution_radius;
			constants[7][3] += screen_scale.j * convolution_radius;
		}
		else if (parameters->convolution_type == _rasterizer_screen_effect_convolution_type_warp)
		{
			real convolution_radius = parameters->convolution_radius;
			real convolution_offset = parameters->convolution_mask ?
				0.0f : -convolution_radius;
			real scanline_offset = 0.0f;
			real noise_offset = convolution_radius;
			real screen_offset = parameters->convolution_mask ?
				-convolution_radius : convolution_radius * 2.0f;

			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				198,
				!parameters->video_on);
			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				199,
				main_get_window_count()<=1);

			constants[0][0] = (1.0f - convolution_offset / convolution_size.i) * constants[0][0];
			constants[1][1] = (1.0f - convolution_offset / convolution_size.j) * constants[1][1];
			constants[2][0] = (1.0f - scanline_offset / scanline_size.i) * constants[2][0];
			constants[3][1] = (1.0f - scanline_offset / scanline_size.j) * constants[3][1];
			constants[4][0] = (1.0f - noise_offset / noise_size.i) * constants[4][0];
			constants[5][1] = (1.0f - noise_offset / noise_size.j) * constants[5][1];
			constants[6][0] = (1.0f - screen_offset / screen_size.i) * constants[6][0];
			constants[7][1] = (1.0f - screen_offset / screen_size.j) * constants[7][1];
			constants[0][3] += convolution_offset * 0.5f;
			constants[1][3] += convolution_offset * 0.5f;
			constants[2][3] += scanline_offset * 0.5f;
			constants[3][3] += scanline_offset * 0.5f;
			constants[4][3] += noise_offset * 0.5f;
			constants[5][3] += noise_offset * 0.5f;
			constants[6][3] += screen_offset * 0.5f;
			constants[7][3] += screen_offset * 0.5f;
		}
		else if (pass == 1 && parameters->video_on)
		{
			real random_value;

			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				223,
				main_get_window_count()<=1);

			/* The native builds draw several frames per tick
			(port/linux/game/render_interpolation.c): move the noise 30 times
			a second, as the Xbox did once a frame, not every frame. */
			{
				unsigned long noise_seed =
					(unsigned long)system_milliseconds() / (MILLISECONDS_PER_SECOND / TICKS_PER_SECOND) * 0x9E3779B1UL;

				noise_seed ^= noise_seed >> 16;
				noise_seed *= 0x7FEB352DUL;
				noise_seed ^= noise_seed >> 15;
				random_value = real_seed_random(&noise_seed);
				constants[4][3] += noise_scale.i * random_value * noise_size.i;
				random_value = real_seed_random(&noise_seed);
				constants[5][3] += noise_scale.j * random_value * noise_size.j;
			}
		}

		/* port: or handed back (rasterizer_screen_effect_port_fast_warp) */
		if (port_constants)
			csmemcpy(port_constants, constants, sizeof(constants));
		else
			IDirect3DDevice8_SetVertexShaderConstant(
				global_d3d_device,
				-81,
				constants,
				8);
	}

	return;
}

/* port: the zoom's warp (a convolution mask and the warp convolution, as the
sniper rifle's and the pistol's zoom draw) drawn small on the Switch, unless
debug.original_zoom is set (halo_screen_fast_warp). The game's passes each read the whole
screen four times and write it once, and the native builds draw several
times the passes (the steps in _rasterizer_screen_effect): 5 ms of a 720p
frame's GPU time. Here the screen is copied to a target of about 360 lines,
the game's own passes (as many as the Xbox drew, with its own spread) blur
that copy, and one pass over the screen blends the blur over it
(halo_screen_warp_blend's shader, port/switch/guest/nv2a_psh_dk.c): the
screen stays sharp where the warp spreads a pixel little (towards its
centre, where the player aims), the blur is drawn where it spreads it more,
and the whole is darkened by the mask, as the game's last pass does. FALSE for
anything else, drawn as the game draws it. */
enum
{
	RASTERIZER_SCREEN_EFFECT_PORT_WARP_LINES = 360,
	NUMBER_OF_RASTERIZER_SCREEN_EFFECT_PORT_WARP_TARGETS = 3
};

/* the warp's spread, in the small copy's texels, the screen stays sharp up
to, and the spread from which the blur is drawn wholly (eased between): at
720p the inside of a scope spreads by about half a texel at its rim */
#define RASTERIZER_SCREEN_EFFECT_PORT_WARP_SHARP 0.5f
#define RASTERIZER_SCREEN_EFFECT_PORT_WARP_BLURRED 1.5f

static D3DTexture *rasterizer_screen_effect_port_warp_textures[NUMBER_OF_RASTERIZER_SCREEN_EFFECT_PORT_WARP_TARGETS];
static D3DSurface *rasterizer_screen_effect_port_warp_surfaces[NUMBER_OF_RASTERIZER_SCREEN_EFFECT_PORT_WARP_TARGETS];
static short rasterizer_screen_effect_port_warp_width;
static short rasterizer_screen_effect_port_warp_height;
static boolean rasterizer_screen_effect_port_warp_failed;

/* the targets, made at a size the first time it is asked for */
static boolean rasterizer_screen_effect_port_warp_targets(
	short width,
	short height)
{
	short index;

	if (rasterizer_screen_effect_port_warp_failed)
		return FALSE;
	if (width == rasterizer_screen_effect_port_warp_width &&
		height == rasterizer_screen_effect_port_warp_height)
		return TRUE;

	for (index = 0; index < NUMBER_OF_RASTERIZER_SCREEN_EFFECT_PORT_WARP_TARGETS; index++)
	{
		if (rasterizer_screen_effect_port_warp_surfaces[index])
			IDirect3DSurface8_Release(rasterizer_screen_effect_port_warp_surfaces[index]);
		if (rasterizer_screen_effect_port_warp_textures[index])
			IDirect3DTexture8_Release(rasterizer_screen_effect_port_warp_textures[index]);
		rasterizer_screen_effect_port_warp_surfaces[index] = NULL;
		rasterizer_screen_effect_port_warp_textures[index] = NULL;
	}
	rasterizer_screen_effect_port_warp_width = 0;
	rasterizer_screen_effect_port_warp_height = 0;

	for (index = 0; index < NUMBER_OF_RASTERIZER_SCREEN_EFFECT_PORT_WARP_TARGETS; index++)
	{
		D3DTexture *texture = NULL;
		D3DSurface *surface = NULL;

		if (IDirect3DDevice8_CreateTexture(global_d3d_device, width, height, 1, D3DUSAGE_RENDERTARGET,
				D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT, &texture) < 0 || !texture ||
			IDirect3DTexture8_GetSurfaceLevel(texture, 0, &surface) < 0 || !surface)
		{
			error(_error_silent, "### WARNING cannot make the zoom's %dx%d targets: drawn as the game draws it",
				width, height);
			rasterizer_screen_effect_port_warp_failed = TRUE;
			return FALSE;
		}
		rasterizer_screen_effect_port_warp_textures[index] = texture;
		rasterizer_screen_effect_port_warp_surfaces[index] = surface;
	}
	rasterizer_screen_effect_port_warp_width = width;
	rasterizer_screen_effect_port_warp_height = height;
	return TRUE;
}

/* one quad over the target, its texture coordinates the viewport's game
pixels, as _rasterizer_screen_effect draws its passes */
static void rasterizer_screen_effect_port_quad(
	short width,
	short height)
{
	IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLEFAN);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, height);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, _rasterizer_vertex_register_position, -1.0f, -1.0f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, width, height);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, _rasterizer_vertex_register_position, 1.0f, -1.0f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, width, 0);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, _rasterizer_vertex_register_position, 1.0f, 1.0f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 0);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, _rasterizer_vertex_register_position, -1.0f, 1.0f);
	IDirect3DDevice8_End(global_d3d_device);
}

static void rasterizer_screen_effect_port_sampler(
	short stage)
{
	IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_MIPFILTER, D3DTEXF_POINT);
}

/* a small target to draw into */
static void rasterizer_screen_effect_port_warp_target(
	short index)
{
	D3DVIEWPORT8 viewport;

	IDirect3DDevice8_SetRenderTarget(global_d3d_device, rasterizer_screen_effect_port_warp_surfaces[index], NULL);
	viewport.X = 0;
	viewport.Y = 0;
	viewport.Width = rasterizer_screen_effect_port_warp_width;
	viewport.Height = rasterizer_screen_effect_port_warp_height;
	viewport.MinZ = 0.0f;
	viewport.MaxZ = 1.0f;
	IDirect3DDevice8_SetViewport(global_d3d_device, &viewport);
}

static boolean rasterizer_screen_effect_port_fast_warp(
	struct rasterizer_cinematic_screen_effect_parameters const *parameters)
{
	real constants[8][4];
	real scale_x;
	real scale_y;
	real screen_scale;
	short viewport_width;
	short viewport_height;
	short width;
	short height;
	short divisor;
	short pass_count;
	short pass;
	short source;
	short destination;
	short row;

	if (!halo_screen_fast_warp() ||
		parameters->convolution_type != _rasterizer_screen_effect_convolution_type_warp ||
		!parameters->convolution_mask ||
		parameters->video_on ||
		parameters->filter_light_enhancement_intensity > 0.0f ||
		parameters->filter_desaturation_intensity > 0.0f ||
		main_get_window_count() > 1)
		return FALSE;

	viewport_width = global_window_parameters.camera.viewport_bounds.x1 -
		global_window_parameters.camera.viewport_bounds.x0;
	viewport_height = global_window_parameters.camera.viewport_bounds.y1 -
		global_window_parameters.camera.viewport_bounds.y0;
	screen_scale = halo_screen_scale();
	divisor = (short)(viewport_height * screen_scale / RASTERIZER_SCREEN_EFFECT_PORT_WARP_LINES + 0.5f);
	if (viewport_width <= 0 || viewport_height <= 0 || divisor < 2)
		return FALSE;
	width = (short)(viewport_width * screen_scale / divisor + 0.5f);
	height = (short)(viewport_height * screen_scale / divisor + 0.5f);
	if (!rasterizer_screen_effect_port_warp_targets(width, height))
		return FALSE;
	/* the small targets' texels to the viewport's game pixel */
	scale_x = (real)width / (real)viewport_width;
	scale_y = (real)height / (real)viewport_height;
	pass_count = (parameters->convolution_extra_passes + 1) * 2;
	if (pass_count > 15)
		pass_count = 15;

	rasterizer_set_vertex_shader_permutation(
		_rasterizer_vertex_shader_screen_effect,
		_rasterizer_vertex_type_screen,
		_rasterizer_vertex_shader_permutation_default);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_NONE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHABLENDENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHATESTENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZBIAS, 0);

	/* the screen copied small (target 0): each texel the screen where its
	centre falls, so the passes' samples land where the game's do */
	csmemset(constants, 0, sizeof(constants));
	constants[0][0] = 1.0f;
	constants[0][3] = global_window_parameters.camera.viewport_bounds.x0;
	constants[1][1] = 1.0f;
	constants[1][3] = global_window_parameters.camera.viewport_bounds.y0;
	IDirect3DDevice8_SetVertexShaderConstant(global_d3d_device, -81, constants, 8);
	rasterizer_set_target_as_texture(0, _rasterizer_target_render_primary, FALSE);
	rasterizer_screen_effect_port_sampler(0);
	IDirect3DDevice8_SetTexture(global_d3d_device, 1, NULL);
	IDirect3DDevice8_SetTexture(global_d3d_device, 2, NULL);
	IDirect3DDevice8_SetTexture(global_d3d_device, 3, NULL);
	csmemset(&pixel_shader, 0, sizeof(pixel_shader));
	pixel_shader.texture_modes = 0x00000001;
	pixel_shader.rgb_inputs[0] = 0x08200000;
	pixel_shader.rgb_outputs[0] = 0x000000c0;
	pixel_shader.final_combiner_inputs_abcd = 0x0000000c;
	pixel_shader.combiner_count = 1 | 0x00011000;
	rasterizer_set_pixel_shader(&pixel_shader);
	rasterizer_screen_effect_port_warp_target(0);
	rasterizer_screen_effect_port_quad(viewport_width, viewport_height);

	/* the game's passes over the copy, from target 0 to 1, then between 1
	and 2: its texture coordinates in the small targets' texels, without the
	half pixel the Xbox's pixel centres needed (here the centres are on the
	half pixel already, and a half texel more would move the blur off the
	copy it is put back over) */
	source = 0;
	destination = 1;
	for (pass = 0; pass < pass_count; pass++)
	{
		rasterizer_screen_effect_set_texture_transforms(parameters, pass, pass_count, constants);
		constants[2][3] -= global_window_parameters.camera.viewport_bounds.x0;
		constants[3][3] -= global_window_parameters.camera.viewport_bounds.y0;
		for (row = 2; row < 8; row++)
		{
			real scale = (row & 1) ? scale_y : scale_x;

			constants[row][3] -= 0.5f;
			constants[row][0] *= scale;
			constants[row][1] *= scale;
			constants[row][2] *= scale;
			constants[row][3] *= scale;
		}
		IDirect3DDevice8_SetVertexShaderConstant(global_d3d_device, -81, constants, 8);

		rasterizer_set_texture_bitmap_data(0, parameters->convolution_mask);
		rasterizer_screen_effect_port_sampler(0);
		for (row = 1; row < 4; row++)
		{
			IDirect3DDevice8_SetTexture(global_d3d_device, row,
				(IDirect3DBaseTexture8 *)rasterizer_screen_effect_port_warp_textures[source]);
			rasterizer_screen_effect_port_sampler(row);
		}

		csmemset(&pixel_shader, 0, sizeof(pixel_shader));
		pixel_shader.texture_modes = 0x00008421;
		pixel_shader.rgb_inputs[0] = 0x89208a20;
		pixel_shader.rgb_outputs[0] = 0x00000c00;
		pixel_shader.constant_0[1] = real_alpha_to_pixel32(1.0f / 3.0f);
		pixel_shader.rgb_inputs[1] = 0xcc118b11;
		pixel_shader.rgb_outputs[1] = 0x00000c00;
		pixel_shader.rgb_inputs[2] = 0xcc20a020;
		pixel_shader.rgb_outputs[2] = 0x00000c00;
		pixel_shader.rgb_inputs[3] = 0x3809180c;
		pixel_shader.rgb_outputs[3] = 0x00000c00;
		pixel_shader.final_combiner_inputs_abcd = 0x0000000c;
		pixel_shader.final_combiner_inputs_efg = 0x00000800;
		pixel_shader.combiner_count = 4 | 0x00011000;
		rasterizer_set_pixel_shader(&pixel_shader);
		rasterizer_screen_effect_port_warp_target(destination);
		rasterizer_screen_effect_port_quad(viewport_width, viewport_height);

		source = destination;
		destination = destination == 1 ? 2 : 1;
	}

	/* the blend over the screen: the mask in stage 0, as the passes read it,
	the blur in 1 where the screen's pixel is, and in texture coordinate 3 the
	pixel's way from the centre, scaled to the blur's spread there with the
	mask's alpha at 1. A pass moves the two warped copies it mixes in by the
	pixel's way from the centre times the radius over the viewport's size,
	each with a third of the mask's alpha, m: over the passes the spread is
	about that times the root of 2/3 m passes. */
	rasterizer_screen_effect_set_texture_transforms(parameters, pass_count - 1, pass_count, constants);
	for (row = 2; row < 8; row++)
	{
		constants[row][0] = 0.0f;
		constants[row][1] = 0.0f;
		constants[row][2] = 0.0f;
		constants[row][3] = 0.0f;
	}
	constants[2][0] = scale_x;
	constants[3][1] = scale_y;
	{
		real spread_x = parameters->convolution_radius / viewport_width *
			square_root(pass_count * 2.0f / 3.0f);
		real spread_y = parameters->convolution_radius / viewport_height *
			square_root(pass_count * 2.0f / 3.0f);

		constants[6][0] = scale_x * spread_x;
		constants[6][3] = -scale_x * spread_x * viewport_width * 0.5f;
		constants[7][1] = scale_y * spread_y;
		constants[7][3] = -scale_y * spread_y * viewport_height * 0.5f;
	}
	IDirect3DDevice8_SetVertexShaderConstant(global_d3d_device, -81, constants, 8);
	rasterizer_set_texture_bitmap_data(0, parameters->convolution_mask);
	rasterizer_screen_effect_port_sampler(0);
	IDirect3DDevice8_SetTexture(global_d3d_device, 1,
		(IDirect3DBaseTexture8 *)rasterizer_screen_effect_port_warp_textures[source]);
	rasterizer_screen_effect_port_sampler(1);
	IDirect3DDevice8_SetTexture(global_d3d_device, 2, NULL);
	IDirect3DDevice8_SetTexture(global_d3d_device, 3, NULL);

	csmemset(&pixel_shader, 0, sizeof(pixel_shader));
	pixel_shader.texture_modes = 0x00000021;
	pixel_shader.rgb_inputs[0] = 0x08200000;
	pixel_shader.rgb_outputs[0] = 0x000000c0;
	pixel_shader.final_combiner_inputs_abcd = 0x0000000c;
	/* the spreads, in the small copy's texels over 4, the screen is sharp
	up to (red) and wholly blurred from (green): sharp past the inside of
	the sniper rifle's and pistol's scopes, where the player aims */
	pixel_shader.constant_0[0] =
		((pixel32)(RASTERIZER_SCREEN_EFFECT_PORT_WARP_SHARP / 4.0f * 255.0f + 0.5f) << 16) |
		((pixel32)(RASTERIZER_SCREEN_EFFECT_PORT_WARP_BLURRED / 4.0f * 255.0f + 0.5f) << 8);
	pixel_shader.combiner_count = 1 | 0x00011000;
	rasterizer_set_pixel_shader(&pixel_shader);
	rasterizer_set_target(_rasterizer_target_render_primary, FALSE, FALSE, FALSE, FALSE);
	SetRenderStateSmart(D3DRS_ALPHABLENDENABLE, TRUE);
	SetRenderStateSmart(D3DRS_SRCBLEND, D3DBLEND_ONE);
	SetRenderStateSmart(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA);
	SetRenderStateSmart(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	halo_screen_warp_blend(TRUE);
	rasterizer_screen_effect_port_quad(viewport_width, viewport_height);
	halo_screen_warp_blend(FALSE);

	return TRUE;
}

/* port: a blur (the convolution's blur, or its warp without a mask: the
"dazed" screens cutscenes draw, the same passes of the same shader) drawn small,
on the Switch, unless debug.original_blur is set (halo_screen_fast_blur).
Each of its passes averages four copies of the screen, apart diagonally by
its radius; the native builds drew several times the passes at the
display's resolution (the steps in _rasterizer_screen_effect), up to 24
passes of 1080p in a cutscene, 39 ms of a frame. Here the game's own passes,
as many as the Xbox drew with its radius, go through the targets of the
zoom's warp (about 360 lines): the first reads the screen and writes a small
target, the ones between go from one small target to the other, and the last
reads the small target and draws over the screen with the game's own last
pass (its filters and its blend). Only where the blur spreads a pixel over
half of the small targets' texels or more: a blur slighter than that is
drawn as the game draws it, as the small targets would lose what it keeps
(a cutscene's blur growing from nothing is so for a few frames, which drawn
as the game draws them, 24 passes of 1080p, were 57 ms each). TRUE with the
scale from the viewport's game pixels to the small targets' texels. */
#define RASTERIZER_SCREEN_EFFECT_PORT_BLUR_LEAST_SPREAD 0.5f
/* port: the spread a blur keeps over the small targets' texels, where the screen has the lines to draw them larger: on
a screen of many times the Xbox's lines (a phone's 1848, the targets of 360 lines a fifth of it), a blur fading out
(a10's waking up, from a radius of 10 to nothing over five seconds) drawn small showed the small target's texels, a
fifth of the screen's resolution, as if the screen had lost its anti-aliasing until the blur was over. The targets
are a third, then half the screen's lines for a blur too slight to hide the smaller ones' texels (a spread of at least
this many), and half the screen's, as before, down to the least spread */
#define RASTERIZER_SCREEN_EFFECT_PORT_BLUR_HIDING_SPREAD 2.0f

/* (the port's, port/linux/src/sdl_platform.c) */
void platform_log(char const *format, ...);

static boolean rasterizer_screen_effect_port_small_blur(
	struct rasterizer_cinematic_screen_effect_parameters const *parameters,
	real *scale_x,
	real *scale_y)
{
	/* (said when how it is drawn changes, not its radius: a blur that fades changes its radius every frame, and Lumoria's
	logged a line a frame) */
	static short logged_type = -1;
	static short logged_passes = -1;
	static short logged_small = -1;
	real screen_scale;
	real spread;
	short viewport_width;
	short viewport_height;
	short width;
	short height;
	short divisor;
	short pass_count;
	boolean small;

	if (!halo_screen_fast_blur() ||
		(parameters->convolution_type != _rasterizer_screen_effect_convolution_type_blur &&
		(parameters->convolution_type != _rasterizer_screen_effect_convolution_type_warp ||
		parameters->convolution_mask)) ||
		parameters->video_on ||
		main_get_window_count() > 1)
		return FALSE;

	viewport_width = global_window_parameters.camera.viewport_bounds.x1 -
		global_window_parameters.camera.viewport_bounds.x0;
	viewport_height = global_window_parameters.camera.viewport_bounds.y1 -
		global_window_parameters.camera.viewport_bounds.y0;
	screen_scale = halo_screen_scale();
	divisor = (short)(viewport_height * screen_scale / RASTERIZER_SCREEN_EFFECT_PORT_WARP_LINES + 0.5f);
	if (viewport_width <= 0 || viewport_height <= 0 || divisor < 2)
		return FALSE;
	pass_count = (parameters->convolution_extra_passes + 1) * 2;
	/* a pass moves each copy by the radius each way: over the passes the
	spread is the radius times the root of their count, in game pixels, here
	in the screen's (the small targets' texels are divisor of them) */
	spread = parameters->convolution_radius * square_root((real)pass_count) * screen_scale;
	/* (port: the targets as small as the blur hides: the smallest, a third of the screen's lines or half of them; and
	half of them for a slighter blur, down to the least spread) */
	{
		short candidates[3];
		short candidate;

		candidates[0] = divisor;
		candidates[1] = (short)((divisor + 1) / 2);
		candidates[2] = 2;
		for (candidate = 0; candidate < 3; candidate++)
		{
			if (candidates[candidate] >= 2 && spread / (real)candidates[candidate] >= RASTERIZER_SCREEN_EFFECT_PORT_BLUR_HIDING_SPREAD)
				break;
		}
		divisor = candidate < 3 ? candidates[candidate] : 2;
	}
	width = (short)(viewport_width * screen_scale / divisor + 0.5f);
	height = (short)(viewport_height * screen_scale / divisor + 0.5f);
	spread /= (real)divisor;
	small = spread >= RASTERIZER_SCREEN_EFFECT_PORT_BLUR_LEAST_SPREAD;
	if (parameters->convolution_type != logged_type || pass_count != logged_passes || small != logged_small)
	{
		logged_type = parameters->convolution_type;
		logged_passes = pass_count;
		logged_small = small;
		platform_log("screen blur: %s, radius %.2f, %d passes, a spread of %.1f texels at %dx%d: %s",
			parameters->convolution_type == _rasterizer_screen_effect_convolution_type_warp ? "warp" : "blur",
			parameters->convolution_radius, pass_count, spread, width, height,
			small ? "drawn small" : "drawn as the game draws it");
	}
	if (!small || !rasterizer_screen_effect_port_warp_targets(width, height))
		return FALSE;
	*scale_x = (real)width / (real)viewport_width;
	*scale_y = (real)height / (real)viewport_height;
	return TRUE;
}

/* port: the passes a convolution draws after the Xbox's own on a screen of
screen_scale pixels to the Xbox's one, 0 at the Xbox's size. Each of the
Xbox's passes blends three copies of the screen a radius apart, and at
several pixels to the Xbox's one the copies stand apart as sharp ghosts.
Two more passes, at a third and a ninth of the radius, put eight copies
between each two, a ninth of the radius apart: on a screen of up to nine
pixels to the Xbox's one (2160 lines are 4.5) they stand no farther apart
than the Xbox's did in its own pixels, where they blended, and two more
again for up to 81. The blur stays as wide as the Xbox's (its variance 6%
more). Splitting the radius into as many times the passes as the screen's
scale, to six, drew ten full-screen passes at 2160 lines, where a scope's
zoom halved the frame rate, and narrowed the blur to less than half the
Xbox's. The count is even: the last pass draws into the screen, as the
Xbox's does. */
static short rasterizer_screen_effect_refinement_passes(
	real screen_scale)
{
	short passes = 0;
	real spacing = 1.0f;

	while (screen_scale * spacing > 1.0f && passes < 4)
	{
		passes += 2;
		spacing /= 9.0f;
	}
	return passes;
}

/* port: the radius of a pass of a convolution drawn with refinement
passes: the Xbox's passes keep the effect's, each pass after them a third
of the one before it */
static real rasterizer_screen_effect_pass_radius(
	real radius,
	short pass,
	short stock_pass_count)
{
	short refinement;

	for (refinement = stock_pass_count; refinement <= pass; refinement++)
		radius /= 3.0f;
	return radius;
}

/* ---------- public code */

void _rasterizer_screen_effect(
	struct rasterizer_cinematic_screen_effect_parameters *parameters)
{
	boolean success = TRUE;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
		251,
		global_d3d_device);

	rasterizer_profile_begin(_rasterizer_profile_screen_effect);

	/* port: the small targets the zoom and a blur are drawn on
	(rasterizer_screen_effect_port_warp_targets), made and drawn into once
	the first time the screen is drawn, at the menu: made when the first zoom
	needed them, the host's images of them were made in its frame, a hitch of
	a fifth of a second */
	{
		static boolean port_targets_made;

		if (!port_targets_made &&
			global_window_parameters.rasterizer_target == _rasterizer_target_render_primary &&
			(halo_screen_fast_warp() || halo_screen_fast_blur()) && main_get_window_count() <= 1)
		{
			short viewport_width = global_window_parameters.camera.viewport_bounds.x1 -
				global_window_parameters.camera.viewport_bounds.x0;
			short viewport_height = global_window_parameters.camera.viewport_bounds.y1 -
				global_window_parameters.camera.viewport_bounds.y0;
			real screen_scale = halo_screen_scale();
			short divisor = (short)(viewport_height * screen_scale / RASTERIZER_SCREEN_EFFECT_PORT_WARP_LINES + 0.5f);

			port_targets_made = TRUE;
			if (viewport_width > 0 && viewport_height > 0 && divisor >= 2 &&
				rasterizer_screen_effect_port_warp_targets(
					(short)(viewport_width * screen_scale / divisor + 0.5f),
					(short)(viewport_height * screen_scale / divisor + 0.5f)))
			{
				short index;

				for (index = 0; index < NUMBER_OF_RASTERIZER_SCREEN_EFFECT_PORT_WARP_TARGETS; index++)
				{
					rasterizer_screen_effect_port_warp_target(index);
					IDirect3DDevice8_Clear(global_d3d_device, 0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
				}
				rasterizer_set_target(global_window_parameters.rasterizer_target, FALSE, FALSE, FALSE, TRUE);
			}
		}
	}

	parameters = rasterizer_screen_effect_get_cinematic_parameters(parameters);

	if (parameters &&
		(parameters->convolution_type != _rasterizer_screen_effect_convolution_type_none ||
		parameters->convolution_mask ||
		parameters->filter_light_enhancement_intensity > 0.0f ||
		parameters->filter_desaturation_intensity > 0.0f ||
		parameters->video_on) &&
		rasterizer_debug_options.screen_effects_enabled &&
		global_window_parameters.rasterizer_target == _rasterizer_target_render_primary)
	{
		short pass_count = (parameters->convolution_extra_passes + 1) * 2;
		short pass;
		short source_target;
		short destination_target;
		short stage;
		short viewport_width;
		short viewport_height;
		real_rectangle2d vertex_bounds;
		unsigned long light_enhancement_input;
		unsigned long desaturation_input;
		struct rasterizer_cinematic_screen_effect_parameters scaled_parameters;
		/* (the Xbox's own passes, and its parameters, under the refinement passes) */
		short stock_pass_count;
		struct rasterizer_cinematic_screen_effect_parameters const *original_parameters;
		real port_blur_scale_x = 1.0f;
		real port_blur_scale_y = 1.0f;
		boolean port_blur;

		/* port: each convolution effect said once, what it is: the log tells
		which effects the port's small paths take (below) */
		{
			static short logged_type = -1;
			static short logged_extra_passes = -1;
			static boolean logged_mask = FALSE;
			static real logged_filters = -1.0f;
			real filters = parameters->filter_light_enhancement_intensity + parameters->filter_desaturation_intensity * 2.0f;

			if (parameters->convolution_type != _rasterizer_screen_effect_convolution_type_none &&
				(parameters->convolution_type != logged_type ||
				parameters->convolution_extra_passes != logged_extra_passes ||
				(parameters->convolution_mask != NULL) != logged_mask ||
				(filters > 0.0f) != (logged_filters > 0.0f)))
			{
				logged_type = parameters->convolution_type;
				logged_extra_passes = parameters->convolution_extra_passes;
				logged_mask = parameters->convolution_mask != NULL;
				logged_filters = filters;
				platform_log("screen effect: convolution %s, %s, %d extra passes, radius %.2f, light enhancement %.2f, "
					"desaturation %.2f, video %s",
					parameters->convolution_type == _rasterizer_screen_effect_convolution_type_warp ? "warp" :
					parameters->convolution_type == _rasterizer_screen_effect_convolution_type_blur ? "blur" : "?",
					parameters->convolution_mask ? "with a mask" : "no mask", parameters->convolution_extra_passes,
					parameters->convolution_radius, parameters->filter_light_enhancement_intensity,
					parameters->filter_desaturation_intensity, parameters->video_on ? "on" : "off");
			}
		}

		/* port: the zoom drawn small (rasterizer_screen_effect_port_fast_warp): no
		passes here */
		if (rasterizer_screen_effect_port_fast_warp(parameters))
			pass_count = 0;
		/* port: a blur drawn small (rasterizer_screen_effect_port_small_blur):
		its passes go through the small targets, as many as the Xbox's */
		port_blur = pass_count > 1 &&
			rasterizer_screen_effect_port_small_blur(parameters, &port_blur_scale_x, &port_blur_scale_y);

		/* port: the native builds draw the screen at several pixels to the
		Xbox's one (halo_screen_scale): a convolution's few copies of the
		screen, apart by its radius in the Xbox's pixels, blended into a blur
		at 640x480, and at four times the pixels they stand apart as sharp
		ghosts (the zoom's warp). The Xbox's passes are drawn as they were,
		and a few more, each a third of the last one's radius, fill the gaps
		between the copies (rasterizer_screen_effect_refinement_passes); not
		for a blur drawn small, whose passes are the Xbox's, nor for the zoom
		drawn small, which drew them all above (pass_count 0: refinement
		passes added to none were two full-screen passes of the warp, 2.6 ms
		of a Switch's zoomed frame at 720p). */
		stock_pass_count = pass_count;
		original_parameters = parameters;
		if (parameters->convolution_type != _rasterizer_screen_effect_convolution_type_none && !parameters->video_on &&
			!port_blur && pass_count > 0)
		{
			short refinement_passes = rasterizer_screen_effect_refinement_passes(halo_screen_scale());

			if (refinement_passes > 0)
			{
				scaled_parameters = *parameters;
				parameters = &scaled_parameters;
				pass_count += refinement_passes;
			}
		}

		if (parameters->video_on)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				276,
				parameters->video_scanline_map,
				"video effect must specify scanline map");
			match_vassert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				277,
				parameters->video_noise_map,
				"video effect must specify noise map");
			match_vassert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				278,
				!parameters->convolution_extra_passes,
				"video effect cannot specify extra convolution passes");
			match_vassert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				279,
				!parameters->convolution_type,
				"video effect cannot specify convolution type");
			match_vassert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				280,
				parameters->convolution_radius == 0.0f,
				"video effect cannot specify nonzero convolution radius");
			match_vassert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				281,
				!parameters->convolution_mask,
				"video effect cannot specify convolution mask");
		}

		if (parameters->convolution_type == _rasterizer_screen_effect_convolution_type_blur)
			match_vassert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
				286,
				!parameters->convolution_mask,
				"blur effect cannot specify convolution mask");

		rasterizer_set_vertex_shader_permutation(
			_rasterizer_vertex_shader_screen_effect,
			_rasterizer_vertex_type_screen,
			_rasterizer_vertex_shader_permutation_default);

		for (pass = 0; pass < pass_count; pass++)
		{
			if (pass_count == 1)
			{
				match_assert(
					"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
					313,
					pass==0);

				source_target = NONE;
				destination_target = NONE;
			}
			else if (!(pass & 1))
			{
				source_target = _rasterizer_target_render_primary;
				destination_target = _rasterizer_target_screen_effect;
			}
			else
			{
				source_target = _rasterizer_target_screen_effect;
				destination_target = _rasterizer_target_render_primary;
			}

			if ((pass & 1) && parameters->video_on)
			{
				match_assert(
					"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
					334,
					pass==1);

				rasterizer_set_target_as_texture(0, source_target, FALSE);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MINFILTER, D3DTEXF_POINT);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MIPFILTER, D3DTEXF_POINT);

				rasterizer_set_texture_bitmap_data(1, parameters->video_scanline_map);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 1, D3DTSS_MAGFILTER, D3DTEXF_POINT);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 1, D3DTSS_MINFILTER, D3DTEXF_POINT);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 1, D3DTSS_MIPFILTER, D3DTEXF_POINT);

				match_vassert(
					"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
					351,
					!TEST_FLAG(parameters->video_noise_map->flags, _bitmap_linear_bit),
					"video effect noise map cannot be in linear format");

				rasterizer_set_texture_bitmap_data(2, parameters->video_noise_map);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 2, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 2, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 2, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				IDirect3DDevice8_SetTextureStageState(global_d3d_device, 2, D3DTSS_MIPFILTER, D3DTEXF_POINT);
			}
			else
			{
				for (stage = 0; stage < 4; stage++)
				{
					if (parameters->convolution_type == _rasterizer_screen_effect_convolution_type_none)
					{
						if (pass_count == 1)
						{
							match_assert(
								"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
								371,
								pass==0);

							if (stage == 0)
								rasterizer_set_texture_bitmap_data(0, parameters->convolution_mask);
							else
								continue;
						}
						else if (pass == 0)
						{
							if (stage == 0)
								rasterizer_set_target_as_texture(0, source_target, FALSE);
							else
								continue;
						}
						else if (pass == 1)
						{
							if (parameters->convolution_mask)
							{
								if (stage == 0)
									rasterizer_set_texture_bitmap_data(0, parameters->convolution_mask);
								else if (stage == 1)
									rasterizer_set_target_as_texture(1, source_target, FALSE);
								else
									continue;
							}
							else if (stage == 0)
							{
								rasterizer_set_target_as_texture(0, source_target, FALSE);
							}
							else
							{
								continue;
							}
						}
						else
						{
							match_vassert(
								"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
								398,
								FALSE,
								"### ERROR non-convolution effect tried to render more than 2 passes");
						}
					}
					else if (parameters->convolution_mask)
					{
						if (stage == 0)
							rasterizer_set_texture_bitmap_data(0, parameters->convolution_mask);
						else if (stage == 1)
							rasterizer_set_target_as_texture(1, source_target, FALSE);
						else if (stage == 2)
							rasterizer_set_target_as_texture(2, source_target, FALSE);
						else if (stage == 3)
							rasterizer_set_target_as_texture(3, source_target, FALSE);
						else
							continue;
					}
					else
					{
						if (stage == 0)
							rasterizer_set_target_as_texture(0, source_target, FALSE);
						else if (stage == 1)
							rasterizer_set_target_as_texture(1, source_target, FALSE);
						else if (stage == 2)
							rasterizer_set_target_as_texture(2, source_target, FALSE);
						else if (stage == 3)
							rasterizer_set_target_as_texture(3, source_target, FALSE);
						else
							continue;
					}

					IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
					IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
					IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
					IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
					IDirect3DDevice8_SetTextureStageState(global_d3d_device, stage, D3DTSS_MIPFILTER, D3DTEXF_POINT);
				}
			}

			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_CULLMODE,
				D3DCULL_NONE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_COLORWRITEENABLE,
				D3DCOLORWRITEENABLE_RED |
				D3DCOLORWRITEENABLE_GREEN |
				D3DCOLORWRITEENABLE_BLUE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHABLENDENABLE,
				FALSE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHATESTENABLE,
				FALSE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ZENABLE,
				FALSE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ZBIAS,
				0);

			/* port: the refinement passes' radius (above) */
			if (parameters == &scaled_parameters)
			{
				scaled_parameters.convolution_radius = rasterizer_screen_effect_pass_radius(
					original_parameters->convolution_radius,
					pass,
					stock_pass_count);
			}
			/* port: a blur drawn small reads the small target the pass before
			wrote, its coordinates in that target's texels, and draws into the
			other small target, all but its last pass (above) */
			if (port_blur && pass > 0)
			{
				real constants[8][4];
				short row;

				for (stage = 0; stage < 4; stage++)
					IDirect3DDevice8_SetTexture(global_d3d_device, stage,
						(IDirect3DBaseTexture8 *)rasterizer_screen_effect_port_warp_textures[1 + ((pass - 1) & 1)]);
				rasterizer_screen_effect_set_texture_transforms(parameters, pass, pass_count, constants);
				constants[0][3] -= global_window_parameters.camera.viewport_bounds.x0;
				constants[1][3] -= global_window_parameters.camera.viewport_bounds.y0;
				constants[2][3] -= global_window_parameters.camera.viewport_bounds.x0;
				constants[3][3] -= global_window_parameters.camera.viewport_bounds.y0;
				for (row = 0; row < 8; row++)
				{
					real scale = (row & 1) ? port_blur_scale_y : port_blur_scale_x;

					constants[row][0] *= scale;
					constants[row][1] *= scale;
					constants[row][2] *= scale;
					constants[row][3] *= scale;
				}
				IDirect3DDevice8_SetVertexShaderConstant(global_d3d_device, -81, constants, 8);
			}
			else
				rasterizer_screen_effect_set_texture_transforms(parameters, pass, pass_count, NULL);

			if (port_blur && pass < pass_count - 1)
				rasterizer_screen_effect_port_warp_target(1 + (pass & 1));
			else if (destination_target != NONE)
				rasterizer_set_target(destination_target, FALSE, FALSE, FALSE, FALSE);

			if (parameters->video_on)
			{
				csmemset(&pixel_shader, 0, sizeof(pixel_shader));

				if (pass == 0)
				{
					pixel_shader.texture_modes = 0x00000001;
					pixel_shader.combiner_count = 1;
					pixel_shader.final_combiner_inputs_abcd = 0x00000008;
				}
				else if (pass == 1)
				{
					long video_overbright_scales[NUMBER_OF_RASTERIZER_SCREEN_EFFECT_VIDEO_OVERBRIGHT_MODES] =
						{0x00, 0x10, 0x20};

					pixel_shader.texture_modes = 0x00000421;
					pixel_shader.combiner_count = 4;

					pixel_shader.constant_0[0] =
						real_alpha_to_pixel32(PIN(parameters->video_noise_intensity, 0.0f, 1.0f));
					pixel_shader.alpha_inputs[0] = 0x3120111a;
					pixel_shader.alpha_outputs[0] = 0x00000c00;
					pixel_shader.rgb_inputs[0] = 0x3120110a;
					pixel_shader.rgb_outputs[0] = 0x00000c00;
					pixel_shader.rgb_inputs[1] = 0x0c091c19;
					pixel_shader.rgb_outputs[1] = 0x000000c4;
					pixel_shader.rgb_inputs[2] = 0x08080000;
					pixel_shader.rgb_outputs[2] = 0x000000d0;

					match_assert(
						"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
						497,
						parameters->video_overbright_mode>=0 && parameters->video_overbright_mode<NUMBER_OF_RASTERIZER_SCREEN_EFFECT_VIDEO_OVERBRIGHT_MODES);

					pixel_shader.rgb_inputs[3] = 0x0d0d0820;
					pixel_shader.rgb_outputs[3] =
						(video_overbright_scales[parameters->video_overbright_mode] << 12) | 0x000000d8;
					pixel_shader.final_combiner_inputs_abcd = 0x2c0d0800;
					pixel_shader.final_combiner_inputs_efg = 0x00000400;

					IDirect3DDevice8_SetRenderState(
						global_d3d_device,
						D3DRS_ALPHABLENDENABLE,
						TRUE);
					IDirect3DDevice8_SetRenderState(
						global_d3d_device,
						D3DRS_SRCBLEND,
						D3DBLEND_INVSRCALPHA);
					IDirect3DDevice8_SetRenderState(
						global_d3d_device,
						D3DRS_DESTBLEND,
						D3DBLEND_ZERO);
					IDirect3DDevice8_SetRenderState(
						global_d3d_device,
						D3DRS_BLENDOP,
						D3DBLENDOP_ADD);
				}
				else
				{
					match_vassert(
						"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
						513,
						FALSE,
						"### ERROR video effect tried to render more than 2 passes");
				}

				rasterizer_set_pixel_shader(&pixel_shader);
			}
			else
			{
				short combiner_count = 0;

				csmemset(&pixel_shader, 0, sizeof(pixel_shader));

				if (parameters->convolution_type == _rasterizer_screen_effect_convolution_type_none)
				{
					if (pass_count == 1)
					{
						match_assert(
							"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
							529,
							pass==0);

						pixel_shader.texture_modes = 0x00000001;
						pixel_shader.rgb_inputs[0] = 0x08200000;
						pixel_shader.rgb_outputs[0] = 0x000000c0;
						combiner_count = 1;
						pixel_shader.final_combiner_inputs_efg = 0x00000800;
						pixel_shader.final_combiner_inputs_abcd = 0x0000000c;
					}
					else if (pass == 0)
					{
						pixel_shader.texture_modes = 0x00000001;
						pixel_shader.rgb_inputs[0] = 0x08200000;
						pixel_shader.rgb_outputs[0] = 0x000000c0;
						combiner_count = 1;
						pixel_shader.final_combiner_inputs_efg = 0;
						pixel_shader.final_combiner_inputs_abcd = 0x0000000c;
					}
					else if (pass == 1)
					{
						if (parameters->convolution_mask)
						{
							pixel_shader.texture_modes = 0x00000021;
							pixel_shader.rgb_inputs[0] = 0x09200000;
							pixel_shader.rgb_outputs[0] = 0x000000c0;
							combiner_count = 1;
							pixel_shader.final_combiner_inputs_efg = 0x00000800;
							pixel_shader.final_combiner_inputs_abcd = 0x0000000c;
						}
						else
						{
							pixel_shader.texture_modes = 0x00000021;
							pixel_shader.rgb_inputs[0] = 0x08200000;
							pixel_shader.rgb_outputs[0] = 0x000000c0;
							combiner_count = 1;
							pixel_shader.final_combiner_inputs_efg = 0;
							pixel_shader.final_combiner_inputs_abcd = 0x0000000c;
						}
					}
					else
					{
						match_vassert(
							"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
							584,
							FALSE,
							"### ERROR non-convolution effect tried to render more than 2 passes");
					}
				}
				else
				{
					if (parameters->convolution_mask)
					{
						pixel_shader.texture_modes = 0x00008421;
						pixel_shader.rgb_inputs[0] = 0x89208a20;
						pixel_shader.rgb_outputs[0] = 0x00000c00;
						pixel_shader.constant_0[1] = real_alpha_to_pixel32(1.0f / 3.0f);
						pixel_shader.rgb_inputs[1] = 0xcc118b11;
						pixel_shader.rgb_outputs[1] = 0x00000c00;
						pixel_shader.rgb_inputs[2] = 0xcc20a020;
						pixel_shader.rgb_outputs[2] = 0x00000c00;
						pixel_shader.rgb_inputs[3] = 0x3809180c;
						pixel_shader.rgb_outputs[3] = 0x00000c00;
						combiner_count = 4;
						pixel_shader.final_combiner_inputs_efg = 0x00000800;
					}
					else
					{
						pixel_shader.texture_modes = 0x00008421;
						pixel_shader.rgb_inputs[0] = 0x88208920;
						pixel_shader.rgb_outputs[0] = 0x00030c00;
						pixel_shader.rgb_inputs[1] = 0x8a208b20;
						pixel_shader.rgb_outputs[1] = 0x00030d00;
						pixel_shader.rgb_inputs[2] = 0xcc20cd20;
						pixel_shader.rgb_outputs[2] = 0x00030c00;
						pixel_shader.rgb_inputs[3] = 0xcc20a020;
						pixel_shader.rgb_outputs[3] = 0x00000c00;
						combiner_count = 4;
						pixel_shader.final_combiner_inputs_efg = 0;
					}

					pixel_shader.final_combiner_inputs_abcd = 0x0000000c;
				}

				light_enhancement_input = parameters->filter_light_enhancement_uses_convolution_mask ? 0x68 : 0x20;
				desaturation_input = parameters->filter_desaturation_uses_convolution_mask ? 0x68 : 0x20;
				pixel_shader.constant_0[combiner_count] =
					real_alpha_to_pixel32(parameters->filter_light_enhancement_intensity);
				pixel_shader.constant_1[combiner_count] =
					real_alpha_to_pixel32(parameters->filter_desaturation_intensity);
				pixel_shader.alpha_inputs[combiner_count] =
					(light_enhancement_input << 16) |
					desaturation_input |
					0x11001200;
				pixel_shader.alpha_outputs[combiner_count] = 0x000000cd;

				if (pass == pass_count - 1 &&
					parameters->filter_light_enhancement_intensity > 0.0f)
				{
					pixel_shader.rgb_inputs[combiner_count] = 0x2c2c0000;
					pixel_shader.rgb_outputs[combiner_count] = 0x00000d00;
					combiner_count++;
					pixel_shader.rgb_inputs[combiner_count] = 0x0d0d0000;
					pixel_shader.rgb_outputs[combiner_count] = 0x00000d00;
					combiner_count++;
					pixel_shader.final_combiner_inputs_abcd = 0x3c0c2d00;
					pixel_shader.final_combiner_inputs_efg =
						(parameters->convolution_mask ? 8 : 0) << 8;
				}

				if (pass == pass_count - 1 &&
					parameters->filter_desaturation_intensity > 0.0f)
				{
					real_rgb_color desaturation_weights;

					desaturation_weights.red = 1.0f / 3.0f;
					desaturation_weights.green = 1.0f / 3.0f;
					desaturation_weights.blue = 1.0f / 3.0f;

					if (parameters->filter_light_enhancement_intensity > 0.0f)
					{
						pixel_shader.constant_0[combiner_count] =
							real_alpha_to_pixel32(parameters->filter_light_enhancement_intensity);
						pixel_shader.rgb_inputs[combiner_count] = 0x3c0c1c2d;
						pixel_shader.rgb_outputs[combiner_count] = 0x00000c00;
						combiner_count++;
					}

					pixel_shader.constant_0[combiner_count] =
						real_rgb_color_to_pixel32(&desaturation_weights);
					pixel_shader.rgb_inputs[combiner_count] = 0x0c010000;
					pixel_shader.rgb_outputs[combiner_count] = 0x000020d0;
					combiner_count++;

					if (parameters->filter_desaturation_is_additive)
					{
						pixel_shader.final_combiner_constant_0 =
							real_rgb_color_to_pixel32(&parameters->filter_desaturation_tint);
						pixel_shader.final_combiner_inputs_abcd = 0x1d0f000c;
						pixel_shader.final_combiner_inputs_efg =
							((parameters->convolution_mask ? 8 : 0) | 0x00010d00) << 8;
					}
					else
					{
						pixel_shader.final_combiner_constant_0 =
							real_rgb_color_to_pixel32(&parameters->filter_desaturation_tint);
						pixel_shader.final_combiner_inputs_abcd = 0x1d0f0c00;
						pixel_shader.final_combiner_inputs_efg =
							((parameters->convolution_mask ? 8 : 0) | 0x00010d00) << 8;
					}
				}

				match_assert(
					"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
					705,
					combiner_count<=RASTERIZER_MAXIMUM_COMBINER_STAGES);

				pixel_shader.combiner_count = combiner_count | 0x00011000;
				rasterizer_set_pixel_shader(&pixel_shader);

				if (pass_count == 1)
				{
					IDirect3DDevice8_SetRenderState(
						global_d3d_device,
						D3DRS_ALPHABLENDENABLE,
						TRUE);
					IDirect3DDevice8_SetRenderState(
						global_d3d_device,
						D3DRS_SRCBLEND,
						D3DBLEND_ZERO);
					SetRenderStateSmart(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
					SetRenderStateSmart(D3DRS_BLENDOP, D3DBLENDOP_ADD);
				}
				else if (pass == pass_count - 1)
				{
					SetRenderStateSmart(D3DRS_ALPHABLENDENABLE, TRUE);
					SetRenderStateSmart(D3DRS_SRCBLEND, D3DBLEND_INVSRCALPHA);
					SetRenderStateSmart(D3DRS_DESTBLEND, D3DBLEND_ZERO);
					SetRenderStateSmart(D3DRS_BLENDOP, D3DBLENDOP_ADD);
				}
			}

			viewport_width = global_window_parameters.camera.viewport_bounds.x1 -
				global_window_parameters.camera.viewport_bounds.x0;
			viewport_height = global_window_parameters.camera.viewport_bounds.y1 -
				global_window_parameters.camera.viewport_bounds.y0;

			if (pass == 0 && main_get_window_count() > 1 && pass_count != 1)
			{
				vertex_bounds.x0 = 2 * global_window_parameters.camera.viewport_bounds.x0 *
					(1.0f / (real)halo_screen_width()) - 1.0f;
				vertex_bounds.x1 = 2 * global_window_parameters.camera.viewport_bounds.x1 *
					(1.0f / (real)halo_screen_width()) - 1.0f;
				vertex_bounds.y0 = -2 * global_window_parameters.camera.viewport_bounds.y0 *
					(1.0f / 480.0f) + 1.0f;
				vertex_bounds.y1 = -2 * global_window_parameters.camera.viewport_bounds.y1 *
					(1.0f / 480.0f) + 1.0f;
			}
			else
			{
				vertex_bounds.x0 = vertex_bounds.y1 = -1.0f;
				vertex_bounds.x1 = vertex_bounds.y0 = 1.0f;
			}

			if (IDirect3DDevice8_Begin(
				global_d3d_device,
				D3DPT_TRIANGLEFAN) >= 0 && success)
			{
				success = TRUE;
			}
			else
			{
				success = FALSE;
				rasterizer_error(
					0,
					"IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLEFAN)");
			}

			if (IDirect3DDevice8_SetVertexData2s(
				global_d3d_device,
				4,
				0,
				viewport_height) >= 0 && success)
			{
				success = TRUE;
			}
			else
			{
				success = FALSE;
				rasterizer_error(
					0,
					"IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, viewport_height)");
			}

			if (IDirect3DDevice8_SetVertexData2f(
				global_d3d_device,
				_rasterizer_vertex_register_position,
				vertex_bounds.x0,
				vertex_bounds.y1) >= 0 && success)
			{
				success = TRUE;
			}
			else
			{
				success = FALSE;
				rasterizer_error(
					0,
					"IDirect3DDevice8_SetVertexData2f(global_d3d_device, VSDE_VERTEX, vertex_bounds.x0, vertex_bounds.y1)");
			}

			if (IDirect3DDevice8_SetVertexData2s(
				global_d3d_device,
				4,
				viewport_width,
				viewport_height) >= 0 && success)
			{
				success = TRUE;
			}
			else
			{
				success = FALSE;
				rasterizer_error(
					0,
					"IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, viewport_width, viewport_height)");
			}

			if (IDirect3DDevice8_SetVertexData2f(
				global_d3d_device,
				_rasterizer_vertex_register_position,
				vertex_bounds.x1,
				vertex_bounds.y1) >= 0 && success)
			{
				success = TRUE;
			}
			else
			{
				success = FALSE;
				rasterizer_error(
					0,
					"IDirect3DDevice8_SetVertexData2f(global_d3d_device, VSDE_VERTEX, vertex_bounds.x1, vertex_bounds.y1)");
			}

			if (IDirect3DDevice8_SetVertexData2s(
				global_d3d_device,
				4,
				viewport_width,
				0) >= 0 && success)
			{
				success = TRUE;
			}
			else
			{
				success = FALSE;
				rasterizer_error(
					0,
					"IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, viewport_width, 0)");
			}

			if (IDirect3DDevice8_SetVertexData2f(
				global_d3d_device,
				_rasterizer_vertex_register_position,
				vertex_bounds.x1,
				vertex_bounds.y0) >= 0 && success)
			{
				success = TRUE;
			}
			else
			{
				success = FALSE;
				rasterizer_error(
					0,
					"IDirect3DDevice8_SetVertexData2f(global_d3d_device, VSDE_VERTEX, vertex_bounds.x1, vertex_bounds.y0)");
			}

			if (IDirect3DDevice8_SetVertexData2s(
				global_d3d_device,
				4,
				0,
				0) >= 0 && success)
			{
				success = TRUE;
			}
			else
			{
				success = FALSE;
				rasterizer_error(
					0,
					"IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 0)");
			}

			if (IDirect3DDevice8_SetVertexData2f(
				global_d3d_device,
				_rasterizer_vertex_register_position,
				vertex_bounds.x0,
				vertex_bounds.y0) >= 0 && success)
			{
				success = TRUE;
			}
			else
			{
				success = FALSE;
				rasterizer_error(
					0,
					"IDirect3DDevice8_SetVertexData2f(global_d3d_device, VSDE_VERTEX, vertex_bounds.x0, vertex_bounds.y0)");
			}

			if (IDirect3DDevice8_End(global_d3d_device) >= 0 && success)
			{
				success = TRUE;
			}
			else
			{
				success = FALSE;
				rasterizer_error(
					0,
					"IDirect3DDevice8_End(global_d3d_device)");
			}
		}

		rasterizer_set_target(
			global_window_parameters.rasterizer_target,
			FALSE,
			FALSE,
			FALSE,
			TRUE);
	}

	rasterizer_profile_end(_rasterizer_profile_screen_effect);

	if (!success)
		error(2, "### ERROR rasterizer_screen_effect failed");

	return;
}

void _rasterizer_screen_flash(
	void)
{
	real vertex_constants[5][4];
	real_argb_color inverse_flash_color;
	real_argb_color flash_color;
	pixel32 inverse_flash_pixel;
	pixel32 flash_pixel;
	unsigned long rgb_input;
	unsigned long alpha_input;
	short viewport_width;
	short viewport_height;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
		786,
		global_d3d_device);

	rasterizer_profile_begin(_rasterizer_profile_screen_flash);

	if (rasterizer_debug_options.screen_flash_enabled &&
		global_window_parameters.screen_flash.type != _render_screen_flash_type_none)
	{
		flash_color.alpha = global_window_parameters.screen_flash.intensity *
			global_window_parameters.screen_flash.color.alpha;
		flash_color.red = global_window_parameters.screen_flash.color.red *
			global_window_parameters.screen_flash.intensity;
		flash_color.green = global_window_parameters.screen_flash.color.green *
			global_window_parameters.screen_flash.intensity;
		flash_color.blue = global_window_parameters.screen_flash.color.blue *
			global_window_parameters.screen_flash.intensity;
		inverse_flash_color.alpha = flash_color.alpha;
		inverse_flash_color.red = (1.0f - global_window_parameters.screen_flash.color.red) *
			global_window_parameters.screen_flash.intensity;
		inverse_flash_color.green = (1.0f - global_window_parameters.screen_flash.color.green) *
			global_window_parameters.screen_flash.intensity;
		inverse_flash_color.blue = (1.0f - global_window_parameters.screen_flash.color.blue) *
			global_window_parameters.screen_flash.intensity;

		flash_pixel = real_argb_color_to_pixel32(&flash_color);
		inverse_flash_pixel = real_argb_color_to_pixel32(&inverse_flash_color);

		rgb_input = 0;
		alpha_input = 0;

		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_CULLMODE,
			D3DCULL_CCW);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_COLORWRITEENABLE,
			D3DCOLORWRITEENABLE_RED |
			D3DCOLORWRITEENABLE_GREEN |
			D3DCOLORWRITEENABLE_BLUE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ALPHABLENDENABLE,
			TRUE);

		switch (global_window_parameters.screen_flash.type)
		{
			case _render_screen_flash_type_lighten:
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_SRCBLEND,
					D3DBLEND_ONE);
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_DESTBLEND,
					D3DBLEND_INVSRCALPHA);
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_BLENDOP,
					D3DBLENDOP_ADD);
				rgb_input = 0x01200000;
				alpha_input = 0x11200000;
				break;

			case _render_screen_flash_type_darken:
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_SRCBLEND,
					D3DBLEND_ONE);
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_DESTBLEND,
					D3DBLEND_ONE);
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_BLENDOP,
					D3DBLENDOP_REVSUBTRACT);
				rgb_input = 0x01200000;
				alpha_input = 0x11200000;
				break;

			case _render_screen_flash_type_max:
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_SRCBLEND,
					D3DBLEND_INVDESTCOLOR);
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_DESTBLEND,
					D3DBLEND_INVCONSTANTCOLOR);
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_BLENDOP,
					D3DBLENDOP_MAX);
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_BLENDCOLOR,
					flash_pixel);
				rgb_input = 0x01201140;
				alpha_input = 0;
				break;

			case _render_screen_flash_type_min:
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_SRCBLEND,
					D3DBLEND_INVDESTCOLOR);
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_DESTBLEND,
					D3DBLEND_INVCONSTANTCOLOR);
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_BLENDOP,
					D3DBLENDOP_MIN);
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_BLENDCOLOR,
					flash_pixel);
				rgb_input = 0x01201120;
				alpha_input = 0;
				break;

			case _render_screen_flash_type_invert:
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_SRCBLEND,
					D3DBLEND_INVDESTCOLOR);
				SetRenderStateSmart(D3DRS_DESTBLEND, D3DBLEND_INVCONSTANTCOLOR);
				SetRenderStateSmart(D3DRS_BLENDOP, D3DBLENDOP_ADD);
				SetRenderStateSmart(D3DRS_BLENDCOLOR, flash_pixel);
				rgb_input = 0x01411120;
				alpha_input = 0;
				break;

			case _render_screen_flash_type_tint:
				SetRenderStateSmart(D3DRS_SRCBLEND, D3DBLEND_ONE);
				SetRenderStateSmart(D3DRS_DESTBLEND, D3DBLEND_INVCONSTANTCOLOR);
				SetRenderStateSmart(D3DRS_BLENDOP, D3DBLENDOP_ADD);
				flash_pixel = inverse_flash_pixel;
				SetRenderStateSmart(D3DRS_BLENDCOLOR, flash_pixel);
				rgb_input = 0x11200000;
				alpha_input = 0;
				break;

			default:
				match_vassert(
					"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_screen_effect.c",
					875,
					FALSE,
					"### ERROR unsupported screen flash type");
				flash_pixel = inverse_flash_pixel;
				break;
		}

		SetRenderStateSmart(D3DRS_ALPHATESTENABLE, FALSE);
		SetRenderStateSmart(D3DRS_ZENABLE, FALSE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ZBIAS,
			0);
		rasterizer_set_vertex_shader_permutation(
			_rasterizer_vertex_shader_screen,
			_rasterizer_vertex_type_screen,
			_rasterizer_vertex_shader_permutation_default);

		viewport_width = global_window_parameters.camera.viewport_bounds.x1 -
			global_window_parameters.camera.viewport_bounds.x0;
		viewport_height = global_window_parameters.camera.viewport_bounds.y1 -
			global_window_parameters.camera.viewport_bounds.y0;
		vertex_constants[0][0] = 1.0f / viewport_width * 2.0f;
		vertex_constants[0][1] = 0.0f;
		vertex_constants[0][2] = 0.0f;
		vertex_constants[0][3] = -1.0f - 1.0f / viewport_width;
		vertex_constants[1][0] = 0.0f;
		vertex_constants[1][1] = 1.0f / viewport_height * -2.0f;
		vertex_constants[1][2] = 0.0f;
		vertex_constants[1][3] = 1.0f / viewport_height + 1.0f;
		vertex_constants[2][0] = 0.0f;
		vertex_constants[2][1] = 0.0f;
		vertex_constants[2][2] = 0.0f;
		vertex_constants[2][3] = 0.5f;
		vertex_constants[3][0] = 0.0f;
		vertex_constants[3][1] = 0.0f;
		vertex_constants[3][2] = 0.0f;
		vertex_constants[3][3] = 1.0f;
		vertex_constants[4][0] = 0.0f;
		vertex_constants[4][1] = 0.0f;
		vertex_constants[4][2] = 0.0f;
		vertex_constants[4][3] = 1.0f;
		IDirect3DDevice8_SetVertexShaderConstant(
			global_d3d_device,
			-68,
			vertex_constants,
			5);

		csmemset(&pixel_shader, 0, sizeof(pixel_shader));
		pixel_shader.combiner_count = 1;
		pixel_shader.constant_0[0] = flash_pixel;
		pixel_shader.alpha_inputs[0] = alpha_input;
		pixel_shader.alpha_outputs[0] = 0x00000C00;
		pixel_shader.rgb_inputs[0] = rgb_input;
		pixel_shader.rgb_outputs[0] = 0x00000C00;
		pixel_shader.final_combiner_inputs_abcd = 0x0000000C;
		pixel_shader.final_combiner_inputs_efg = 0x00001C00;
		rasterizer_set_pixel_shader(&pixel_shader);

		viewport_width = global_window_parameters.camera.viewport_bounds.x1 -
			global_window_parameters.camera.viewport_bounds.x0;
		viewport_height = global_window_parameters.camera.viewport_bounds.y1 -
			global_window_parameters.camera.viewport_bounds.y0;

		IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLEFAN);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 0, 0, 0);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 0, viewport_width, 0);
		IDirect3DDevice8_SetVertexData2s(
			global_d3d_device,
			0,
			viewport_width,
			viewport_height);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 0, 0, viewport_height);
		IDirect3DDevice8_End(global_d3d_device);
	}

	rasterizer_profile_end(_rasterizer_profile_screen_flash);

	return;
}
