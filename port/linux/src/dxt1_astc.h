/*
DXT1_ASTC.H

DXT1 as ASTC 4x4, for GPUs with ASTC and no BC (S3TC): the OpenGL ES
renderer's (xbox_textures.c, display.dxt1_astc) and Android's Vulkan
renderer's (port/android/guest/xbox_textures_vk.c). Static functions, for the
one file of each image that includes it.

A DXT1 block is two 565 colors and a 2-bit index for each texel, which an
ASTC 4x4 block holds nearly as it is: the colors as its endpoints, the
indices as its weights. So DXT1 needs no encoder here, only its bits moved,
and takes 8 bits a texel on the GPU instead of the 32 it is decoded to.
DXT3 and DXT5, whose alpha does not fit beside the colors, are still
decoded. A block is one of three kinds:
- four colors (c0 > c1): RGB endpoints of 8 bits and 2-bit weights, 0, 21,
  43 and 64 sixty-fourths of the way (DXT's thirds within a 64th);
- three colors, none transparent: RGB endpoints of 8 bits and weights of
  0, 32 and 64 (exact);
- three colors and transparent texels: two planes, the colors' weights and
  alpha's (0 or 255), with RGBA endpoints of 7 bits, as many as the block
  has room for; a transparent texel takes the darker color, where DXT's is
  black.
(The layout: the Khronos Data Format Specification's "ASTC" chapter.)
*/

#ifndef DXT1_ASTC_H
#define DXT1_ASTC_H

#include <string.h>

/* a 5- or 6-bit channel to 8 bits, as a DXT decoder widens it */
static unsigned long dxt1_astc_expand5(unsigned long value)
{
	return (value << 3) | (value >> 2);
}

static unsigned long dxt1_astc_expand6(unsigned long value)
{
	return (value << 2) | (value >> 4);
}

/* a field's bits written into the block from the bit given, lowest first;
or for weights (reversed), down from the block's top */
static void astc_bits(unsigned char block[16], unsigned long position, unsigned long count, unsigned long value,
	int reversed)
{
	unsigned long bit;

	for (bit = 0; bit < count; bit++)
	{
		unsigned long at = reversed ? 127 - (position + bit) : position + bit;

		if ((value >> bit) & 1)
			block[at >> 3] |= (unsigned char)(1 << (at & 7));
	}
}

/* five trits from their 8 bits, as the specification unpacks them */
static void astc_trits_unpacked(unsigned long bits, unsigned char trits[5])
{
	unsigned long c;

	if (((bits >> 2) & 7) == 7)
	{
		c = (((bits >> 5) & 7) << 2) | (bits & 3);
		trits[4] = 2;
		trits[3] = 2;
	}
	else
	{
		c = bits & 0x1f;
		if (((bits >> 5) & 3) == 3)
		{
			trits[4] = 2;
			trits[3] = (unsigned char)((bits >> 7) & 1);
		}
		else
		{
			trits[4] = (unsigned char)((bits >> 7) & 1);
			trits[3] = (unsigned char)((bits >> 5) & 3);
		}
	}
	if ((c & 3) == 3)
	{
		trits[2] = 2;
		trits[1] = (unsigned char)((c >> 4) & 1);
		trits[0] = (unsigned char)((((c >> 3) & 1) << 1) | ((c >> 2) & 1 & ~(c >> 3)));
	}
	else if (((c >> 2) & 3) == 3)
	{
		trits[2] = 2;
		trits[1] = 2;
		trits[0] = (unsigned char)(c & 3);
	}
	else
	{
		trits[2] = (unsigned char)((c >> 4) & 1);
		trits[1] = (unsigned char)((c >> 2) & 3);
		trits[0] = (unsigned char)((((c >> 1) & 1) << 1) | (c & 1 & ~(c >> 1)));
	}
}

/* the weights (0 to 2, count of them) as trits, packed five to 8 bits, the
last group with only the bits its trits need, from the block's top */
static void astc_trit_weights(unsigned char block[16], const unsigned char *weights, unsigned long count)
{
	static unsigned char packed[243];
	static int made;
	unsigned long group, position = 0;

	if (!made)
	{
		static int filled[243];
		unsigned long bits;

		/* (the smallest bits for each five, so that a last group's unused
		trits, zero, need no bits) */
		for (bits = 0; bits < 256; bits++)
		{
			unsigned char trits[5];
			unsigned long index;

			astc_trits_unpacked(bits, trits);
			index = trits[0] + 3 * trits[1] + 9 * trits[2] + 27 * trits[3] + 81 * trits[4];
			if (!filled[index])
			{
				filled[index] = 1;
				packed[index] = (unsigned char)bits;
			}
		}
		made = 1;
	}
	for (group = 0; group < count; group += 5)
	{
		static const unsigned char group_bits[6] = { 0, 2, 4, 5, 7, 8 };
		unsigned long index = 0, scale = 1, trit, in_group = count - group < 5 ? count - group : 5;

		for (trit = 0; trit < in_group; trit++, scale *= 3)
			index += weights[group + trit] * scale;
		astc_bits(block, position, group_bits[in_group], packed[index], 1);
		position += group_bits[in_group];
	}
}

/* one DXT1 block as ASTC 4x4 */
static void dxt1_astc_block(const unsigned char *source, unsigned char block[16])
{
	unsigned long c0 = source[0] | (source[1] << 8), c1 = source[2] | (source[3] << 8);
	unsigned long indices = source[4] | (source[5] << 8) | ((unsigned long)source[6] << 16) |
		((unsigned long)source[7] << 24);
	unsigned long endpoints[2][3], sums[2], channel, texel;
	unsigned char weights[32];
	int four = c0 > c1, transparent = 0, swap;
	unsigned long bits = 8, maximum = four ? 3 : 2;

	/* (each index's weight from the first color: DXT's order is c0, c1,
	then the colors between) */
	static const unsigned char four_weights[4] = { 0, 3, 1, 2 }, three_weights[4] = { 0, 2, 1, 0 };

	memset(block, 0, 16);
	for (texel = 0; texel < 16; texel++)
		transparent |= !four && ((indices >> (texel * 2)) & 3) == 3;
	if (transparent)
		bits = 7;
	endpoints[0][0] = dxt1_astc_expand5(c0 >> 11);
	endpoints[0][1] = dxt1_astc_expand6((c0 >> 5) & 0x3f);
	endpoints[0][2] = dxt1_astc_expand5(c0 & 0x1f);
	endpoints[1][0] = dxt1_astc_expand5(c1 >> 11);
	endpoints[1][1] = dxt1_astc_expand6((c1 >> 5) & 0x3f);
	endpoints[1][2] = dxt1_astc_expand5(c1 & 0x1f);
	for (texel = 0; texel < 2; texel++)
	{
		sums[texel] = 0;
		for (channel = 0; channel < 3; channel++)
		{
			/* (7 bits: the nearest the decoder's bit replication gives) */
			if (bits == 7)
				endpoints[texel][channel] = ((endpoints[texel][channel] >> 1) << 1) | (endpoints[texel][channel] >> 7);
			sums[texel] += endpoints[texel][channel];
		}
	}
	/* (the decoder swaps the endpoints, and blends blue into the others,
	where the second's sum is less: the lesser comes first here instead) */
	swap = sums[1] < sums[0];
	for (texel = 0; texel < 16; texel++)
	{
		unsigned long index = (indices >> (texel * 2)) & 3;
		unsigned char weight = four ? four_weights[index] : three_weights[index];

		if (!four && index == 3)
			weight = 0;
		else if (swap)
			weight = (unsigned char)(maximum - weight);
		if (transparent)
		{
			weights[texel * 2] = weight;
			weights[texel * 2 + 1] = index != 3 ? 2 : 0;
		}
		else
			weights[texel] = weight;
	}
	/* the block mode (a 4x4 grid; 2-bit weights, trits, or trits on two
	planes), one partition, and its color endpoint mode (RGB direct, or
	RGBA direct) */
	astc_bits(block, 0, 11, four ? 0x042 : transparent ? 0x451 : 0x051, 0);
	astc_bits(block, 13, 4, transparent ? 12 : 8, 0);
	for (channel = 0; channel < 3; channel++)
	{
		astc_bits(block, 17 + channel * 2 * bits, bits, endpoints[swap][channel] >> (8 - bits), 0);
		astc_bits(block, 17 + (channel * 2 + 1) * bits, bits, endpoints[!swap][channel] >> (8 - bits), 0);
	}
	if (transparent)
	{
		/* (alpha 0 at the first endpoint and 255 at the second, and on the
		second plane: the color component selector, below the weights) */
		astc_bits(block, 17 + 7 * bits, bits, 0x7f, 0);
		astc_bits(block, 128 - 52 - 2, 2, 3, 0);
		astc_trit_weights(block, weights, 32);
	}
	else if (four)
	{
		for (texel = 0; texel < 16; texel++)
			astc_bits(block, texel * 2, 2, weights[texel], 1);
	}
	else
		astc_trit_weights(block, weights, 16);
}

/* a level's DXT1 blocks (width by height texels) as ASTC 4x4 blocks, twice the bytes */
static void dxt1_astc_level(const unsigned char *source, unsigned long width, unsigned long height,
	unsigned char *blocks)
{
	unsigned long block, count = ((width + 3) / 4) * ((height + 3) / 4);

	for (block = 0; block < count; block++)
		dxt1_astc_block(source + block * 8, blocks + block * 16);
}

#endif
