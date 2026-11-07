/* A filtered local visibility field avoids rejecting a whole model at a door.
   Queries see structure, scenery and machines, plus vehicles for turret flashes.
   The field
   is approximate within one cell of an edge; this is not a shadow renderer. */
#include "plasma_occlusion.h"
#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include "plasma_collision.h"
#include "ammo_counter_falloff.h"
#include "cseries.h"
#include "physics/collisions.h"
#include "plasma_lighting.h"
#include "authored_emitter_lighting.h"
#include "muzzle_lighting.h"
#include "shield_glow.h"
#include "covenant_emitter_lighting.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_lights.h"
#include <math.h>
#include <string.h>
#include <xtl.h>

#include "lighting_config.h"
void platform_log(char const *format, ...);
enum
{
	OCC_SIZE = 16,
	OCC_COUNT = 4096,
	OCC_LIGHTS = 8,
	OCC_HELD_RESOLUTION = 4,
	OCC_MOVING_RESOLUTION = 8,
	OCC_SUBMITTED = 128,
	/* (views: more than one frame's, with split screen) */
	OCC_UNREAD_VIEWS = 4
};
static struct occlusion_field
{
	long identity;
	unsigned long used, updated, window, admitted, traced;
	unsigned long scene_revision;
	struct plasma_collision_scene scene;
	int fading, texture_mask;
	real admission;
	real center[3], extent, radius, specular;
	real visibility[OCC_COUNT];
	real targets[OCC_COUNT];
	real depths[6][OCC_SIZE * OCC_SIZE], trace_center[3], trace_extent;
	int trace_resolution; /* the ray grid its depths were traced with */
	int settled; /* still, and its last fade changed nothing: nothing to do until that changes */
	unsigned long traced_window; /* the view plan_traces traced it for */
	unsigned long waits_window; /* the view it sits out, so that another light has the slot */
	int too_far; /* this view: it could not be traced, and has moved too far from its depths */
	unsigned long consumed; /* the view a draw last asked for it */
	int passes_used; /* which texture passes draws asked for since the last prepare */
	int prepared; /* has been through plasma_occlusion_prepare_view */
	IDirect3DVolumeTexture8 *texture[3];
	/* Exact last successful uploads, bounded to 384 KiB across all fields.
	   Identical texels need no memory-watch fault, decoding or GPU upload. */
	unsigned long uploaded[3][OCC_COUNT];
	int uploaded_mask;
	int directional;
	real direction[3];
	int valid;
} fields[OCC_LIGHTS];
static struct
{
	long identity, rasterizer;
} submitted[OCC_SUBMITTED];
/* Only overflow identities need admission history; no engine pointers persist. */
static struct
{
	long identity;
	unsigned long seen;
	int valid;
} overflow[OCC_SUBMITTED];
static void remember_overflow(long identity, unsigned long now)
{
	int i, oldest = 0;
	for (i = 0; i < OCC_SUBMITTED; i++)
	{
		if (overflow[i].valid && overflow[i].identity == identity)
		{
			oldest = i;
			break;
		}
		if (!overflow[i].valid)
		{
			oldest = i;
			break;
		}
		if (now - overflow[i].seen > now - overflow[oldest].seen)
			oldest = i;
	}
	overflow[oldest].identity = identity;
	overflow[oldest].seen = now;
	overflow[oldest].valid = 1;
}
/* What this view has spent of lighting_budget, and what is held back for a light
   a draw asked for and was refused (remember_overflow). */
static struct
{
	long rays, us, reserved_rays, reserved_us;
	int traces, planned;
	int weapon_limit, weapon_updates; /* a limit is only set by plan_traces */
} view_budget;
/* debug.gpu_stats: what the fields of the last second came to, so a profile's
   time can be put down to the lights that spent it. */
static struct
{
	unsigned long logged;
	long views, rays;
	long trace_new, trace_held, trace_moved, trace_scene;
	long rebuild_held, rebuild_moved, rebuild_stationary;
	long fade, fade_settled;
	long textures, uploads;
	long refused, too_far, overflow, at_rest, waited;
	long submitted, promoted, shrunk, bolts_seen, bolts, glows;
} stats;
static int submitted_count;
static int updates_this_window;
static int fields_dirty;
static unsigned long current_window;
static long model_slots[2] = {NONE, NONE};
static int enabled(void)
{
	/* port: Light Occlusion off means no rays, for muzzle and vehicle lights
	   too (docs/dynamic_lighting_port.md), so they can light through walls. */
	return lighting_config_boolean("display.plasma_occlusion") &&
		(lighting_config_boolean("display.plasma_lighting") || authored_emitter_lighting_enabled() ||
		covenant_emitter_lighting_enabled() || shield_glow_enabled() ||
		lighting_config_boolean("display.muzzle_lighting") || lighting_config_boolean("display.vehicle_lighting"));
}
struct sample_axis
{
	int a, b, inside;
	real t;
};
static void sample_axis_initialize(struct sample_axis *axis, real coordinate)
{
	real p = MAX(0.0f, MIN((real)(OCC_SIZE - 1), coordinate * OCC_SIZE - 0.5f));
	axis->a = (int)p;
	axis->b = MIN(axis->a + 1, OCC_SIZE - 1);
	axis->t = p - axis->a;
	axis->inside = coordinate >= 0 && coordinate <= 1;
}
static real sample_axes(const real *values, const struct sample_axis *x,
	const struct sample_axis *y, const struct sample_axis *z)
{
	int row0 = (z->a * OCC_SIZE + y->a) * OCC_SIZE;
	int row1 = (z->a * OCC_SIZE + y->b) * OCC_SIZE;
	int row2 = (z->b * OCC_SIZE + y->a) * OCC_SIZE;
	int row3 = (z->b * OCC_SIZE + y->b) * OCC_SIZE;
	real a = values[row0+x->a], b = values[row1+x->a];
	real c = values[row2+x->a], d = values[row3+x->a];
	/* Seven linear interpolations replace eight three-factor weights. This
	   render-only field tolerates sub-texel rounding, before 8-bit upload. */
	a += (values[row0+x->b] - a) * x->t;
	b += (values[row1+x->b] - b) * x->t;
	c += (values[row2+x->b] - c) * x->t;
	d += (values[row3+x->b] - d) * x->t;
	a += (b - a) * y->t;
	c += (d - c) * y->t;
	return a + (c - a) * z->t;
}
static real sample_field(const real *values, real x, real y, real z)
{
	struct sample_axis axes[3];
	sample_axis_initialize(&axes[0], x);
	sample_axis_initialize(&axes[1], y);
	sample_axis_initialize(&axes[2], z);
	return sample_axes(values, &axes[0], &axes[1], &axes[2]);
}

static void half_sample_axes(struct sample_axis axes[3][OCC_SIZE])
{
	int axis, i;
	/* With a half-size receiver volume, coordinate * 16 - 0.5 is exactly
	   3.75 + index / 2. All samples are interior, at quarter-texel weights.
	   Keep the same interpolation order; other scale ratios use sample_field. */
	for (axis = 0; axis < 3; axis++)
		for (i = 0; i < OCC_SIZE; i++)
		{
			axes[axis][i].a = (i + 7) / 2;
			axes[axis][i].b = axes[axis][i].a + 1;
			axes[axis][i].t = i & 1 ? 0.25f : 0.75f;
			axes[axis][i].inside = TRUE;
		}
}

/* Reuse the x/y interpolation shared by neighbouring output cells. The final
   z interpolation remains in the update loop. This is the same x-then-y-then-z
   arithmetic as sample_axes, with three interpolations per cell instead of
   seven. Scratch lives only during this synchronous render update. */
static void reproject_rows(const real *values, struct sample_axis const axes[3][OCC_SIZE], real *rows)
{
	real horizontal[OCC_COUNT];
	int x, y, z, low[3] = {0, OCC_SIZE - 1, OCC_SIZE - 1}, high[3] = {0, 0, 0};
	/* Only source rows reached by the requested coordinates are needed.
	   In the half-size volume, ten source rows feed sixteen output rows. */
	for (x = 0; x < OCC_SIZE; x++)
		for (y = 1; y < 3; y++)
		{
			low[y] = MIN(low[y], MIN(axes[y][x].a, axes[y][x].b));
			high[y] = MAX(high[y], MAX(axes[y][x].a, axes[y][x].b));
		}
	for (z = low[2]; z <= high[2]; z++)
		for (y = low[1]; y <= high[1]; y++)
			for (x = 0; x < OCC_SIZE; x++)
			{
				int row = (z * OCC_SIZE + y) * OCC_SIZE;
				real a = values[row + axes[0][x].a];
				horizontal[row + x] = a + (values[row + axes[0][x].b] - a) * axes[0][x].t;
			}
	for (z = low[2]; z <= high[2]; z++)
		for (y = 0; y < OCC_SIZE; y++)
			for (x = 0; x < OCC_SIZE; x++)
			{
				int row0 = (z * OCC_SIZE + axes[1][y].a) * OCC_SIZE;
				int row1 = (z * OCC_SIZE + axes[1][y].b) * OCC_SIZE;
				real a = horizontal[row0 + x];
				rows[(z * OCC_SIZE + y) * OCC_SIZE + x] = a + (horizontal[row1 + x] - a) * axes[1][y].t;
			}
}
static real sample_history_rows(const real *rows, int x, int y, struct sample_axis const *z)
{
	real a = rows[(z->a * OCC_SIZE + y) * OCC_SIZE + x];
	return a + (rows[(z->b * OCC_SIZE + y) * OCC_SIZE + x] - a) * z->t;
}

/* One depth ray per cubemap texel serves every radial cell on that ray.
   Interpolate visibility comparisons, never distances across a doorway. */
static void trace_depths(real const extent, real_point3d const *from, long ignore, int vehicles, int resolution, real depths[6][OCC_SIZE * OCC_SIZE])
{
	real_vector3d vectors[PLASMA_COLLISION_RAYS];
	real hits[PLASMA_COLLISION_RAYS];
	int face, x, y, axis, count = 0;
	unsigned long flags =
		FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_back_facing_surfaces_bit) | FLAG(_collision_test_objects_bit) |
		FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_machines_bit);
	if (vehicles) flags |= FLAG(_collision_test_objects_vehicles_bit);
	for (face = 0; face < 6; face++)
		for (y = 0; y < resolution; y++)
			for (x = 0; x < resolution; x++)
			{
				real component[3];
				real_vector3d ray;
				axis = face / 2;
				component[axis] = (face & 1) ? extent : -extent;
				component[(axis + 1) % 3] = (2.0f * (x + 0.5f) / resolution - 1) * extent;
				component[(axis + 2) % 3] = (2.0f * (y + 0.5f) / resolution - 1) * extent;
				ray.i = component[0];
				ray.j = component[1];
				ray.k = component[2];
				vectors[count++] = ray;
			}
	plasma_collision_depths(flags, from, vectors, count, ignore, hits);
	count = 0;
	for (face = 0; face < 6; face++)
		for (y = 0; y < resolution; y++)
			for (x = 0; x < resolution; x++)
				depths[face][y * OCC_SIZE + x] = hits[count++];
}
/* port: the extent changed before the next trace is due. A depth is a fraction
   of its ray, whose length follows the extent, so the same hit is at another
   fraction. A ray that missed stays clear until the next trace, even if the
   extent grew past where it stopped looking. */
static void rescale_depths(real depths[6][OCC_SIZE * OCC_SIZE], real from, real to)
{
	real scale = from / to;
	int face, i;
	for (face = 0; face < 6; face++)
		for (i = 0; i < OCC_SIZE * OCC_SIZE; i++)
			if (depths[face][i] < 1.0f)
				depths[face][i] = MIN(1.0f, depths[face][i] * scale);
}
static real depth_visibility(real depths[6][OCC_SIZE * OCC_SIZE], real_vector3d const *ray, real extent, int resolution)
{
	real p[3], magnitude, u, v, tx, ty, result = 0, length, fraction, hits[2][2];
	int axis = 0, face, x, y, i, j, clear = TRUE;
	p[0] = ray->i;
	p[1] = ray->j;
	p[2] = ray->k;
	if (fabsf(p[1]) > fabsf(p[axis]))
		axis = 1;
	if (fabsf(p[2]) > fabsf(p[axis]))
		axis = 2;
	magnitude = fabsf(p[axis]);
	if (magnitude < 0.0001f)
		return 1;
	face = axis * 2 + (p[axis] > 0);
	u = MAX(0.0f, MIN((real)(resolution - 1), (p[(axis + 1) % 3] / magnitude + 1) * 0.5f * resolution - 0.5f));
	v = MAX(0.0f, MIN((real)(resolution - 1), (p[(axis + 2) % 3] / magnitude + 1) * 0.5f * resolution - 0.5f));
	x = (int)u;
	y = (int)v;
	tx = u - x;
	ty = v - y;
	fraction = magnitude / extent;
	for (j = 0; j < 2; j++)
		for (i = 0; i < 2; i++)
		{
			hits[j][i] = depths[face][MIN(y + j, resolution - 1) * OCC_SIZE + MIN(x + i, resolution - 1)];
			if (!(hits[j][i] >= fraction)) clear = FALSE;
		}
	/* All four comparisons saturate to one before any distance feathering.
	   Retain the original sum/order of weights, even its rounding at seams,
	   but avoid the square root and four redundant visibility calculations. */
	if (clear)
		return (1 - tx) * (1 - ty) + tx * (1 - ty) + (1 - tx) * ty + tx * ty;
	length = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) * extent / magnitude;
	for (j = 0; j < 2; j++)
		for (i = 0; i < 2; i++)
		{
			real visible = MAX(0.0f, MIN(1.0f, 1.0f - (fraction - hits[j][i]) * length / 0.025f));
			result += visible * (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
		}
	return result;
}
/* Four receiver cells share face selection and visibility arithmetic. Keep
   exact divides, square roots and weight order: no approximate reciprocals or
   reduced grids. One row of logic for both instruction sets; a comparison gives
   all-ones lanes, min and max choose as SSE's do (a second operand on a tie or a
   NaN), and the harness checks they give the scalar path's every bit on both.
   Other CPU targets retain the scalar implementation. */
#if defined(__SSE2__)
#define VISIBILITY_VECTOR
typedef __m128 vis_real;
static vis_real vis_set(real value) { return _mm_set1_ps(value); }
static vis_real vis_load(const real *from) { return _mm_loadu_ps(from); }
static void vis_store(real *to, vis_real value) { _mm_storeu_ps(to, value); }
static vis_real vis_add(vis_real a, vis_real b) { return _mm_add_ps(a, b); }
static vis_real vis_sub(vis_real a, vis_real b) { return _mm_sub_ps(a, b); }
static vis_real vis_mul(vis_real a, vis_real b) { return _mm_mul_ps(a, b); }
static vis_real vis_div(vis_real a, vis_real b) { return _mm_div_ps(a, b); }
static vis_real vis_sqrt(vis_real a) { return _mm_sqrt_ps(a); }
static vis_real vis_abs(vis_real a) { return _mm_and_ps(a, _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff))); }
static vis_real vis_min(vis_real a, vis_real b) { return _mm_min_ps(a, b); }
static vis_real vis_max(vis_real a, vis_real b) { return _mm_max_ps(a, b); }
static vis_real vis_greater(vis_real a, vis_real b) { return _mm_cmpgt_ps(a, b); }
static vis_real vis_less(vis_real a, vis_real b) { return _mm_cmplt_ps(a, b); }
static vis_real vis_at_least(vis_real a, vis_real b) { return _mm_cmpge_ps(a, b); }
static vis_real vis_and(vis_real a, vis_real b) { return _mm_and_ps(a, b); }
static vis_real vis_choose(vis_real mask, vis_real a, vis_real b) { return _mm_or_ps(_mm_and_ps(mask, a), _mm_andnot_ps(mask, b)); }
static vis_real vis_all_set(void) { return _mm_castsi128_ps(_mm_set1_epi32(-1)); }
static int vis_all(vis_real mask) { return _mm_movemask_ps(mask) == 15; }
static void vis_store_int(int *to, vis_real value) { _mm_storeu_si128((__m128i *)to, _mm_cvttps_epi32(value)); }
static vis_real vis_truncate(vis_real value) { return _mm_cvtepi32_ps(_mm_cvttps_epi32(value)); }
#elif defined(__ARM_NEON) && defined(__aarch64__)
#define VISIBILITY_VECTOR
typedef float32x4_t vis_real;
static vis_real vis_set(real value) { return vdupq_n_f32(value); }
static vis_real vis_load(const real *from) { return vld1q_f32(from); }
static void vis_store(real *to, vis_real value) { vst1q_f32(to, value); }
static vis_real vis_add(vis_real a, vis_real b) { return vaddq_f32(a, b); }
static vis_real vis_sub(vis_real a, vis_real b) { return vsubq_f32(a, b); }
static vis_real vis_mul(vis_real a, vis_real b) { return vmulq_f32(a, b); }
static vis_real vis_div(vis_real a, vis_real b) { return vdivq_f32(a, b); }
static vis_real vis_sqrt(vis_real a) { return vsqrtq_f32(a); }
static vis_real vis_abs(vis_real a) { return vabsq_f32(a); }
static vis_real vis_greater(vis_real a, vis_real b) { return vreinterpretq_f32_u32(vcgtq_f32(a, b)); }
static vis_real vis_less(vis_real a, vis_real b) { return vreinterpretq_f32_u32(vcltq_f32(a, b)); }
static vis_real vis_at_least(vis_real a, vis_real b) { return vreinterpretq_f32_u32(vcgeq_f32(a, b)); }
static vis_real vis_and(vis_real a, vis_real b) { return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b))); }
static vis_real vis_choose(vis_real mask, vis_real a, vis_real b) { return vbslq_f32(vreinterpretq_u32_f32(mask), a, b); }
static vis_real vis_min(vis_real a, vis_real b) { return vis_choose(vis_less(a, b), a, b); }
static vis_real vis_max(vis_real a, vis_real b) { return vis_choose(vis_greater(a, b), a, b); }
static vis_real vis_all_set(void) { return vreinterpretq_f32_u32(vdupq_n_u32(0xffffffffu)); }
static int vis_all(vis_real mask) { return vminvq_u32(vreinterpretq_u32_f32(mask)) == 0xffffffffu; }
static void vis_store_int(int *to, vis_real value) { vst1q_s32(to, vcvtq_s32_f32(value)); }
static vis_real vis_truncate(vis_real value) { return vcvtq_f32_s32(vcvtq_s32_f32(value)); }
#endif
#ifdef VISIBILITY_VECTOR
static void depth_visibility_row(real depths[6][OCC_SIZE * OCC_SIZE], const real xs[OCC_SIZE], real y, real z,
								 real extent, int resolution, real *output)
{
	vis_real zero = vis_set(0), one = vis_set(1), half = vis_set(.5f);
	vis_real py = vis_set(y), pz = vis_set(z), ey = vis_set(fabsf(y)), ez = vis_set(fabsf(z));
	vis_real ex = vis_set(extent), size = vis_set((real)resolution), limit = vis_set((real)(resolution - 1));
	vis_real facey = vis_set((real)(2 + (y > 0))), facez = vis_set((real)(4 + (z > 0)));
	int x, i, j, lane;
	for (x = 0; x < OCC_SIZE; x += 4)
	{
		vis_real px = vis_load(xs + x), magnitude = vis_abs(px);
		vis_real mask = vis_greater(ey, magnitude), u = vis_choose(mask, pz, py),
			   v = vis_choose(mask, px, pz);
		vis_real face = vis_choose(mask, facey, vis_and(vis_greater(px, zero), one));
		vis_real near_origin, tx, ty, fraction, hits[4], length;
		vis_real clear = vis_all_set(), result = zero;
		int faces[4], columns[4], rows[4];
		real gather[4];
		magnitude = vis_choose(mask, ey, magnitude);
		mask = vis_greater(ez, magnitude);
		magnitude = vis_choose(mask, ez, magnitude);
		face = vis_choose(mask, facez, face);
		u = vis_choose(mask, px, u);
		v = vis_choose(mask, py, v);
		near_origin = vis_less(magnitude, vis_set(.0001f));
		magnitude = vis_choose(near_origin, one, magnitude);
		u = vis_sub(vis_mul(vis_mul(vis_add(vis_div(u, magnitude), one), half), size), half);
		v = vis_sub(vis_mul(vis_mul(vis_add(vis_div(v, magnitude), one), half), size), half);
		u = vis_max(zero, vis_min(limit, u));
		v = vis_max(zero, vis_min(limit, v));
		vis_store_int(faces, face);
		vis_store_int(columns, u);
		vis_store_int(rows, v);
		tx = vis_sub(u, vis_truncate(u));
		ty = vis_sub(v, vis_truncate(v));
		fraction = vis_div(magnitude, ex);
		for (j = 0; j < 2; j++)
			for (i = 0; i < 2; i++)
			{
				for (lane = 0; lane < 4; lane++)
					gather[lane] = depths[faces[lane]][MIN(rows[lane] + j, resolution - 1) * OCC_SIZE +
													   MIN(columns[lane] + i, resolution - 1)];
				hits[j * 2 + i] = vis_load(gather);
				clear = vis_and(clear, vis_at_least(hits[j * 2 + i], fraction));
			}
		if (vis_all(clear))
		{
			result = vis_add(vis_mul(vis_sub(one, tx), vis_sub(one, ty)), vis_mul(tx, vis_sub(one, ty)));
			result = vis_add(result, vis_mul(vis_sub(one, tx), ty));
			result = vis_add(result, vis_mul(tx, ty));
		}
		else
		{
			length = vis_div(vis_mul(vis_sqrt(vis_add(vis_add(vis_mul(px, px), vis_mul(py, py)), vis_mul(pz, pz))), ex),
							 magnitude);
			for (j = 0; j < 2; j++)
				for (i = 0; i < 2; i++)
				{
					vis_real visible = vis_sub(one, vis_div(vis_mul(vis_sub(fraction, hits[j * 2 + i]), length), vis_set(.025f)));
					visible = vis_max(zero, vis_min(one, visible));
					result = vis_add(result, vis_mul(vis_mul(visible, i ? tx : vis_sub(one, tx)), j ? ty : vis_sub(one, ty)));
				}
		}
		vis_store(output + x, vis_choose(near_origin, one, result));
	}
}
#else
static void depth_visibility_row(real depths[6][OCC_SIZE * OCC_SIZE], const real xs[OCC_SIZE], real y, real z,
								 real extent, int resolution, real *output)
{
	int x;
	for (x = 0; x < OCC_SIZE; x++)
	{
		real_vector3d ray;
		ray.i = xs[x];
		ray.j = y;
		ray.k = z;
		output[x] = depth_visibility(depths, &ray, extent, resolution);
	}
}
#endif

static void reconstruct_visibility(real depths[6][OCC_SIZE * OCC_SIZE], real extent, int resolution, real dx, real dy,
								   real dz, real *output)
{
	real offsets[OCC_SIZE], xs[OCC_SIZE];
	int x, y, z;
	for (x = 0; x < OCC_SIZE; x++)
	{
		offsets[x] = (2.0f * (x + 0.5f) / OCC_SIZE - 1) * extent;
		xs[x] = offsets[x] + dx;
	}
	for (z = 0; z < OCC_SIZE; z++)
		for (y = 0; y < OCC_SIZE; y++)
			depth_visibility_row(depths, xs, offsets[y] + dy, offsets[z] + dz, extent, resolution,
								 output + (z * OCC_SIZE + y) * OCC_SIZE);
}

static real filter_visibility(real history, real target, real clear_alpha, real block_alpha)
{
	real alpha = target < history ? block_alpha : clear_alpha;
	real value = history + (target - history) * alpha;
	/* Render-only fade history can otherwise stick at subnormal values.
	   This cutoff is far below one 8-bit texture step and leaves the
	   simulation's floating-point mode untouched. */
	if (value < 1.0e-20f) value = 0;
	return value;
}
/* port: straight to the backend, sent in order with the draws (the texture's
   memory is never read: halo_field_texture_write). Texels past a radial pass's
   sphere are zero, so it says how far the pass reaches for the world-light clip. */
static int upload(IDirect3DVolumeTexture8 **texture, unsigned long const *pixels, int pass)
{
	if (!*texture && D3DDevice_CreateVolumeTexture(OCC_SIZE, OCC_SIZE, OCC_SIZE, 1, 0, D3DFMT_A8R8G8B8,
												   D3DPOOL_MANAGED, texture) < 0)
		return 0;
	return halo_field_texture_write(*texture, pixels, pass ? PLASMA_FIELD_RADIAL_REACH : -1.0f);
}
/* The ray grid per cube face. Prong glows, and lights on a weapon in a hand, are
   short, and move with the weapon on every frame: 96 rays, not 1536. They keep
   their occlusion. Every other light keeps the full grid. */
static int light_trace_resolution(long identity, long rasterizer_index)
{
	real axis[3], origin[3], radius;
	return plasma_lighting_prong_cone(rasterizer_index, axis, origin, &radius) ||
		covenant_emitter_lighting_held_weapon(identity) || muzzle_lighting_held_weapon(identity) ? OCC_HELD_RESOLUTION : OCC_SIZE;
}
/* The grid for a field's next trace. A new light has no depths to have moved
   from, and one that stops goes back to the full grid (update_field). */
static int field_trace_resolution(struct occlusion_field const *field, long rasterizer_index)
{
	struct rasterizer_light_submit_parameters const *light = &rasterizer_lights.lights[rasterizer_index];
	int resolution = light_trace_resolution(field->identity, rasterizer_index);
	if (resolution == OCC_SIZE && field->valid && lighting_moving_lights_384() &&
		(light->position.x != field->trace_center[0] || light->position.y != field->trace_center[1] ||
		 light->position.z != field->trace_center[2]))
		return OCC_MOVING_RESOLUTION;
	return resolution;
}
/* Keep the large reconstruction scratch off the repeated per-draw lookup
   path. Clang otherwise inlines this back into field_for, reserving/probing
   its large stack frame even on a cache hit. All ports build with Clang.
   This runs at most once per admitted field in a view. */
static unsigned long budget_clock(void)
{
	return lighting_budget()->time_us ? lighting_budget_clock_us() : 0;
}
static void budget_charge(unsigned long started)
{
	if (lighting_budget()->time_us)
		view_budget.us += (long)(lighting_budget_clock_us() - started);
}
/* A view with nothing held back always traces its first light. */
static int budget_allows(long rays, long reserved_rays, long reserved_us)
{
	const struct lighting_budget *budget = lighting_budget();
	if (!view_budget.traces && !reserved_rays && !reserved_us)
		return TRUE;
	if (budget->rays && view_budget.rays + rays + reserved_rays > budget->rays)
		return FALSE;
	if (budget->time_us && view_budget.us + reserved_us >= budget->time_us)
		return FALSE;
	return TRUE;
}
static real field_extent(long rasterizer_index)
{
	real specular = plasma_occlusion_specular_scale(rasterizer_index);
	return rasterizer_lights.lights[rasterizer_index].radius * MAX(1.0f, specular);
}
static void trace_field(struct occlusion_field *field, struct rasterizer_light_submit_parameters *light, real extent,
	int resolution, int vehicles, unsigned long scene_revision, unsigned long now)
{
	if (!field->valid)
		stats.trace_new++;
	else if (resolution == OCC_HELD_RESOLUTION)
		stats.trace_held++;
	else if (light->position.x != field->trace_center[0] || light->position.y != field->trace_center[1] ||
		light->position.z != field->trace_center[2] || field->trace_extent != extent)
		stats.trace_moved++;
	else
		stats.trace_scene++;
	stats.rays += resolution * resolution * 6;
	trace_depths(extent, &light->position, covenant_emitter_lighting_occlusion_ignore(field->identity),
		vehicles, resolution, field->depths);
	view_budget.rays += resolution * resolution * 6;
	view_budget.traces++;
	field->trace_resolution = resolution;
	field->scene_revision = scene_revision;
	field->traced = now;
	field->trace_extent = extent;
	field->trace_center[0] = light->position.x;
	field->trace_center[1] = light->position.y;
	field->trace_center[2] = light->position.z;
}
static int field_vehicles(long identity)
{
	return muzzle_lighting_vehicle_occlusion(identity) || plasma_lighting_vehicle_occlusion(identity) ||
		covenant_emitter_lighting_vehicle_occlusion(identity);
}
/* With a limit, draws can't decide who gets the rays, or the first light drawn
   would always win. Before the view's first field: the most out-of-date moved
   light first, then the next that fits, and the rest offset the depths they
   have. Only fields in recent use, as a light no draw reads needs no rays. */
static __attribute__((noinline)) void plan_traces(unsigned long now)
{
	const struct lighting_budget *budget = lighting_budget();
	struct occlusion_field *order[OCC_LIGHTS];
	struct occlusion_field *weapons[OCC_LIGHTS];
	long lights[OCC_LIGHTS];
	real scores[OCC_LIGHTS];
	long reserve_rays = 0, reserve_us = 0;
	int count = 0, weapon_count = 0, at_work = 0, other_wanted = FALSE, s, i, j;
	for (s = 0; s < submitted_count; s++)
	{
		struct occlusion_field *field = NULL;
		struct rasterizer_light_submit_parameters *light;
		long rasterizer_index = submitted[s].rasterizer;
		real extent, dx, dy, dz, distance, score;
		if (rasterizer_index < 0 || rasterizer_index >= rasterizer_lights.light_count)
			continue;
		for (i = 0; i < OCC_LIGHTS; i++)
			if (fields[i].valid && fields[i].identity == submitted[s].identity)
				field = &fields[i];
		if (!field)
		{
			/* a draw asked for this one lately and was refused: room for it,
			   if it has a field to go to */
			for (i = 0; i < OCC_SUBMITTED; i++)
				if (overflow[i].valid && overflow[i].identity == submitted[s].identity && now - overflow[i].seen <= 50)
				{
					for (j = 0; j < OCC_LIGHTS; j++)
						if (!fields[j].valid || now - fields[j].used > 100)
						{
							int resolution = light_trace_resolution(submitted[s].identity, rasterizer_index);
							reserve_rays = MAX(reserve_rays, (long)resolution * resolution * 6);
							reserve_us = budget->time_us / 2;
							if (resolution == OCC_SIZE)
								other_wanted = TRUE;
							break;
						}
					break;
				}
			continue;
		}
		if (current_window - field->consumed > OCC_UNREAD_VIEWS || count == OCC_LIGHTS)
			continue;
		if (!field->settled)
		{
			if (light_trace_resolution(field->identity, rasterizer_index) != OCC_HELD_RESOLUTION)
			{
				at_work++;
				other_wanted = TRUE;
			}
			else if (weapon_count < OCC_LIGHTS)
				weapons[weapon_count++] = field;
		}
		light = &rasterizer_lights.lights[rasterizer_index];
		extent = field_extent(rasterizer_index);
		if (!(extent > 0.001f && extent < 1000.0f))
			continue;
		dx = light->position.x - field->trace_center[0];
		dy = light->position.y - field->trace_center[1];
		dz = light->position.z - field->trace_center[2];
		distance = dx * dx + dy * dy + dz * dz;
		score = distance / (extent * extent);
		if (score <= 0.015625f && (now - field->traced < 12 || (distance == 0 && field->trace_extent == extent)))
			continue;
		score += (real)(now - field->traced) * 0.001f;
		for (i = count; i > 0 && scores[i - 1] < score; i--)
		{
			order[i] = order[i - 1];
			lights[i] = lights[i - 1];
			scores[i] = scores[i - 1];
		}
		order[i] = field;
		lights[i] = rasterizer_index;
		scores[i] = score;
		count++;
	}
	/* Weapon lights go first, and three of them would leave every other light with
	   no field at all, so no occlusion. When another light wants one, they leave it
	   a slot: the one updated most lately sits out, so none waits two views running. */
	view_budget.weapon_limit = other_wanted ? MAX(budget->fields - 1, 1) : budget->fields;
	while (weapon_count > view_budget.weapon_limit)
	{
		int latest = 0;
		for (i = 1; i < weapon_count; i++)
			if ((long)(weapons[i]->updated - weapons[latest]->updated) > 0)
				latest = i;
		weapons[latest]->waits_window = current_window;
		weapons[latest] = weapons[--weapon_count];
	}
	/* Rays are only held for a light that can get in: with every slot taken they
	   would go unused, and short the lights that are at work. */
	if (at_work + weapon_count < budget->fields)
	{
		view_budget.reserved_rays = reserve_rays;
		view_budget.reserved_us = reserve_us;
	}
	for (i = 0; i < count; i++)
	{
		int resolution = field_trace_resolution(order[i], lights[i]);
		int vehicles = field_vehicles(order[i]->identity);
		struct rasterizer_light_submit_parameters *light = &rasterizer_lights.lights[lights[i]];
		real extent = field_extent(lights[i]);
		unsigned long started;
		if (order[i]->traced_window == current_window || order[i]->waits_window == current_window ||
			!budget_allows((long)resolution * resolution * 6, view_budget.reserved_rays, view_budget.reserved_us))
			continue;
		started = budget_clock();
		trace_field(order[i], light, extent, resolution, vehicles,
			plasma_collision_scene_revision(&order[i]->scene, &light->position, extent, vehicles), now);
		order[i]->traced_window = current_window;
		budget_charge(started);
	}
}
static __attribute__((noinline)) struct occlusion_field *update_field(struct occlusion_field *field, long rasterizer_index, unsigned long now)
{
	struct rasterizer_light_submit_parameters *light;
	int i, x, y, z, stationary, vehicles, resolution, retraced, due, moved, revision_known = FALSE, retrace, refused = FALSE;
	unsigned long scene_revision = 0;
	real elapsed, extent, specular, clear_alpha, block_alpha, admission;
	real previous[OCC_COUNT];
	real projected[OCC_COUNT];
	struct sample_axis history_axes[3][OCC_SIZE];
	real trace_dx, trace_dy, trace_dz;
	fields_dirty = 1;
	field->used = now;
	field->settled = FALSE;
	light = &rasterizer_lights.lights[rasterizer_index];
	specular = plasma_occlusion_specular_scale(rasterizer_index);
	extent = field_extent(rasterizer_index);
	if (!(extent > 0.001f && extent < 1000.0f))
		return NULL;
	updates_this_window++;
	elapsed = field->valid ? MIN((real)(now - field->updated) * 0.001f, 0.1f) : 0.1f;
	trace_dx = light->position.x - field->trace_center[0];
	trace_dy = light->position.y - field->trace_center[1];
	trace_dz = light->position.z - field->trace_center[2];
	vehicles = field_vehicles(field->identity);
	retraced = field->traced_window == current_window;
	resolution = field_trace_resolution(field, rasterizer_index);
	/* Cache collision depths briefly, but project and fade at every rendered pose.
	   Exact stationary poses can retain depths while all collision inputs are
	   unchanged. Geometry changes follow the existing 12 ms trace cadence;
	   they cannot be mistaken for a permanently unchanged stationary scene. */
	due = now - field->traced >= 12;
	moved = trace_dx != 0 || trace_dy != 0 || trace_dz != 0;
	retrace = !field->valid || trace_dx * trace_dx + trace_dy * trace_dy + trace_dz * trace_dz > extent * extent * 0.015625f;
	/* port: the scene is only looked at when it can decide: the cadence is due,
	   and nothing else already asks for a trace. A moved light or a changed
	   extent traces at the cadence; between traces the extent only rescales. */
	if (!retrace && due)
	{
		if (moved || field->trace_extent != extent)
			retrace = TRUE;
		else
		{
			scene_revision = plasma_collision_scene_revision(&field->scene, &light->position, extent, vehicles);
			revision_known = TRUE;
			retrace = field->scene_revision != scene_revision || field->trace_resolution < resolution;
		}
	}
	/* port: no rays left this view: the depths it has, offset, if it is near them.
	   A field never traced has no depths: field_for only lets one in with room. */
	if (retrace && field->valid &&
		!budget_allows((long)resolution * resolution * 6, view_budget.reserved_rays, view_budget.reserved_us))
	{
		retrace = FALSE;
		refused = TRUE;
		stats.refused++;
	}
	if (refused && trace_dx * trace_dx + trace_dy * trace_dy + trace_dz * trace_dz > extent * extent * 0.25f)
	{
		field->too_far = TRUE;
		field->window = current_window;
		stats.too_far++;
		return NULL;
	}
	field->too_far = FALSE;
	if (retrace && !revision_known)
		scene_revision = plasma_collision_scene_revision(&field->scene, &light->position, extent, vehicles);
	if (!retrace && field->trace_extent != extent)
	{
		rescale_depths(field->depths, field->trace_extent, extent);
		field->trace_extent = extent;
	}
	if (retrace)
	{
		trace_field(field, light, extent, resolution, vehicles, scene_revision, now);
		retraced = TRUE;
		trace_dx = trace_dy = trace_dz = 0;
	}
	memcpy(previous, field->visibility, sizeof(previous));
	stationary = field->valid && field->extent == extent &&
		field->center[0] == light->position.x && field->center[1] == light->position.y &&
		field->center[2] == light->position.z;
	admission = field->fading ? MIN(1.0f, (real)(now - field->admitted) / 80.0f) : 1.0f;
	admission = admission * admission * (3.0f - 2.0f * admission);
	clear_alpha = 1.0f - powf(0.5f, elapsed / 0.025f);
	block_alpha = 1.0f - powf(0.5f, elapsed / 0.008f);
	/* Translation and uniform scale are separable: each axis has sixteen
	   coordinates, shared by all 4096 cells. Keep the original arithmetic
	   and interpolation order, including the outside-history fallback. */
	if (!stationary && field->valid && field->extent > 0)
	{
		for (i = 0; i < OCC_SIZE; i++)
		{
			real offset = (2.0f * (i + 0.5f) / OCC_SIZE - 1) * extent;
			sample_axis_initialize(&history_axes[0][i], 0.5f + (light->position.x + offset - field->center[0]) / (2 * field->extent));
			sample_axis_initialize(&history_axes[1][i], 0.5f + (light->position.y + offset - field->center[1]) / (2 * field->extent));
			sample_axis_initialize(&history_axes[2][i], 0.5f + (light->position.z + offset - field->center[2]) / (2 * field->extent));
		}
		reproject_rows(previous, history_axes, projected);
	}
	/* Unchanged poses only advance fades. This flat loop can vectorize without
	   ray construction or history-reprojection branches. */
	if (stationary && !retraced)
	{
		stats.fade++;
		for (i = 0; i < OCC_COUNT; i++)
			field->visibility[i] = filter_visibility(previous[i], field->targets[i], clear_alpha, block_alpha);
	}
	else
	{
		if (resolution == OCC_HELD_RESOLUTION)
			stats.rebuild_held++;
		else if (stationary)
			stats.rebuild_stationary++;
		else
			stats.rebuild_moved++;
		reconstruct_visibility(field->depths, extent, field->trace_resolution, trace_dx, trace_dy, trace_dz, field->targets);
		for (z = 0; z < OCC_SIZE; z++)
			for (y = 0; y < OCC_SIZE; y++)
				for (x = 0; x < OCC_SIZE; x++)
				{
					real target, history;
					i = (z * OCC_SIZE + y) * OCC_SIZE + x;
					target = field->targets[i];
					/* Reproject history at the receiver, not at a moving light-local cell. */
					history = target;
					if (stationary)
						history = previous[i];
					else if (field->valid && field->extent > 0)
					{
						if (history_axes[0][x].inside && history_axes[1][y].inside && history_axes[2][z].inside)
							history = sample_history_rows(projected, x, y, &history_axes[2][z]);
					}
					field->visibility[i] = filter_visibility(history, target, clear_alpha, block_alpha);
				}
	}

	/* Texture coordinates are light-local. A changed world position alone
	   does not change texels if the projected visibility stayed identical. */
	if (!field->valid || field->radius != light->radius || field->specular != specular ||
		field->extent != extent || field->admission != admission ||
		memcmp(previous, field->visibility, sizeof(previous)))
		field->texture_mask = 0;
	else
	{
		field->texture_mask &= 7; /* failed uploads retry next view */
		if (stationary && !retraced)
		{
			stats.fade_settled++;
			field->settled = TRUE;
		}
	}
	field->center[0] = light->position.x;
	field->center[1] = light->position.y;
	field->center[2] = light->position.z;
	field->radius = light->radius;
	field->specular = specular;
	field->extent = extent;
	field->admission = admission;
	field->updated = now;
	field->window = current_window;
	field->valid = 1;
	return field;
}
/* port: a settled field has no work this view, so the light costs a scene check
   and doesn't count against budget->fields: still lights can't keep a moving one
   from having a field. Anything that would change its texels wakes it. */
static int field_at_rest(struct occlusion_field *field, long rasterizer_index)
{
	struct rasterizer_light_submit_parameters const *light = &rasterizer_lights.lights[rasterizer_index];
	real specular = plasma_occlusion_specular_scale(rasterizer_index);
	real extent = field_extent(rasterizer_index);
	return field->settled && !(field->texture_mask & ~7) &&
		field->center[0] == light->position.x && field->center[1] == light->position.y &&
		field->center[2] == light->position.z && field->radius == light->radius &&
		field->specular == specular && field->extent == extent && field->trace_extent == extent &&
		field->trace_resolution >= field_trace_resolution(field, rasterizer_index) &&
		field->scene_revision == plasma_collision_scene_revision(&field->scene, &light->position, extent, field_vehicles(field->identity));
}
/* A field with no work this view keeps last view's texels, if the light is still
   near where they were made. */
static int field_near(struct occlusion_field const *field, long rasterizer_index)
{
	struct rasterizer_light_submit_parameters const *light = &rasterizer_lights.lights[rasterizer_index];
	real dx = light->position.x - field->center[0];
	real dy = light->position.y - field->center[1];
	real dz = light->position.z - field->center[2];
	return dx * dx + dy * dy + dz * dz <= field->extent * field->extent * 0.0625f;
}
static int field_wait(struct occlusion_field *field, long rasterizer_index, unsigned long now)
{
	if (!field_near(field, rasterizer_index))
		return FALSE;
	stats.waited++;
	field->used = now;
	field->window = current_window;
	field->too_far = FALSE;
	return TRUE;
}
static struct occlusion_field *field_update_for(long rasterizer_index)
{
	struct occlusion_field *field = NULL, *updated;
	const struct lighting_budget *budget = lighting_budget();
	unsigned long now = GetTickCount(), started;
	long identity = NONE;
	int i, resolution;
	if (!enabled() || rasterizer_index < 0 || rasterizer_index >= rasterizer_lights.light_count)
		return NULL;
	for (i = 0; i < submitted_count; i++)
		if (submitted[i].rasterizer == rasterizer_index)
			identity = submitted[i].identity;
	if (identity == NONE)
		return NULL;
	if (!view_budget.planned)
	{
		view_budget.planned = TRUE;
		if (budget->rays || budget->time_us)
			plan_traces(now);
	}
	for (i = 0; i < OCC_LIGHTS; i++)
		if (fields[i].valid && fields[i].identity == identity)
			field = &fields[i];
	if (field && field->window == current_window)
		return field->too_far ? NULL : field;
	if (field && field_at_rest(field, rasterizer_index))
	{
		stats.at_rest++;
		field->used = field->updated = now;
		field->window = current_window;
		return field;
	}
	/* port: out of time: last view's field, if the light is still near it. One
	   plan_traces traced this view still gets the good of its rays. */
	if (field && field->traced_window != current_window && budget->time_us && view_budget.us >= budget->time_us)
	{
		field->used = now;
		field->window = current_window;
		field->too_far = !field_near(field, rasterizer_index);
		return field->too_far ? NULL : field;
	}
	/* (past its limit, one too far from its texels asks like any other) */
	if (field && field->waits_window == current_window && field_wait(field, rasterizer_index, now))
		return field;
	if (updates_this_window >= budget->fields)
	{
		/* a light that woke from rest and was turned away is still asking */
		if (field)
			field->settled = FALSE;
		stats.overflow++;
		remember_overflow(identity, now);
		return NULL;
	}
	resolution = light_trace_resolution(identity, rasterizer_index);
	if (resolution == OCC_HELD_RESOLUTION && view_budget.weapon_limit && view_budget.weapon_updates >= view_budget.weapon_limit)
	{
		if (field && field_wait(field, rasterizer_index, now))
			return field;
		stats.overflow++;
		remember_overflow(identity, now);
		return NULL;
	}
	if (!field)
	{
		if (!budget_allows((long)resolution * resolution * 6, 0, 0))
		{
			stats.overflow++;
			remember_overflow(identity, now);
			return NULL;
		}
		for (i = 0; i < OCC_LIGHTS; i++)
			if (!fields[i].valid || now - fields[i].used > 100)
			{
				field = &fields[i];
				break;
			}
		if (!field)
		{
			stats.overflow++;
			remember_overflow(identity, now);
			return NULL; /* active lights are never evicted */
		}
		field->valid = 0;
		field->traced_window = 0;
		field->waits_window = 0;
		field->too_far = FALSE;
		field->consumed = current_window;
		field->passes_used = 0;
		field->prepared = FALSE;
		field->settled = FALSE;
		view_budget.reserved_rays = view_budget.reserved_us = 0;
		field->identity = identity;
		field->admitted = now;
		field->fading = 0;
		for (i = 0; i < OCC_SUBMITTED; i++)
			if (overflow[i].valid && overflow[i].identity == identity && now - overflow[i].seen < 200)
				field->fading = 1;
		for (i = 0; i < OCC_COUNT; i++)
			field->visibility[i] = 1;
	}
	if (resolution == OCC_HELD_RESOLUTION)
		view_budget.weapon_updates++;
	started = budget_clock();
	updated = update_field(field, rasterizer_index, now);
	budget_charge(started);
	return updated;
}
/* What a draw asks for: only that counts as a light being read. */
static struct occlusion_field *field_for(long rasterizer_index)
{
	struct occlusion_field *field = field_update_for(rasterizer_index);
	if (field)
		field->consumed = current_window;
	return field;
}
/* Build only a texture actually consumed by this view. CPU visibility history
   stays independent of pass order, and each pass uploads at most once. */
/* Keep the pixel/row scratch out of cache hits, including under LTO. */
static __attribute__((noinline)) void *prepare_field_texture(struct occlusion_field *field, int pass, int directional, real const axis[3])
{
	unsigned long pixels[OCC_COUNT];
	real rows[OCC_COUNT];
	struct sample_axis half_axes[3][OCC_SIZE];
	real prong_mask[OCC_COUNT];
	int x, y, z, i;
	real scale;
	stats.textures++;
	scale = (pass == 2 ? field->specular : 1.0f) * field->radius / field->extent;
	if (pass && scale == 0.5f)
	{
		half_sample_axes(half_axes);
		reproject_rows(field->visibility, half_axes, rows);
	}
	/* The ordinary model mask needs no radial/directional calculation or
	   resampling. A flat conversion keeps its common path vectorizable. */
	if (!pass && !directional)
	{
		for (i = 0; i < OCC_COUNT; i++)
			pixels[i] = (unsigned long)(255 * (1 - field->admission + field->admission * field->visibility[i]) + 0.5f) * 0x01010101UL;
	}
	else if (pass && !directional && (scale == 0.5f || scale == 1.0f))
	{
		/* Ordinary radial masks use half-size or unit-size sampling. Resolve
		   the source slices once per output slice, leaving contiguous
		   rows and no profile/resampling branches in the conversion loop. */
		for (z = 0; z < OCC_SIZE; z++)
		{
			real const *a = scale == 1.0f ? field->visibility + z * OCC_SIZE * OCC_SIZE : rows + half_axes[2][z].a * OCC_SIZE * OCC_SIZE;
			real const *b = scale == 1.0f ? a : rows + half_axes[2][z].b * OCC_SIZE * OCC_SIZE;
			real blend = scale == 1.0f ? 0 : half_axes[2][z].t;
			real dz = 2.0f * (z + 0.5f) / OCC_SIZE - 1;
			for (y = 0; y < OCC_SIZE; y++)
			{
				real dy = 2.0f * (y + 0.5f) / OCC_SIZE - 1;
				for (x = 0; x < OCC_SIZE; x++)
				{
					int row = y * OCC_SIZE + x;
					real dx = 2.0f * (x + 0.5f) / OCC_SIZE - 1;
					real remaining = MAX(0.0f, 1 - dx * dx - dy * dy - dz * dz);
					real value = a[row] + (b[row] - a[row]) * blend;
					value = 1 - field->admission + field->admission * value;
					value *= remaining * remaining;
					pixels[z * OCC_SIZE * OCC_SIZE + row] = (unsigned long)(255 * value + 0.5f) * 0x01010101UL;
				}
			}
		}
	}
	else
	{
		if (directional == 3)
			plasma_prong_falloff_mask(axis, prong_mask);
		for (z = 0; z < OCC_SIZE; z++)
			for (y = 0; y < OCC_SIZE; y++)
				for (x = 0; x < OCC_SIZE; x++)
				{
					real dx = 2.0f * (x + 0.5f) / OCC_SIZE - 1;
					real dy = 2.0f * (y + 0.5f) / OCC_SIZE - 1;
					real dz = 2.0f * (z + 0.5f) / OCC_SIZE - 1;
					real remaining = MAX(0.0f, 1 - dx * dx - dy * dy - dz * dz);
					real value;
					i = (z * OCC_SIZE + y) * OCC_SIZE + x;
					/* Unit-scale samples land exactly at texel centres. Outside the
					   attenuation sphere the result is zero regardless of visibility. */
					value = field->visibility[i];
					if (pass && remaining > 0 && scale != 1.0f)
						value = scale == 0.5f ? sample_history_rows(rows, x, y, &half_axes[2][z]) :
							sample_field(field->visibility, 0.5f + dx * scale * 0.5f,
								0.5f + dy * scale * 0.5f, 0.5f + dz * scale * 0.5f);
					value = 1 - field->admission + field->admission * value;
					if (directional)
						value *= directional == 3 ? prong_mask[i] : directional == 2 ? needler_falloff_factor(dz *
							(pass == 0 ? field->extent / field->radius : pass == 2 ? field->specular : 1.0f)) :
							ammo_counter_falloff_factor(axis, dx, dy, dz);
					if (pass)
						value *= remaining * remaining;
					pixels[i] = (unsigned long)(255 * value + 0.5f) * 0x01010101UL;
				}
	}
	if (!(field->uploaded_mask & (1 << pass)) || memcmp(field->uploaded[pass], pixels, sizeof(pixels)))
	{
		if (!upload(&field->texture[pass], pixels, pass))
		{
			/* A failed unlock may have changed backing storage. */
			field->uploaded_mask &= ~(1 << pass);
			field->texture_mask |= 8 << pass;
			return NULL;
		}
		stats.uploads++;
		memcpy(field->uploaded[pass], pixels, sizeof(pixels));
		field->uploaded_mask |= 1 << pass;
	}
	field->texture_mask |= 1 << pass;
	return field->texture[pass];
}
static void *field_texture(struct occlusion_field *field, int pass, long rasterizer)
{
	real axis[3], origin[3], radius;
	int directional = covenant_emitter_lighting_cone(rasterizer, axis, origin, &radius);
	if (!directional) directional = plasma_lighting_prong_cone(rasterizer, axis, origin, &radius);
	if (!field)
		return NULL;
	/* Directional profiles can rotate without changing collision visibility.
	   Needler's lower-height fade is independent of the weapon's aim. */
	if (directional != field->directional ||
		(directional && directional != 2 && plasma_direction_moved(field->direction, axis)))
	{
		field->texture_mask = 0;
		field->directional = directional;
		if (directional && directional != 2) memcpy(field->direction, axis, sizeof(field->direction));
	}
	/* Equal radii produce identical diffuse/specular texels, including the
	   directional mask. Share that upload; different extents stay independent. */
	if (pass == 2 && field->specular == 1.0f)
		pass = 1;
	/* A failed pass falls back for the rest of this view, then retries next view. */
	if (field->texture_mask & (8 << pass))
		return NULL;
	if (field->texture_mask & (1 << pass))
		return field->texture[pass];
	{
		unsigned long started = budget_clock();
		/* (the direction it was last built for: see plasma_direction_moved) */
		void *texture = prepare_field_texture(field, pass, directional, field->direction);
		budget_charge(started);
		return texture;
	}
}
void plasma_occlusion_reset(void)
{
	int i, j;
	ammo_counter_falloff_dispose();
	/* The disabled frame-end path may request reset repeatedly. Clear the
	   bounded upload/visibility storage only after it has actually been used. */
	if (fields_dirty)
	{
		for (i = 0; i < OCC_LIGHTS; i++)
			for (j = 0; j < 3; j++)
				if (fields[i].texture[j])
				{
					halo_field_texture_forget(fields[i].texture[j]);
					D3DResource_Release((D3DResource *)fields[i].texture[j]);
				}
		memset(fields, 0, sizeof(fields));
		fields_dirty = 0;
	}
	memset(overflow, 0, sizeof(overflow));
	plasma_occlusion_window();
}


static void stats_second(void)
{
	unsigned long now = GetTickCount();
	stats.views++;
	if (now - stats.logged < 1000)
		return;
	if (lighting_config_boolean("debug.gpu_stats") &&
		(stats.fade || stats.rebuild_held || stats.rebuild_moved || stats.rebuild_stationary || stats.too_far || stats.overflow || stats.at_rest || stats.waited))
		platform_log("lighting: %ld views; traces %ld new, %ld held, %ld moved, %ld scene changed (%ld rays, %ld refused); "
			"rebuilds %ld held, %ld moved, %ld stationary; fades %ld (%ld settled); textures %ld built, %ld uploaded; "
			"%ld too far, %ld overflow, %ld at rest, %ld waited; lights %ld submitted, %ld promoted (%ld bolts of %ld, %ld weapon glows), %ld shrunk",
			stats.views, stats.trace_new, stats.trace_held, stats.trace_moved, stats.trace_scene, stats.rays, stats.refused,
			stats.rebuild_held, stats.rebuild_moved, stats.rebuild_stationary, stats.fade, stats.fade_settled,
			stats.textures, stats.uploads, stats.too_far, stats.overflow, stats.at_rest, stats.waited,
				stats.submitted, stats.promoted, stats.bolts, stats.bolts_seen, stats.glows, stats.shrunk);
	memset(&stats, 0, sizeof(stats));
	stats.logged = now;
}
void plasma_occlusion_note_kinds(int bolts_seen, int bolts, int glows)
{
	stats.bolts_seen += bolts_seen;
	stats.bolts += bolts;
	stats.glows += glows;
}
void plasma_occlusion_note_promoted(int promoted, int shrunk)
{
	stats.promoted += promoted;
	stats.shrunk += shrunk;
}
void plasma_occlusion_window(void)
{
	stats.submitted += submitted_count;
	stats_second();
	ammo_counter_falloff_window();
	current_window++;
	submitted_count = 0;
	updates_this_window = 0;
	memset(&view_budget, 0, sizeof(view_budget));
	model_slots[0] = model_slots[1] = NONE;
}
unsigned long plasma_occlusion_view_serial(void)
{
	return current_window;
}
void plasma_occlusion_submit(long light, long rasterizer_index)
{
	/* port: every light follows Light Occlusion: see enabled() */
	if (lighting_config_boolean("display.plasma_occlusion") &&
		(lighting_config_boolean("display.plasma_lighting") || authored_emitter_lighting_enabled() ||
		plasma_lighting_vehicle_occlusion(light) || covenant_emitter_lighting_vehicle_occlusion(light) ||
		muzzle_lighting_selected(rasterizer_index) ||
		covenant_emitter_lighting_selected(rasterizer_index) || shield_glow_selected(rasterizer_index)) &&
		submitted_count < OCC_SUBMITTED)
	{
		submitted[submitted_count].identity = light;
		submitted[submitted_count++].rasterizer = rasterizer_index;
	}
}
/* port: every field this view will read, at once, before its first draw: under
   Vulkan an upload ends the rendering it interrupts, and under deko3d each one
   between draws costs two full barriers. A draw asking for anything missed still
   gets it, the old way. Short lights go first, as held weapons' should not lose a
   field to something across the map. */
void plasma_occlusion_prepare_view(void)
{
	int round, s, i, pass;
	if (!enabled())
		return;
	for (round = 0; round < 2; round++)
		for (s = 0; s < submitted_count; s++)
		{
			struct occlusion_field *field = NULL;
			long rasterizer_index = submitted[s].rasterizer, identity = submitted[s].identity;
			int mask;
			if (rasterizer_index < 0 || rasterizer_index >= rasterizer_lights.light_count ||
				(light_trace_resolution(identity, rasterizer_index) != OCC_SIZE) != !round ||
				!plasma_occlusion_light_visible(rasterizer_index, field_extent(rasterizer_index)))
				continue;
			for (i = 0; i < OCC_LIGHTS; i++)
				if (fields[i].valid && fields[i].identity == identity)
					field = &fields[i];
			/* in view, and no draw has read it lately: no receiver near it */
			if (field && current_window - field->consumed > OCC_UNREAD_VIEWS)
				continue;
			field = field_update_for(rasterizer_index);
			if (!field)
				continue;
			/* a new field's are a guess: the world's diffuse pass and models */
			mask = field->prepared ? field->passes_used : 1 << 0 | 1 << 1;
			field->prepared = TRUE;
			field->passes_used = 0;
			for (pass = 0; pass < 3; pass++)
				if (mask & 1 << pass)
					field_texture(field, pass, rasterizer_index);
		}
}
void plasma_occlusion_model_slot(long rasterizer_index, short slot)
{
	if (slot >= 0 && slot < 2)
		model_slots[slot] = rasterizer_index;
}
void *plasma_occlusion_texture(long rasterizer_index, int specular)
{
	struct occlusion_field *field = field_for(rasterizer_index);
	void *texture;
	if (field)
		field->passes_used |= 1 << (specular ? 2 : 1);
	texture = field_texture(field, specular ? 2 : 1, rasterizer_index);
	return texture ? texture : ammo_counter_falloff_texture(rasterizer_index, specular ? 2 : 1, NULL);
}
void *plasma_occlusion_model_texture(int slot, float transform[4])
{
	struct occlusion_field *field;
	if (slot < 0 || slot >= 2)
		return NULL;
	field = field_for(model_slots[slot]);
	if (!field)
		return ammo_counter_falloff_texture(model_slots[slot], 0, transform);
	field->passes_used |= 1 << 0;
	transform[0] = field->center[0];
	transform[1] = field->center[1];
	transform[2] = field->center[2];
	transform[3] = 0.5f / field->extent;
	{
		void *texture = field_texture(field, 0, model_slots[slot]);
		return texture ? texture : ammo_counter_falloff_texture(model_slots[slot], 0, transform);
	}
}
