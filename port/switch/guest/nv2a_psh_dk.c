/*
NV2A_PSH_DK.C

Translation of Xbox pixel shaders - the NV2A texture shader stages and
register combiners, as held in the pixel shader render states - into GLSL
for UAM, for the Switch's deko3d renderer (port/switch/DEKO3D.md, phase 4).
Copied from the OpenGL renderer's nv2a_psh.c (port/linux/src), which the
other platforms keep; what differs is the GLSL dialect, not the
translation. Any change to what this file writes is a change to the
shaders themselves and must raise DK_SHADER_GENERATOR_VERSION
(dk_shaders.h), so the cache on the card is compiled again:

- #version 460, and every uniform in a block with an explicit binding:
  UAM rejects uniforms outside blocks, and blocks and samplers need
  bindings (dk_shaders.h). The OpenGL renderer's loose alpha_reference is
  a vec4's x component here.
- Explicit locations on the inputs, the same the vertex generator writes:
  UAM links nothing, so a vertex output and the pixel input it feeds meet
  only by location. All nine are always declared, read or not, so that any
  pixel shader goes with any vertex shader.
- No OpenGL ES branches: no precision statements and no texture_lod_bias
  (under deko3d the LOD bias is the sampler's, DkSampler.lodBias), and no
  occlusion counting - the visibility tests count with
  DkCounter_SamplesPassed, so the key's count_samples is ignored.

A pixel shader runs in three parts:
- texture stages 0-3 (PSTextureModes) fetch t0-t3, some of them using the
  result of an earlier stage (dot product and bump environment modes);
- up to eight general combiner stages (PSRGBInputs/Outputs and
  PSAlphaInputs/Outputs) that each compute A*B, C*D and their sum or mux
  on the registers and write them back;
- the final combiner (PSFinalCombinerInputsABCD/EFG):
  rgb = A*B + (1-A)*C + D, alpha = G.
Register values are clamped to [-1, 1] between stages, as on the hardware.

The generator follows the range each register's channels can hold (struct
ranges): textures and vertex colours are in [0, 1], the constants too (they
are 8-bit colours), and a combiner's outputs in what its inputs make of
them. A mapping's max(x, 0) and a clamp are written only where the value can
be outside them, so the GLSL computes the same values with fewer
instructions (a third of the GPU's time on the level's passes was the
combiners' arithmetic).
*/

#include "xgpu.h"
#include "dk_shaders.h"
#include "port_config.h"

#include <stdio.h>
#include <stdlib.h>

enum
{
	_register_zero = 0,
	_register_c0 = 1,
	_register_c1 = 2,
	_register_fog = 3,
	_register_v0 = 4,
	_register_v1 = 5,
	_register_t0 = 8,
	_register_t1 = 9,
	_register_t2 = 10,
	_register_t3 = 11,
	_register_r0 = 12,
	_register_r1 = 13,
	_register_v1r0_sum = 14,
	_register_ef_product = 15,
};

enum
{
	_mode_none = 0x00,
	_mode_project2d = 0x01,
	_mode_project3d = 0x02,
	_mode_cubemap = 0x03,
	_mode_passthru = 0x04,
	_mode_clipplane = 0x05,
	_mode_bumpenvmap = 0x06,
	_mode_bumpenvmap_luminance = 0x07,
	_mode_brdf = 0x08,
	_mode_dot_st = 0x09,
	_mode_dot_zw = 0x0a,
	_mode_dot_reflect_diffuse = 0x0b,
	_mode_dot_reflect_specular = 0x0c,
	_mode_dot_str_3d = 0x0d,
	_mode_dot_str_cube = 0x0e,
	_mode_dependent_ar = 0x0f,
	_mode_dependent_gb = 0x10,
	_mode_dot_product = 0x11,
	_mode_dot_reflect_specular_constant = 0x12,
};

/* ---------- the ranges of the registers' values */

struct range
{
	float low, high;
};

/* each register's rgb and alpha (by the register numbers above) */
struct ranges
{
	struct range rgb[16];
	struct range alpha[16];
};

/* debug.gpu_shader_ranges (true unless set false): whether the ranges are
used. false writes every max and clamp, as before them, to compare the two */
static BOOL ranges_used(void)
{
	static int looked, used;

	if (!looked)
	{
		looked = 1;
		used = config_boolean("debug.gpu_shader_ranges");
	}
	return used;
}

static struct range range_make(float low, float high)
{
	struct range range = { low, high };

	return range;
}

static float minimum4(float a, float b, float c, float d)
{
	float m = a < b ? a : b;

	m = m < c ? m : c;
	return m < d ? m : d;
}

static float maximum4(float a, float b, float c, float d)
{
	float m = a > b ? a : b;

	m = m > c ? m : c;
	return m > d ? m : d;
}

static struct range range_multiply(struct range a, struct range b)
{
	return range_make(minimum4(a.low * b.low, a.low * b.high, a.high * b.low, a.high * b.high),
		maximum4(a.low * b.low, a.low * b.high, a.high * b.low, a.high * b.high));
}

static struct range range_add(struct range a, struct range b)
{
	return range_make(a.low + b.low, a.high + b.high);
}

static struct range range_union(struct range a, struct range b)
{
	return range_make(a.low < b.low ? a.low : b.low, a.high > b.high ? a.high : b.high);
}

/* x * scale + offset, scale positive */
static struct range range_scale(struct range a, float scale, float offset)
{
	return range_make(a.low * scale + offset, a.high * scale + offset);
}

static struct range range_clamp(struct range a, float low, float high)
{
	return range_make(a.low < low ? low : a.low > high ? high : a.low, a.high > high ? high : a.high < low ? low : a.high);
}

/* clamp(value, low, high), or as much of it as the value's range needs */
static void append_clamped(struct xgpu_text *text, const char *value, struct range range, float low, float high)
{
	if (!ranges_used())
		xgpu_text_append(text, "clamp(%s, %.1f, %.1f)", value, low, high);
	else if (range.low >= low && range.high <= high)
		xgpu_text_append(text, "(%s)", value);
	else if (range.low >= low)
		xgpu_text_append(text, "min(%s, %.1f)", value, high);
	else if (range.high <= high)
		xgpu_text_append(text, "max(%s, %.1f)", value, low);
	else
		xgpu_text_append(text, "clamp(%s, %.1f, %.1f)", value, low, high);
}

/* the range of the channels an input reads: alpha, or blue in the alpha
portion, else rgb */
static struct range input_range(const struct ranges *ranges, unsigned long reg, BOOL alpha_portion, BOOL alpha_channel)
{
	if (alpha_channel)
		return ranges->alpha[reg & 0x0f];
	(void)alpha_portion;
	return ranges->rgb[reg & 0x0f];
}

/* ---------- combiner inputs */

static const char *register_expression(unsigned long reg, int stage, BOOL unique_c0, BOOL unique_c1)
{
	static char buffer[4][32];
	static int next = 0;
	char *result = buffer[next++ & 3];

	switch (reg)
	{
	case _register_c0:
		if (stage < 0)
			return "ps_final_c0";
		snprintf(result, sizeof(buffer[0]), "ps_c0[%d]", unique_c0 ? stage : 0);
		return result;
	case _register_c1:
		if (stage < 0)
			return "ps_final_c1";
		snprintf(result, sizeof(buffer[0]), "ps_c1[%d]", unique_c1 ? stage : 0);
		return result;
	case _register_fog: return "fog";
	case _register_v0: return "v0";
	case _register_v1: return "v1";
	case _register_t0: return "t0";
	case _register_t1: return "t1";
	case _register_t2: return "t2";
	case _register_t3: return "t3";
	case _register_r0: return "r0";
	case _register_r1: return "r1";
	case _register_v1r0_sum: return stage < 0 ? "v1r0_sum" : "vec4(0.0)";
	case _register_ef_product: return stage < 0 ? "ef_product" : "vec4(0.0)";
	default: return "vec4(0.0)";
	}
}

/* one combiner input byte as a vec3 (rgb) or float (alpha) expression;
the range of its values */
static struct range combiner_input(struct xgpu_text *text, unsigned long input, BOOL alpha_portion, int stage,
	BOOL unique_c0, BOOL unique_c1, const struct ranges *ranges)
{
	unsigned long reg = input & 0x0f;
	BOOL alpha_channel = (input & 0x10) != 0;
	unsigned long mapping = input & 0xe0;
	const char *source = register_expression(reg, stage, unique_c0, unique_c1);
	struct range range = input_range(ranges, reg, alpha_portion, alpha_channel);
	/* max(value, 0.0), or the value where it cannot be negative */
	struct range positive = range_make(range.low > 0.0f ? range.low : 0.0f, range.high > 0.0f ? range.high : 0.0f);
	char value[64], unsigned_value[96];

	if (alpha_portion)
		snprintf(value, sizeof(value), "%s.%s", source, alpha_channel ? "a" : "b");
	else if (alpha_channel)
		snprintf(value, sizeof(value), "vec3(%s.a)", source);
	else
		snprintf(value, sizeof(value), "%s.rgb", source);

	if (range.low >= 0.0f && ranges_used())
		snprintf(unsigned_value, sizeof(unsigned_value), "%s", value);
	else
		snprintf(unsigned_value, sizeof(unsigned_value), "max(%s, 0.0)", value);
	switch (mapping)
	{
	case 0x00:
		xgpu_text_append(text, "(%s)", unsigned_value);
		return positive;
	case 0x20:
		xgpu_text_append(text, "(1.0 - ");
		append_clamped(text, value, range, 0.0f, 1.0f);
		xgpu_text_append(text, ")");
		range = range_clamp(range, 0.0f, 1.0f);
		return range_make(1.0f - range.high, 1.0f - range.low);
	case 0x40:
		xgpu_text_append(text, "(2.0 * %s - 1.0)", unsigned_value);
		return range_scale(positive, 2.0f, -1.0f);
	case 0x60:
		xgpu_text_append(text, "(1.0 - 2.0 * %s)", unsigned_value);
		return range_make(1.0f - 2.0f * positive.high, 1.0f - 2.0f * positive.low);
	case 0x80:
		xgpu_text_append(text, "(%s - 0.5)", unsigned_value);
		return range_scale(positive, 1.0f, -0.5f);
	case 0xa0:
		xgpu_text_append(text, "(0.5 - %s)", unsigned_value);
		return range_make(0.5f - positive.high, 0.5f - positive.low);
	case 0xc0:
		xgpu_text_append(text, "(%s)", value);
		return range;
	default:
		xgpu_text_append(text, "(-%s)", value);
		return range_make(-range.high, -range.low);
	}
}

/* final combiner inputs only have the unsigned identity and invert mappings */
static void final_input(struct xgpu_text *text, unsigned long input, BOOL alpha_portion, const struct ranges *ranges)
{
	unsigned long reg = input & 0x0f;
	BOOL alpha_channel = (input & 0x10) != 0;
	const char *source = register_expression(reg, -1, FALSE, FALSE);
	struct range range = input_range(ranges, reg, alpha_portion, alpha_channel);
	char value[64];

	if (alpha_portion)
		snprintf(value, sizeof(value), "%s.%s", source, alpha_channel ? "a" : "b");
	else if (alpha_channel)
		snprintf(value, sizeof(value), "vec3(%s.a)", source);
	else
		snprintf(value, sizeof(value), "%s.rgb", source);
	if (input & 0x20)
	{
		xgpu_text_append(text, "(1.0 - ");
		append_clamped(text, value, range, 0.0f, 1.0f);
		xgpu_text_append(text, ")");
	}
	else
		append_clamped(text, value, range, 0.0f, 1.0f);
}

static const char *destination_name(unsigned long reg)
{
	switch (reg)
	{
	case _register_v0: return "v0";
	case _register_v1: return "v1";
	case _register_t0: return "t0";
	case _register_t1: return "t1";
	case _register_t2: return "t2";
	case _register_t3: return "t3";
	case _register_r0: return "r0";
	case _register_r1: return "r1";
	default: return NULL;
	}
}

static const char *output_mapping(unsigned long flags)
{
	switch (flags & 0x38)
	{
	case 0x08: return "(%s - 0.5)";
	case 0x10: return "(%s * 2.0)";
	case 0x18: return "((%s - 0.5) * 2.0)";
	case 0x20: return "(%s * 4.0)";
	case 0x30: return "(%s * 0.5)";
	default: return "(%s)";
	}
}

/* the range output_mapping makes of a value's */
static struct range output_mapping_range(unsigned long flags, struct range range)
{
	switch (flags & 0x38)
	{
	case 0x08: return range_scale(range, 1.0f, -0.5f);
	case 0x10: return range_scale(range, 2.0f, 0.0f);
	case 0x18: return range_scale(range, 2.0f, -1.0f);
	case 0x20: return range_scale(range, 4.0f, 0.0f);
	case 0x30: return range_scale(range, 0.5f, 0.0f);
	default: return range;
	}
}

/* ---------- one general combiner stage */

static void combiner_stage(struct xgpu_text *text, const DWORD *state, int stage, struct ranges *ranges)
{
	DWORD combiner_count = state[D3DRS_PSCOMBINERCOUNT];
	BOOL unique_c0 = (combiner_count & 0x1000) != 0;
	BOOL unique_c1 = (combiner_count & 0x10000) != 0;
	BOOL mux_msb = (combiner_count & 0x100) != 0;
	/* each portion's AB, CD and SUM, as written back */
	struct range written[2][3];
	int portion;

	xgpu_text_append(text, "\t/* combiner stage %d */\n\t{\n", stage);
	for (portion = 0; portion < 2; portion++)
	{
		BOOL alpha = portion == 1;
		DWORD inputs = alpha ? state[D3DRS_PSALPHAINPUTS0 + stage] : state[D3DRS_PSRGBINPUTS0 + stage];
		DWORD outputs = alpha ? state[D3DRS_PSALPHAOUTPUTS0 + stage] : state[D3DRS_PSRGBOUTPUTS0 + stage];
		unsigned long flags = outputs >> 12;
		const char *type = alpha ? "float" : "vec3";
		const char *prefix = alpha ? "a" : "c";
		const char *mapping = output_mapping(flags);
		char mapped[64], name[16];
		struct range a, b, c, d, ab, cd, sum;

		xgpu_text_append(text, "\t\t%s %sA = ", type, prefix);
		a = combiner_input(text, (inputs >> 24) & 0xff, alpha, stage, unique_c0, unique_c1, ranges);
		xgpu_text_append(text, ";\n\t\t%s %sB = ", type, prefix);
		b = combiner_input(text, (inputs >> 16) & 0xff, alpha, stage, unique_c0, unique_c1, ranges);
		xgpu_text_append(text, ";\n\t\t%s %sC = ", type, prefix);
		c = combiner_input(text, (inputs >> 8) & 0xff, alpha, stage, unique_c0, unique_c1, ranges);
		xgpu_text_append(text, ";\n\t\t%s %sD = ", type, prefix);
		d = combiner_input(text, inputs & 0xff, alpha, stage, unique_c0, unique_c1, ranges);
		xgpu_text_append(text, ";\n");

		ab = range_multiply(a, b);
		cd = range_multiply(c, d);
		if (!alpha && (flags & 0x02))
		{
			xgpu_text_append(text, "\t\tvec3 cAB = vec3(dot(cA, cB));\n");
			ab = range_scale(ab, 3.0f, 0.0f);
		}
		else
			xgpu_text_append(text, "\t\t%s %sAB = %sA * %sB;\n", type, prefix, prefix, prefix);
		if (!alpha && (flags & 0x01))
		{
			xgpu_text_append(text, "\t\tvec3 cCD = vec3(dot(cC, cD));\n");
			cd = range_scale(cd, 3.0f, 0.0f);
		}
		else
			xgpu_text_append(text, "\t\t%s %sCD = %sC * %sD;\n", type, prefix, prefix, prefix);
		sum = (flags & 0x04) ? range_union(ab, cd) : range_add(ab, cd);
		if (flags & 0x04)
		{
			if (mux_msb)
				xgpu_text_append(text, "\t\t%s %sSUM = r0.a >= 0.5 ? %sCD : %sAB;\n", type, prefix, prefix, prefix);
			else
				xgpu_text_append(text, "\t\t%s %sSUM = (int(r0.a * 255.0 + 0.5) & 1) != 0 ? %sCD : %sAB;\n",
					type, prefix, prefix, prefix);
		}
		else
		{
			xgpu_text_append(text, "\t\t%s %sSUM = %sAB + %sCD;\n", type, prefix, prefix, prefix);
		}
		{
			static const char *const parts[3] = { "AB", "CD", "SUM" };
			struct range part_ranges[3];
			int part;

			part_ranges[0] = ab;
			part_ranges[1] = cd;
			part_ranges[2] = sum;
			for (part = 0; part < 3; part++)
			{
				struct range mapped_range = output_mapping_range(flags, part_ranges[part]);

				snprintf(name, sizeof(name), "%s%s", prefix, parts[part]);
				snprintf(mapped, sizeof(mapped), mapping, name);
				xgpu_text_append(text, "\t\t%s = ", name);
				append_clamped(text, mapped, mapped_range, -1.0f, 1.0f);
				xgpu_text_append(text, ";\n");
				written[portion][part] = range_clamp(mapped_range, -1.0f, 1.0f);
			}
		}
	}

	/* write back only after both portions have read their inputs */
	for (portion = 0; portion < 2; portion++)
	{
		BOOL alpha = portion == 1;
		DWORD outputs = alpha ? state[D3DRS_PSALPHAOUTPUTS0 + stage] : state[D3DRS_PSRGBOUTPUTS0 + stage];
		unsigned long flags = outputs >> 12;
		const char *prefix = alpha ? "a" : "c";
		const char *component = alpha ? "a" : "rgb";
		const char *ab = destination_name((outputs >> 4) & 0x0f);
		const char *cd = destination_name(outputs & 0x0f);
		const char *sum = destination_name((outputs >> 8) & 0x0f);

		struct range *written_ranges = alpha ? ranges->alpha : ranges->rgb;

		if (ab)
		{
			xgpu_text_append(text, "\t\t%s.%s = %sAB;\n", ab, component, prefix);
			written_ranges[(outputs >> 4) & 0x0f] = written[portion][0];
			if (!alpha && (flags & 0x80))
			{
				xgpu_text_append(text, "\t\t%s.a = cAB.b;\n", ab);
				ranges->alpha[(outputs >> 4) & 0x0f] = written[portion][0];
			}
		}
		if (cd)
		{
			xgpu_text_append(text, "\t\t%s.%s = %sCD;\n", cd, component, prefix);
			written_ranges[outputs & 0x0f] = written[portion][1];
			if (!alpha && (flags & 0x40))
			{
				xgpu_text_append(text, "\t\t%s.a = cCD.b;\n", cd);
				ranges->alpha[outputs & 0x0f] = written[portion][1];
			}
		}
		if (sum)
		{
			xgpu_text_append(text, "\t\t%s.%s = %sSUM;\n", sum, component, prefix);
			written_ranges[(outputs >> 8) & 0x0f] = written[portion][2];
		}
	}
	xgpu_text_append(text, "\t}\n");
}

/* ---------- texture stages */

static const char *sampler_declaration(unsigned char type)
{
	switch (type)
	{
	case _xgpu_sampler_3d: return "sampler3D";
	case _xgpu_sampler_cube: return "samplerCube";
	default: return "sampler2D";
	}
}

static unsigned long stage_mode(const struct nv2a_pixel_shader_key *key, int stage)
{
	return (key->texture_modes >> (5 * stage)) & 0x1f;
}

static int stage_input(const DWORD *state, int stage)
{
	switch (stage)
	{
	case 2: return (state[D3DRS_PSINPUTTEXTURE] >> 16) & 1;
	case 3: return (state[D3DRS_PSINPUTTEXTURE] >> 20) & 3;
	default: return 0;
	}
}

/* the input texel of a dot product stage, mapped per PSDotMapping */
static void dot_input(struct xgpu_text *text, const DWORD *state, int stage)
{
	unsigned long mapping = (state[D3DRS_PSDOTMAPPING] >> ((stage - 1) * 4)) & 7;
	int input = stage_input(state, stage);

	switch (mapping)
	{
	case 0: xgpu_text_append(text, "t%d.rgb", input); break;
	case 1: xgpu_text_append(text, "((t%d.rgb * 255.0 - 128.0) / 127.0)", input); break;
	case 3: xgpu_text_append(text, "signed_bytes(t%d.rgb)", input); break;
	default: xgpu_text_append(text, "(t%d.rgb * 2.0 - 1.0)", input); break;
	}
}

static void sample(struct xgpu_text *text, const struct nv2a_pixel_shader_key *key, int stage, const char *coordinates)
{
	switch (key->sampler_type[stage])
	{
	case _xgpu_sampler_3d:
		xgpu_text_append(text, "texture(tex%d, (%s).xyz)", stage, coordinates);
		break;
	case _xgpu_sampler_cube:
		xgpu_text_append(text, "texture(tex%d, (%s).xyz)", stage, coordinates);
		break;
	default:
		xgpu_text_append(text, "texture(tex%d, (%s).xy * texture_scale[%d].xy)", stage, coordinates, stage);
		break;
	}
}

/* the range of what a texture stage leaves in its register, rgb and alpha:
a texture's texels are in [0, 1] (unsigned formats, a border colour of 8-bit
channels), a channel marked signed in [-128/127, 1], and a stage that
samples nothing leaves 0 */
static void texture_stage_range(const struct nv2a_pixel_shader_key *key, int stage, struct ranges *ranges)
{
	unsigned long mode = stage_mode(key, stage);
	struct range range = range_make(0.0f, 1.0f);
	int reg = _register_t0 + stage;

	if (key->sampler_type[stage] == _xgpu_sampler_none &&
		mode != _mode_passthru && mode != _mode_clipplane && mode != _mode_dot_product && mode != _mode_dot_zw)
	{
		mode = _mode_none;
	}
	switch (mode)
	{
	case _mode_none:
	case _mode_clipplane:
	case _mode_dot_product:
	case _mode_dot_zw:
		range = range_make(0.0f, 0.0f);
		break;
	case _mode_project2d:
	case _mode_project3d:
	case _mode_cubemap:
	case _mode_passthru:
	case _mode_bumpenvmap:
	case _mode_bumpenvmap_luminance:
	case _mode_dot_st:
	case _mode_dot_reflect_diffuse:
	case _mode_dot_reflect_specular:
	case _mode_dot_reflect_specular_constant:
	case _mode_dot_str_3d:
	case _mode_dot_str_cube:
	case _mode_dependent_ar:
	case _mode_dependent_gb:
		break;
	default:
		range = range_make(0.0f, 0.0f);
		break;
	}
	ranges->rgb[reg] = range;
	ranges->alpha[reg] = range;
	if (key->color_sign[stage] && mode != _mode_none)
	{
		struct range signed_range = range_make(-128.0f / 127.0f, 1.0f);

		if (key->color_sign[stage] & 0x0e)
			ranges->rgb[reg] = range_union(range, signed_range);
		if (key->color_sign[stage] & 0x01)
			ranges->alpha[reg] = range_union(range, signed_range);
	}
}

static void texture_stage(struct xgpu_text *text, const struct nv2a_pixel_shader_key *key, int stage)
{
	const DWORD *state = key->combiner_state;
	unsigned long mode = stage_mode(key, stage);
	char coordinates[96];

	xgpu_text_append(text, "\t/* texture stage %d, mode %lu */\n", stage, mode);
	if (key->sampler_type[stage] == _xgpu_sampler_none &&
		mode != _mode_passthru && mode != _mode_clipplane && mode != _mode_dot_product && mode != _mode_dot_zw)
	{
		mode = _mode_none;
	}
	switch (mode)
	{
	case _mode_project2d:
	case _mode_project3d:
		snprintf(coordinates, sizeof(coordinates), "vec4(xT%d.xyz / (xT%d.w != 0.0 ? xT%d.w : 1.0), 1.0)", stage, stage, stage);
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, key, stage, coordinates);
		xgpu_text_append(text, ";\n");
		/* (a Custom Edition meter drawn from a sheet other art shares: its fill order in alpha, its shape in red, as
		nv2a_psh.c reads them) */
		if (stage == 0 && key->ce_meter_channels)
			xgpu_text_append(text, "\tt0 = t0.aaar;\n");
		/* (a high-res meter's segments: their thresholds, in red, read from the texel itself, unfiltered, so that no
		threshold is averaged with its neighbour's; 0 marks a continuous meter: nv2a_psh.c) */
		if (stage == 0 && key->point_threshold && key->sampler_type[stage] == _xgpu_sampler_2d)
		{
			xgpu_text_append(text,
				"\t{\n\t\tvec2 uv = (%s).xy * texture_scale[0].xy;\n"
				"\t\tivec2 size = textureSize(tex0, 0);\n"
				"\t\tivec2 pixel = clamp(ivec2(floor(uv * vec2(size))), ivec2(0), size - 1);\n"
				"\t\tfloat threshold = texelFetch(tex0, pixel, 0).r;\n"
				"\t\tif (threshold > 0.0) t0.rb = vec2(threshold);\n\t}\n", coordinates);
		}
		break;
	case _mode_cubemap:
		snprintf(coordinates, sizeof(coordinates), "xT%d", stage);
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, key, stage, coordinates);
		xgpu_text_append(text, ";\n");
		break;
	case _mode_passthru:
		xgpu_text_append(text, "\tt%d = clamp(xT%d, 0.0, 1.0);\n", stage, stage);
		break;
	case _mode_clipplane:
	{
		unsigned long compare = (state[D3DRS_PSCOMPAREMODE] >> (4 * stage)) & 0xf;
		static const char components[] = "xyzw";
		int component;

		for (component = 0; component < 4; component++)
		{
			xgpu_text_append(text, "\tif (xT%d.%c %s 0.0) discard;\n", stage, components[component],
				(compare & (1 << component)) ? ">=" : "<");
		}
		xgpu_text_append(text, "\tt%d = vec4(0.0);\n", stage);
		break;
	}
	case _mode_bumpenvmap:
	case _mode_bumpenvmap_luminance:
	{
		int input = stage - 1;

		xgpu_text_append(text, "\t{\n\t\tvec2 d = signed_bytes(t%d.rgb).rg;\n", input);
		xgpu_text_append(text, "\t\tvec2 coordinates = xT%d.xy + vec2(bump_matrix[%d].x * d.x + bump_matrix[%d].z * d.y,"
			" bump_matrix[%d].y * d.x + bump_matrix[%d].w * d.y);\n", stage, stage, stage, stage, stage);
		xgpu_text_append(text, "\t\tt%d = ", stage);
		sample(text, key, stage, "vec4(coordinates, 0.0, 1.0)");
		xgpu_text_append(text, ";\n");
		if (mode == _mode_bumpenvmap_luminance)
		{
			xgpu_text_append(text, "\t\tt%d.rgb *= clamp(bump_luminance[%d].x * t%d.b + bump_luminance[%d].y, 0.0, 1.0);\n",
				stage, stage, input, stage);
		}
		xgpu_text_append(text, "\t}\n");
		break;
	}
	case _mode_dot_product:
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\tt%d = vec4(0.0);\n", stage);
		break;
	case _mode_dot_st:
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\tt%d = ", stage);
		snprintf(coordinates, sizeof(coordinates), "vec4(dot%d, dot%d, 0.0, 1.0)", stage - 1, stage);
		sample(text, key, stage, coordinates);
		xgpu_text_append(text, ";\n");
		break;
	case _mode_dot_zw:
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\tt%d = vec4(0.0);\n", stage);
		break;
	case _mode_dot_reflect_diffuse:
		/* the normal takes its third component from stage 3's dot product */
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\tdot3 = dot(xT3.xyz, ");
		dot_input(text, state, 3);
		xgpu_text_append(text, ");\n\tt%d = ", stage);
		sample(text, key, stage, "vec4(dot1, dot2, dot3, 1.0)");
		xgpu_text_append(text, ";\n");
		break;
	case _mode_dot_reflect_specular:
	case _mode_dot_reflect_specular_constant:
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\t{\n\t\tvec3 n = vec3(dot1, dot2, dot3);\n");
		if (mode == _mode_dot_reflect_specular)
			xgpu_text_append(text, "\t\tvec3 e = vec3(xT1.w, xT2.w, xT3.w);\n");
		else
			xgpu_text_append(text, "\t\tvec3 e = ps_c0[0].xyz;\n");
		xgpu_text_append(text, "\t\tvec3 r = 2.0 * n * dot(n, e) / max(dot(n, n), 1.0e-20) - e;\n\t\tt%d = ", stage);
		sample(text, key, stage, "vec4(r, 1.0)");
		xgpu_text_append(text, ";\n\t}\n");
		break;
	case _mode_dot_str_3d:
	case _mode_dot_str_cube:
		xgpu_text_append(text, "\tdot%d = dot(xT%d.xyz, ", stage, stage);
		dot_input(text, state, stage);
		xgpu_text_append(text, ");\n\tt%d = ", stage);
		sample(text, key, stage, "vec4(dot1, dot2, dot3, 1.0)");
		xgpu_text_append(text, ";\n");
		break;
	case _mode_dependent_ar:
		snprintf(coordinates, sizeof(coordinates), "vec4(t%d.a, t%d.r, 0.0, 1.0)", stage_input(state, stage), stage_input(state, stage));
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, key, stage, coordinates);
		xgpu_text_append(text, ";\n");
		break;
	case _mode_dependent_gb:
		snprintf(coordinates, sizeof(coordinates), "vec4(t%d.g, t%d.b, 0.0, 1.0)", stage_input(state, stage), stage_input(state, stage));
		xgpu_text_append(text, "\tt%d = ", stage);
		sample(text, key, stage, coordinates);
		xgpu_text_append(text, ";\n");
		break;
	default:
		xgpu_text_append(text, "\tt%d = vec4(0.0);\n", stage);
		break;
	}

	/* channels the application marked signed (D3DTSS_COLORSIGN) */
	if (key->color_sign[stage] && mode != _mode_none)
	{
		static const char channels[] = "argb";
		int bit;

		for (bit = 0; bit < 4; bit++)
		{
			if (key->color_sign[stage] & (1 << bit))
				xgpu_text_append(text, "\tt%d.%c = signed_byte(t%d.%c);\n", stage, channels[bit], stage, channels[bit]);
		}
	}
	if (key->alpha_kill[stage] && mode != _mode_none)
		xgpu_text_append(text, "\tif (t%d.a == 0.0) discard;\n", stage);
}

/* ---------- the whole shader */

static const char *comparison_operator(unsigned long function)
{
	switch (function)
	{
	case D3DCMP_NEVER: return NULL;
	case D3DCMP_LESS: return "<";
	case D3DCMP_EQUAL: return "==";
	case D3DCMP_LESSEQUAL: return "<=";
	case D3DCMP_GREATER: return ">";
	case D3DCMP_NOTEQUAL: return "!=";
	case D3DCMP_GREATEREQUAL: return ">=";
	default: return "";
	}
}

char *nv2a_dk_pixel_shader_to_glsl(const struct nv2a_pixel_shader_key *key)
{
	const DWORD *state = key->combiner_state;
	struct xgpu_text text = { 0 };
	unsigned long combiner_count = state[D3DRS_PSCOMBINERCOUNT] & 0xff;
	DWORD final_abcd = state[D3DRS_PSFINALCOMBINERINPUTSABCD];
	DWORD final_efg = state[D3DRS_PSFINALCOMBINERINPUTSEFG];
	struct ranges ranges;
	int stage;

	if (combiner_count > 8)
		combiner_count = 8;

	xgpu_text_append(&text,
		"#version 460\n"
		"layout(std140, binding = %d) uniform pixel_parameters\n"
		"{\n"
		"\tvec4 ps_c0[8];\n"
		"\tvec4 ps_c1[8];\n"
		"\tvec4 ps_final_c0;\n"
		"\tvec4 ps_final_c1;\n"
		"\tvec4 fog_color;\n"
		"\tvec4 fog_parameters;\n"
		"\tvec4 alpha_reference;\n"
		"\tvec4 bump_matrix[4];\n"
		"\tvec4 bump_luminance[4];\n"
		"\tvec4 texture_scale[4];\n"
		"};\n",
		DK_BINDING_PIXEL_PARAMETERS);
	xgpu_text_append(&text,
		"layout(location = %d) in vec4 xD0;\n"
		"layout(location = %d) in vec4 xD1;\n"
		"layout(location = %d) in vec4 xB0;\n"
		"layout(location = %d) in vec4 xB1;\n"
		"layout(location = %d) in vec4 xT0;\n"
		"layout(location = %d) in vec4 xT1;\n"
		"layout(location = %d) in vec4 xT2;\n"
		"layout(location = %d) in vec4 xT3;\n"
		"layout(location = %d) in float xFog;\n"
		"layout(location = %d) out vec4 fragment_color;\n",
		DK_LOCATION_D0, DK_LOCATION_D1, DK_LOCATION_B0, DK_LOCATION_B1,
		DK_LOCATION_T0, DK_LOCATION_T1, DK_LOCATION_T2, DK_LOCATION_T3, DK_LOCATION_FOG,
		DK_LOCATION_FRAGMENT_COLOR);
	for (stage = 0; stage < 4; stage++)
		xgpu_text_append(&text, "layout(binding = %d) uniform %s tex%d;\n", DK_BINDING_TEXTURE0 + stage,
			sampler_declaration(key->sampler_type[stage]), stage);
	xgpu_text_append(&text,
		"float signed_byte(float x)\n"
		"{\n"
		"	float b = floor(x * 255.0 + 0.5);\n"
		"	return (b >= 128.0 ? b - 256.0 : b) / 127.0;\n"
		"}\n"
		"vec3 signed_bytes(vec3 x)\n"
		"{\n"
		"	return vec3(signed_byte(x.r), signed_byte(x.g), signed_byte(x.b));\n"
		"}\n"
		"void main()\n"
		"{\n"
		"\tvec4 v0 = xD0;\n"
		"\tvec4 v1 = xD1;\n"
		"\tvec4 t0 = vec4(0.0), t1 = vec4(0.0), t2 = vec4(0.0), t3 = vec4(0.0);\n"
		"\tfloat dot0 = 0.0, dot1 = 0.0, dot2 = 0.0, dot3 = 0.0;\n");

	/* a stage's coordinates' clip (NV2A_PIXEL_KEY_CLIP, halo_texture_clip in
	d3d8_dk.c): a pixel whose u or v (and w, for a 3D texture), as the stage
	samples them, is outside [alpha_reference.y, .z] does nothing - a shadow
	cast on the level outside its map, a dynamic light's pass outside its
	reach - and is let go before the stages read their textures */
	if (key->texture_modes & NV2A_PIXEL_KEY_CLIP)
	{
		int clip_stage = (int)((key->texture_modes & NV2A_PIXEL_KEY_CLIP_STAGE_MASK) >> NV2A_PIXEL_KEY_CLIP_STAGE_SHIFT);

		/* (the stage's 3D coordinates: a dynamic light's distance
		attenuation, which is black past its box) */
		if (key->texture_modes & NV2A_PIXEL_KEY_CLIP_3D)
		{
			xgpu_text_append(&text,
				"\t{\n"
				"\t\tvec3 clip_uvw = xT%d.xyz / (xT%d.w != 0.0 ? xT%d.w : 1.0) * texture_scale[%d].xyz;\n"
				"\t\tif (any(lessThan(clip_uvw, vec3(alpha_reference.y))) || any(greaterThan(clip_uvw, vec3(alpha_reference.z))))\n"
				"\t\t\tdiscard;\n", clip_stage, clip_stage, clip_stage, clip_stage);
			/* (past the texture's reach: dk_texture_reach) */
			if (key->texture_modes & NV2A_PIXEL_KEY_CLIP_SPHERE)
				xgpu_text_append(&text,
					"\t\tif (length(clip_uvw - vec3(0.5)) > texture_scale[%d].w)\n"
					"\t\t\tdiscard;\n", clip_stage);
			xgpu_text_append(&text, "\t}\n");
		}
		else
			xgpu_text_append(&text,
				"\t{\n"
				"\t\tvec2 clip_uv = xT%d.xy / (xT%d.w != 0.0 ? xT%d.w : 1.0) * texture_scale[%d].xy;\n"
				"\t\tif (any(lessThan(clip_uv, vec2(alpha_reference.y))) || any(greaterThan(clip_uv, vec2(alpha_reference.z))))\n"
				"\t\t\tdiscard;\n"
				"\t}\n", clip_stage, clip_stage, clip_stage, clip_stage);
	}
	for (stage = 0; stage < 4; stage++)
	{
		texture_stage(&text, key, stage);
		/* (NV2A_PIXEL_KEY_CLIP_BLACK: the clipped stage's texel black, the
		draw adds nothing there - an object's shadow, outside its shape in
		its map - and the pixel goes before the stages after it are read) */
		if ((key->texture_modes & NV2A_PIXEL_KEY_CLIP) && (key->texture_modes & NV2A_PIXEL_KEY_CLIP_BLACK) &&
			stage == (int)((key->texture_modes & NV2A_PIXEL_KEY_CLIP_STAGE_MASK) >> NV2A_PIXEL_KEY_CLIP_STAGE_SHIFT))
			xgpu_text_append(&text, "\tif (all(lessThan(t%d.rgb, vec3(0.49 / 255.0))))\n\t\tdiscard;\n", stage);
	}

	/* the registers' ranges as the combiners start: the constants and the fog
	colour are 8-bit colours, the vertex colours are clamped by the vertex
	shader (nv2a_vsh_dk.c), r0 holds t0's alpha and r1 nothing, and the final
	combiner's two registers read as zero in the general stages */
	{
		unsigned long reg;

		for (reg = 0; reg < 16; reg++)
			ranges.rgb[reg] = ranges.alpha[reg] = range_make(0.0f, 0.0f);
		ranges.rgb[_register_c0] = ranges.alpha[_register_c0] = range_make(0.0f, 1.0f);
		ranges.rgb[_register_c1] = ranges.alpha[_register_c1] = range_make(0.0f, 1.0f);
		ranges.rgb[_register_fog] = ranges.alpha[_register_fog] = range_make(0.0f, 1.0f);
		ranges.rgb[_register_v0] = ranges.alpha[_register_v0] = range_make(0.0f, 1.0f);
		ranges.rgb[_register_v1] = ranges.alpha[_register_v1] = range_make(0.0f, 1.0f);
		for (stage = 0; stage < 4; stage++)
			texture_stage_range(key, stage, &ranges);
		ranges.alpha[_register_r0] = ranges.alpha[_register_t0];
	}

	/* the fog register: rgb is the fog color, alpha the fog factor */
	if (key->fog_enable)
	{
		switch (key->fog_table_mode)
		{
		case D3DFOG_EXP:
			xgpu_text_append(&text, "\tfloat fog_factor = exp(-fog_parameters.z * xFog);\n");
			break;
		case D3DFOG_EXP2:
			xgpu_text_append(&text, "\tfloat fog_factor = exp(-(fog_parameters.z * xFog) * (fog_parameters.z * xFog));\n");
			break;
		case D3DFOG_LINEAR:
			xgpu_text_append(&text, "\tfloat fog_factor = (fog_parameters.y - xFog) / max(fog_parameters.y - fog_parameters.x, 1.0e-6);\n");
			break;
		default:
			xgpu_text_append(&text, "\tfloat fog_factor = xFog;\n");
			break;
		}
	}
	else
	{
		xgpu_text_append(&text, "\tfloat fog_factor = 1.0;\n");
	}
	xgpu_text_append(&text,
		"\tvec4 fog = vec4(fog_color.rgb, clamp(fog_factor, 0.0, 1.0));\n"
		"\tvec4 r0 = vec4(0.0, 0.0, 0.0, t0.a);\n"
		"\tvec4 r1 = vec4(0.0);\n");

	for (stage = 0; stage < (int)combiner_count; stage++)
		combiner_stage(&text, state, stage, &ranges);

	if (final_abcd == 0 && final_efg == 0)
	{
		xgpu_text_append(&text, "\tvec4 result = r0;\n");
	}
	else
	{
		unsigned long settings = final_efg & 0xff;

		xgpu_text_append(&text, "\tvec4 ef_product = vec4(");
		final_input(&text, (final_efg >> 24) & 0xff, FALSE, &ranges);
		xgpu_text_append(&text, " * ");
		final_input(&text, (final_efg >> 16) & 0xff, FALSE, &ranges);
		xgpu_text_append(&text, ", 0.0);\n");
		xgpu_text_append(&text, "\tvec4 v1r0_sum = vec4(%s", (settings & 0x40) ? "(1.0 - " : "");
		append_clamped(&text, "v1.rgb", ranges.rgb[_register_v1], 0.0f, 1.0f);
		xgpu_text_append(&text, "%s + %s", (settings & 0x40) ? ")" : "", (settings & 0x20) ? "(1.0 - " : "");
		append_clamped(&text, "r0.rgb", ranges.rgb[_register_r0], 0.0f, 1.0f);
		xgpu_text_append(&text, "%s, 0.0);\n", (settings & 0x20) ? ")" : "");
		if (settings & 0x80)
			xgpu_text_append(&text, "\tv1r0_sum = clamp(v1r0_sum, 0.0, 1.0);\n");
		/* (the final combiner's own registers: their rgb in [0, 1] and [0, 2]
		as made above, their alpha zero) */
		ranges.rgb[_register_ef_product] = range_make(0.0f, 1.0f);
		ranges.rgb[_register_v1r0_sum] = (settings & 0x80) ? range_make(0.0f, 1.0f) : range_make(0.0f, 2.0f);
		ranges.alpha[_register_ef_product] = ranges.alpha[_register_v1r0_sum] = range_make(0.0f, 0.0f);
		xgpu_text_append(&text, "\tvec3 fA = ");
		final_input(&text, (final_abcd >> 24) & 0xff, FALSE, &ranges);
		xgpu_text_append(&text, ";\n\tvec3 fB = ");
		final_input(&text, (final_abcd >> 16) & 0xff, FALSE, &ranges);
		xgpu_text_append(&text, ";\n\tvec3 fC = ");
		final_input(&text, (final_abcd >> 8) & 0xff, FALSE, &ranges);
		xgpu_text_append(&text, ";\n\tvec3 fD = ");
		final_input(&text, final_abcd & 0xff, FALSE, &ranges);
		xgpu_text_append(&text, ";\n\tfloat fG = ");
		final_input(&text, (final_efg >> 8) & 0xff, TRUE, &ranges);
		xgpu_text_append(&text, ";\n\tvec4 result = vec4(fA * fB + (1.0 - fA) * fC + fD, fG);\n");
	}

	/* the zoom's blend (NV2A_PIXEL_KEY_WARP_BLEND, rasterizer_xbox_screen_effect.c):
	t0 the mask, t1 the small copy blurred by the game's passes, and in
	texture coordinate 3 the pixel's way from the warp's centre, scaled so
	that its length times the root of the mask's alpha (how much each pass
	mixes) is the blur's spread in the small copy's texels. The screen stays
	sharp up to a spread of ps_c0[0].r times 4 texels, where the small copy
	would only lose detail the game keeps and where the player aims, and the
	blur is drawn wholly from ps_c0[0].g times 4, eased between. Drawn with
	the screen's colour times the alpha added (D3DBLEND_ONE,
	D3DBLEND_SRCALPHA), and all of it darkened by one less the mask's blue,
	as the game's last pass is (its final alpha, G, reads blue). */
	if (key->texture_modes & NV2A_PIXEL_KEY_WARP_BLEND)
		xgpu_text_append(&text,
			"\t{\n"
			"\t\tfloat warp_spread = length(xT3.xy) * sqrt(t0.a);\n"
			"\t\tfloat warp_blur = smoothstep(ps_c0[0].r * 4.0, max(ps_c0[0].g * 4.0, ps_c0[0].r * 4.0 + 0.01), warp_spread);\n"
			"\t\tfloat warp_dark = 1.0 - t0.b;\n"
			"\t\tresult = vec4(t1.rgb * warp_blur * warp_dark, (1.0 - warp_blur) * warp_dark);\n"
			"\t}\n");
	if (key->coverage_alpha)
		xgpu_text_append(&text, "\tresult.a = mix(1.0, result.a, t0.g);\n");
	if (key->alpha_test_function)
	{
		const char *comparison = comparison_operator(key->alpha_test_function);

		if (!comparison)
			xgpu_text_append(&text, "\tdiscard;\n");
		else if (*comparison)
			xgpu_text_append(&text, "\tif (!(floor(clamp(result.a, 0.0, 1.0) * 255.0 + 0.5) %s alpha_reference.x)) discard;\n",
				comparison);
	}
	/* a blended draw's pixel that would leave the target as it was
	(NV2A_PIXEL_KEY_DISCARD_*, blend_discard in d3d8_dk.c): let go. What the
	blend adds is under half a step of an 8-bit channel there, so the target
	rounds back to what it held */
	if (key->texture_modes & NV2A_PIXEL_KEY_DISCARD_MASK)
	{
		unsigned long discard = (key->texture_modes & NV2A_PIXEL_KEY_DISCARD_MASK) >> NV2A_PIXEL_KEY_DISCARD_SHIFT;
		int alpha = (key->texture_modes & NV2A_PIXEL_KEY_DISCARD_ALPHA_WRITTEN) != 0;

		xgpu_text_append(&text, "\t{\n\t\tvec4 blended = clamp(result, 0.0, 1.0);\n\t\tconst float nothing = 0.49 / 255.0;\n");
		if (discard == NV2A_PIXEL_DISCARD_ADD || discard == NV2A_PIXEL_DISCARD_ADD_ALPHA)
			xgpu_text_append(&text, "\t\tif (%s(all(lessThan(blended.rgb, vec3(nothing)))%s))\n\t\t\tdiscard;\n",
				discard == NV2A_PIXEL_DISCARD_ADD_ALPHA ? "blended.a < nothing || " : "",
				alpha ? " && blended.a < nothing" : "");
		else if (discard == NV2A_PIXEL_DISCARD_OVER)
			xgpu_text_append(&text, "\t\tif (blended.a < nothing)\n\t\t\tdiscard;\n");
		else if (discard == NV2A_PIXEL_DISCARD_MULTIPLY || discard == NV2A_PIXEL_DISCARD_MIN)
			xgpu_text_append(&text, "\t\tif (all(greaterThan(blended.rgb, vec3(1.0 - nothing)))%s)\n\t\t\tdiscard;\n",
				alpha ? " && blended.a > 1.0 - nothing" : "");
		else if (discard == NV2A_PIXEL_DISCARD_PREMULTIPLIED)
			xgpu_text_append(&text, "\t\tif (all(lessThan(blended, vec4(nothing))))\n\t\t\tdiscard;\n");
		else if (discard == NV2A_PIXEL_DISCARD_MAX)
			xgpu_text_append(&text, "\t\tif (all(lessThan(blended.rgb, vec3(nothing)))%s)\n\t\t\tdiscard;\n",
				alpha ? " && blended.a < nothing" : "");
		xgpu_text_append(&text, "\t}\n");
	}
	if (*config_string("debug.gpu_debug_expression"))
		xgpu_text_append(&text, "\tresult = vec4(vec3(%s), 1.0);\n", config_string("debug.gpu_debug_expression"));
	if (config_boolean("debug.gpu_debug_texture0"))
		xgpu_text_append(&text, "\tresult = vec4(t0.rgb, 1.0);\n");
	if (config_boolean("debug.gpu_debug_flat"))
		xgpu_text_append(&text, "\tresult = xD0.a > 0.0 ? vec4(xD0.rgb, 1.0) : vec4(1.0, 0.0, 1.0, 1.0);\n");
	xgpu_text_append(&text, "\tfragment_color = clamp(result, 0.0, 1.0);\n}\n");
	return text.buffer;
}
