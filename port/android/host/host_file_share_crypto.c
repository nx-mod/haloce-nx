/* The file connection's cryptography (host_file_share_crypto.h).
 *
 * AES-256-GCM is done only with the CPU's instructions, chosen at run time
 * as host_mcc_hash.c chooses its SHA-256's: on ARMv8 AESE/AESMC for the
 * rounds and PMULL for GHASH, on x86-64 AES-NI and PCLMULQDQ. A CPU with
 * neither does ChaCha20-Poly1305 instead (the handshake picks it), so there is
 * no table-driven AES here to leak timings.
 *
 * GHASH works on each 16-byte block with every byte's bits reversed (ARMv8's
 * RBIT; on x86-64 two nibble lookups), which puts x^i at bit i of the block
 * read as two little-endian 64-bit halves: products are then plain carryless
 * multiplications, reduced by x^128 = x^7 + x^2 + x + 1 (0x87). Four blocks
 * are folded at a time with H to H^4, reduced once.
 *
 * Measured on a Galaxy Z Fold (2026-11-15, docs/file_share_tcp_plan.md): about
 * 950 MB/s on a little core, 2.6 GB/s on the big one, against 230 to 390 MB/s
 * for ChaCha20-Poly1305. Checked against Python's cryptography
 * (tools/test_host_file_share.py). */
#ifndef HOST_FILE_SHARE_TEST
#include "host.h"
#endif
#include "host_file_share_crypto.h"
#include "host_mcc_hash.h"
#include "posix.h"
#include "monocypher.h"

#include <pthread.h>
#include <string.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#if defined(__ANDROID__) || defined(__linux__)
#include <sys/auxv.h>
#endif
#elif defined(__x86_64__)
#include <cpuid.h>
#include <immintrin.h>
#endif

/* ---------- AES-256's key schedule (the S-box only here, at setup) */

static const unsigned char sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static void aes256_expand(unsigned char const key[32], unsigned char round_keys[15][16])
{
	unsigned char w[240];
	unsigned char rcon = 1;
	int i;

	memcpy(w, key, 32);
	for (i = 32; i < 240; i += 4) {
		unsigned char t[4];

		memcpy(t, w + i - 4, 4);
		if (i % 32 == 0) {
			unsigned char first = t[0];

			t[0] = (unsigned char)(sbox[t[1]] ^ rcon);
			t[1] = sbox[t[2]];
			t[2] = sbox[t[3]];
			t[3] = sbox[first];
			rcon = (unsigned char)((rcon << 1) ^ ((rcon & 0x80) ? 0x1b : 0));
		} else if (i % 32 == 16) {
			t[0] = sbox[t[0]];
			t[1] = sbox[t[1]];
			t[2] = sbox[t[2]];
			t[3] = sbox[t[3]];
		}
		w[i] = (unsigned char)(w[i - 32] ^ t[0]);
		w[i + 1] = (unsigned char)(w[i - 31] ^ t[1]);
		w[i + 2] = (unsigned char)(w[i - 30] ^ t[2]);
		w[i + 3] = (unsigned char)(w[i - 29] ^ t[3]);
	}
	memcpy(round_keys, w, 240);
	fs_wipe(w, sizeof(w));
}

static void counter_block(unsigned char block[16], unsigned char const nonce[12], uint32_t counter)
{
	memcpy(block, nonce, 12);
	block[12] = (unsigned char)(counter >> 24);
	block[13] = (unsigned char)(counter >> 16);
	block[14] = (unsigned char)(counter >> 8);
	block[15] = (unsigned char)counter;
}

static void lengths_block(unsigned char block[16], uint64_t additional_size, uint64_t size)
{
	uint64_t a = additional_size * 8, c = size * 8;
	int k;

	for (k = 0; k < 8; k++) {
		block[k] = (unsigned char)(a >> (56 - 8 * k));
		block[8 + k] = (unsigned char)(c >> (56 - 8 * k));
	}
}

/* ---------- ARMv8 */

#if defined(__aarch64__)
#define FS_HW_TARGET __attribute__((target("+aes"), unused))

FS_HW_TARGET static inline uint8x16_t arm_block(uint8x16_t const rk[15], uint8x16_t s)
{
	int r;

	for (r = 0; r < 13; r++)
		s = vaesmcq_u8(vaeseq_u8(s, rk[r]));
	return veorq_u8(vaeseq_u8(s, rk[13]), rk[14]);
}

FS_HW_TARGET static inline uint64x2_t arm_clmul(uint64_t a, uint64_t b)
{
	return vreinterpretq_u64_p128(vmull_p64((poly64_t)a, (poly64_t)b));
}

FS_HW_TARGET static inline void arm_mul_acc(uint64x2_t a, uint64x2_t b, uint64x2_t *lo, uint64x2_t *hi)
{
	uint64x2_t p0 = arm_clmul(vgetq_lane_u64(a, 0), vgetq_lane_u64(b, 0));
	uint64x2_t p1 = arm_clmul(vgetq_lane_u64(a, 1), vgetq_lane_u64(b, 1));
	uint64x2_t m = veorq_u64(arm_clmul(vgetq_lane_u64(a, 0), vgetq_lane_u64(b, 1)),
		arm_clmul(vgetq_lane_u64(a, 1), vgetq_lane_u64(b, 0)));

	*lo = veorq_u64(*lo, veorq_u64(p0, vcombine_u64(vdup_n_u64(0), vget_low_u64(m))));
	*hi = veorq_u64(*hi, veorq_u64(p1, vcombine_u64(vget_high_u64(m), vdup_n_u64(0))));
}

FS_HW_TARGET static inline uint64x2_t arm_reduce(uint64x2_t lo, uint64x2_t hi)
{
	uint64x2_t t0 = arm_clmul(vgetq_lane_u64(hi, 0), 0x87);
	uint64x2_t t1 = arm_clmul(vgetq_lane_u64(hi, 1), 0x87);
	uint64x2_t t2 = arm_clmul(vgetq_lane_u64(t1, 1), 0x87);

	return veorq_u64(veorq_u64(lo, t0), vcombine_u64(vget_low_u64(t2), vget_low_u64(t1)));
}

FS_HW_TARGET static inline uint64x2_t arm_field(uint8x16_t v)
{
	return vreinterpretq_u64_u8(vrbitq_u8(v));
}

FS_HW_TARGET static uint64x2_t arm_ghash(uint64x2_t const h[4], uint64x2_t y, unsigned char const *p, size_t n)
{
	while (n >= 64) {
		uint64x2_t lo = vdupq_n_u64(0), hi = vdupq_n_u64(0);

		arm_mul_acc(veorq_u64(y, arm_field(vld1q_u8(p))), h[3], &lo, &hi);
		arm_mul_acc(arm_field(vld1q_u8(p + 16)), h[2], &lo, &hi);
		arm_mul_acc(arm_field(vld1q_u8(p + 32)), h[1], &lo, &hi);
		arm_mul_acc(arm_field(vld1q_u8(p + 48)), h[0], &lo, &hi);
		y = arm_reduce(lo, hi);
		p += 64;
		n -= 64;
	}
	while (n) {
		unsigned char block[16] = { 0 };
		size_t take = n < 16 ? n : 16;
		uint64x2_t lo = vdupq_n_u64(0), hi = vdupq_n_u64(0);

		memcpy(block, p, take);
		arm_mul_acc(veorq_u64(y, arm_field(vld1q_u8(block))), h[0], &lo, &hi);
		y = arm_reduce(lo, hi);
		p += take;
		n -= take;
	}
	return y;
}

FS_HW_TARGET static void arm_load(struct fs_aead const *aead, uint8x16_t rk[15], uint64x2_t h[4])
{
	int i;

	for (i = 0; i < 15; i++)
		rk[i] = vld1q_u8(aead->round_keys[i]);
	for (i = 0; i < 4; i++)
		h[i] = vld1q_u64(aead->h[i]);
}

FS_HW_TARGET static void arm_init(struct fs_aead *aead)
{
	uint8x16_t rk[15];
	uint64x2_t h1, power, lo, hi;
	int i;

	for (i = 0; i < 15; i++)
		rk[i] = vld1q_u8(aead->round_keys[i]);
	h1 = arm_field(arm_block(rk, vdupq_n_u8(0)));
	power = h1;
	vst1q_u64(aead->h[0], h1);
	for (i = 1; i < 4; i++) {
		lo = vdupq_n_u64(0);
		hi = vdupq_n_u64(0);
		arm_mul_acc(power, h1, &lo, &hi);
		power = arm_reduce(lo, hi);
		vst1q_u64(aead->h[i], power);
	}
}

/* CTR over in (to out), then GHASH over the ciphertext; decrypting hashes
 * before it decrypts */
FS_HW_TARGET static void arm_gcm(struct fs_aead const *aead, unsigned char const nonce[12],
	unsigned char const *additional, size_t additional_size, unsigned char const *in, size_t size,
	unsigned char *out, unsigned char tag[16], int decrypting)
{
	uint8x16_t rk[15];
	uint64x2_t h[4], y = vdupq_n_u64(0), lo, hi;
	unsigned char block[16];
	uint32_t counter = 2;
	size_t i = 0;

	arm_load(aead, rk, h);
	y = arm_ghash(h, y, additional, additional_size);
	if (decrypting)
		y = arm_ghash(h, y, in, size);
	for (; i + 64 <= size; i += 64, counter += 4) {
		uint8x16_t k0, k1, k2, k3;

		counter_block(block, nonce, counter);
		k0 = arm_block(rk, vld1q_u8(block));
		counter_block(block, nonce, counter + 1);
		k1 = arm_block(rk, vld1q_u8(block));
		counter_block(block, nonce, counter + 2);
		k2 = arm_block(rk, vld1q_u8(block));
		counter_block(block, nonce, counter + 3);
		k3 = arm_block(rk, vld1q_u8(block));
		vst1q_u8(out + i, veorq_u8(vld1q_u8(in + i), k0));
		vst1q_u8(out + i + 16, veorq_u8(vld1q_u8(in + i + 16), k1));
		vst1q_u8(out + i + 32, veorq_u8(vld1q_u8(in + i + 32), k2));
		vst1q_u8(out + i + 48, veorq_u8(vld1q_u8(in + i + 48), k3));
	}
	for (; i < size; i += 16, counter++) {
		unsigned char stream[16];
		size_t take = size - i < 16 ? size - i : 16, j;

		counter_block(block, nonce, counter);
		vst1q_u8(stream, arm_block(rk, vld1q_u8(block)));
		for (j = 0; j < take; j++)
			out[i + j] = (unsigned char)(in[i + j] ^ stream[j]);
	}
	if (!decrypting)
		y = arm_ghash(h, y, out, size);
	lengths_block(block, additional_size, size);
	lo = vdupq_n_u64(0);
	hi = vdupq_n_u64(0);
	arm_mul_acc(veorq_u64(y, arm_field(vld1q_u8(block))), h[0], &lo, &hi);
	y = arm_reduce(lo, hi);
	counter_block(block, nonce, 1);
	vst1q_u8(tag, veorq_u8(vrbitq_u8(vreinterpretq_u8_u64(y)), arm_block(rk, vld1q_u8(block))));
}
#endif

/* ---------- x86-64 */

#if defined(__x86_64__)
#define FS_HW_TARGET __attribute__((target("aes,pclmul,sse4.1,ssse3"), unused))

FS_HW_TARGET static inline __m128i x86_block(__m128i const rk[15], __m128i s)
{
	int r;

	s = _mm_xor_si128(s, rk[0]);
	for (r = 1; r < 14; r++)
		s = _mm_aesenc_si128(s, rk[r]);
	return _mm_aesenclast_si128(s, rk[14]);
}

/* each byte's bits reversed */
FS_HW_TARGET static inline __m128i x86_rbit(__m128i x)
{
	__m128i const table = _mm_setr_epi8(0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF);
	__m128i const mask = _mm_set1_epi8(0x0f);
	__m128i low = _mm_and_si128(x, mask);
	__m128i high = _mm_and_si128(_mm_srli_epi16(x, 4), mask);

	return _mm_or_si128(_mm_slli_epi16(_mm_shuffle_epi8(table, low), 4), _mm_shuffle_epi8(table, high));
}

FS_HW_TARGET static inline void x86_mul_acc(__m128i a, __m128i b, __m128i *lo, __m128i *hi)
{
	__m128i p0 = _mm_clmulepi64_si128(a, b, 0x00);
	__m128i p1 = _mm_clmulepi64_si128(a, b, 0x11);
	__m128i m = _mm_xor_si128(_mm_clmulepi64_si128(a, b, 0x10), _mm_clmulepi64_si128(a, b, 0x01));

	*lo = _mm_xor_si128(*lo, _mm_xor_si128(p0, _mm_slli_si128(m, 8)));
	*hi = _mm_xor_si128(*hi, _mm_xor_si128(p1, _mm_srli_si128(m, 8)));
}

FS_HW_TARGET static inline __m128i x86_reduce(__m128i lo, __m128i hi)
{
	__m128i const k = _mm_set_epi64x(0, 0x87);
	__m128i t0 = _mm_clmulepi64_si128(hi, k, 0x00);
	__m128i t1 = _mm_clmulepi64_si128(hi, k, 0x01);
	__m128i t2 = _mm_clmulepi64_si128(t1, k, 0x01);

	return _mm_xor_si128(_mm_xor_si128(lo, t0), _mm_unpacklo_epi64(t2, t1));
}

FS_HW_TARGET static inline __m128i x86_load(unsigned char const *p)
{
	return _mm_loadu_si128((__m128i const *)p);
}

FS_HW_TARGET static __m128i x86_ghash(__m128i const h[4], __m128i y, unsigned char const *p, size_t n)
{
	while (n >= 64) {
		__m128i lo = _mm_setzero_si128(), hi = _mm_setzero_si128();

		x86_mul_acc(_mm_xor_si128(y, x86_rbit(x86_load(p))), h[3], &lo, &hi);
		x86_mul_acc(x86_rbit(x86_load(p + 16)), h[2], &lo, &hi);
		x86_mul_acc(x86_rbit(x86_load(p + 32)), h[1], &lo, &hi);
		x86_mul_acc(x86_rbit(x86_load(p + 48)), h[0], &lo, &hi);
		y = x86_reduce(lo, hi);
		p += 64;
		n -= 64;
	}
	while (n) {
		unsigned char block[16] = { 0 };
		size_t take = n < 16 ? n : 16;
		__m128i lo = _mm_setzero_si128(), hi = _mm_setzero_si128();

		memcpy(block, p, take);
		x86_mul_acc(_mm_xor_si128(y, x86_rbit(x86_load(block))), h[0], &lo, &hi);
		y = x86_reduce(lo, hi);
		p += take;
		n -= take;
	}
	return y;
}

FS_HW_TARGET static void x86_init(struct fs_aead *aead)
{
	__m128i rk[15], h1, power, lo, hi;
	int i;

	for (i = 0; i < 15; i++)
		rk[i] = x86_load(aead->round_keys[i]);
	h1 = x86_rbit(x86_block(rk, _mm_setzero_si128()));
	power = h1;
	_mm_storeu_si128((__m128i *)aead->h[0], h1);
	for (i = 1; i < 4; i++) {
		lo = _mm_setzero_si128();
		hi = _mm_setzero_si128();
		x86_mul_acc(power, h1, &lo, &hi);
		power = x86_reduce(lo, hi);
		_mm_storeu_si128((__m128i *)aead->h[i], power);
	}
}

FS_HW_TARGET static void x86_gcm(struct fs_aead const *aead, unsigned char const nonce[12],
	unsigned char const *additional, size_t additional_size, unsigned char const *in, size_t size,
	unsigned char *out, unsigned char tag[16], int decrypting)
{
	__m128i rk[15], h[4], y = _mm_setzero_si128(), lo, hi;
	unsigned char block[16];
	uint32_t counter = 2;
	size_t i = 0;
	int r;

	for (r = 0; r < 15; r++)
		rk[r] = x86_load(aead->round_keys[r]);
	for (r = 0; r < 4; r++)
		h[r] = x86_load((unsigned char const *)aead->h[r]);
	y = x86_ghash(h, y, additional, additional_size);
	if (decrypting)
		y = x86_ghash(h, y, in, size);
	for (; i + 64 <= size; i += 64, counter += 4) {
		__m128i k0, k1, k2, k3;

		counter_block(block, nonce, counter);
		k0 = x86_block(rk, x86_load(block));
		counter_block(block, nonce, counter + 1);
		k1 = x86_block(rk, x86_load(block));
		counter_block(block, nonce, counter + 2);
		k2 = x86_block(rk, x86_load(block));
		counter_block(block, nonce, counter + 3);
		k3 = x86_block(rk, x86_load(block));
		_mm_storeu_si128((__m128i *)(out + i), _mm_xor_si128(x86_load(in + i), k0));
		_mm_storeu_si128((__m128i *)(out + i + 16), _mm_xor_si128(x86_load(in + i + 16), k1));
		_mm_storeu_si128((__m128i *)(out + i + 32), _mm_xor_si128(x86_load(in + i + 32), k2));
		_mm_storeu_si128((__m128i *)(out + i + 48), _mm_xor_si128(x86_load(in + i + 48), k3));
	}
	for (; i < size; i += 16, counter++) {
		unsigned char stream[16];
		size_t take = size - i < 16 ? size - i : 16, j;

		counter_block(block, nonce, counter);
		_mm_storeu_si128((__m128i *)stream, x86_block(rk, x86_load(block)));
		for (j = 0; j < take; j++)
			out[i + j] = (unsigned char)(in[i + j] ^ stream[j]);
	}
	if (!decrypting)
		y = x86_ghash(h, y, out, size);
	lengths_block(block, additional_size, size);
	lo = _mm_setzero_si128();
	hi = _mm_setzero_si128();
	x86_mul_acc(_mm_xor_si128(y, x86_rbit(x86_load(block))), h[0], &lo, &hi);
	y = x86_reduce(lo, hi);
	counter_block(block, nonce, 1);
	_mm_storeu_si128((__m128i *)tag, _mm_xor_si128(x86_rbit(y), x86_block(rk, x86_load(block))));
}
#endif

/* ---------- which */

static pthread_once_t aes_once = PTHREAD_ONCE_INIT;
static int aes_fast;
static char const *aes_kind = "none (ChaCha20-Poly1305)";

static void aes_check(void)
{
#if defined(HOST_FILE_SHARE_NO_AES)
	return;
#elif defined(__aarch64__)
	int fast = 1;
#if defined(__ANDROID__) || defined(__linux__)
	unsigned long hwcap = getauxval(AT_HWCAP);

	/* (HWCAP_AES, HWCAP_PMULL) */
	fast = (hwcap & (1ul << 3)) && (hwcap & (1ul << 4));
#endif
	/* (the Switch's Cortex-A57 has them) */
	if (fast) {
		aes_fast = 1;
		aes_kind = "ARMv8 AES and PMULL";
	}
#elif defined(__x86_64__)
	unsigned int a, b, c, d;

	if (__get_cpuid(1, &a, &b, &c, &d) && (c & (1u << 25)) && (c & (1u << 1)) && (c & (1u << 9)) &&
		(c & (1u << 19))) {
		aes_fast = 1;
		aes_kind = "AES-NI and PCLMULQDQ";
	}
#endif
}

int fs_crypto_aes_fast(void)
{
	pthread_once(&aes_once, aes_check);
	return aes_fast;
}

char const *fs_crypto_aes_kind(void)
{
	pthread_once(&aes_once, aes_check);
	return aes_kind;
}

/* ---------- the AEAD */

int fs_aead_init(struct fs_aead *aead, int suite, unsigned char const key[32])
{
	memset(aead, 0, sizeof(*aead));
	if (suite == FS_SUITE_CHACHA20_POLY1305) {
		aead->suite = suite;
		memcpy(aead->key, key, 32);
		return 1;
	}
	if (suite != FS_SUITE_AES256_GCM || !fs_crypto_aes_fast())
		return 0;
	aead->suite = suite;
	aes256_expand(key, aead->round_keys);
#if defined(__aarch64__)
	arm_init(aead);
#elif defined(__x86_64__)
	x86_init(aead);
#endif
	return 1;
}

void fs_aead_seal(struct fs_aead const *aead, unsigned char const nonce[12], unsigned char const *additional,
	size_t additional_size, unsigned char const *in, size_t size, unsigned char *out, unsigned char tag[16])
{
	if (aead->suite == FS_SUITE_CHACHA20_POLY1305) {
		crypto_aead_ctx context;

		crypto_aead_init_ietf(&context, aead->key, nonce);
		crypto_aead_write(&context, out, tag, additional, additional_size, in, size);
		crypto_wipe(&context, sizeof(context));
		return;
	}
#if defined(__aarch64__)
	arm_gcm(aead, nonce, additional, additional_size, in, size, out, tag, 0);
#elif defined(__x86_64__)
	x86_gcm(aead, nonce, additional, additional_size, in, size, out, tag, 0);
#else
	(void)nonce; (void)additional; (void)additional_size; (void)in; (void)size; (void)out;
	memset(tag, 0, 16);
#endif
}

int fs_aead_open(struct fs_aead const *aead, unsigned char const nonce[12], unsigned char const *additional,
	size_t additional_size, unsigned char const *in, size_t size, unsigned char *out, unsigned char const tag[16])
{
	unsigned char expected[16];
	int good;

	if (aead->suite == FS_SUITE_CHACHA20_POLY1305) {
		crypto_aead_ctx context;

		crypto_aead_init_ietf(&context, aead->key, nonce);
		good = crypto_aead_read(&context, out, tag, additional, additional_size, in, size) == 0;
		crypto_wipe(&context, sizeof(context));
		return good;
	}
#if defined(__aarch64__)
	arm_gcm(aead, nonce, additional, additional_size, in, size, out, expected, 1);
#elif defined(__x86_64__)
	x86_gcm(aead, nonce, additional, additional_size, in, size, out, expected, 1);
#else
	(void)nonce; (void)additional; (void)additional_size; (void)in; (void)size; (void)out;
	return 0;
#endif
	good = fs_equal(expected, tag, 16);
	return good;
}

void fs_aead_wipe(struct fs_aead *aead)
{
	fs_wipe(aead, sizeof(*aead));
}

/* ---------- HMAC-SHA256, X25519, the rest */

void fs_sha256(unsigned char const *data, size_t size, unsigned char out[32])
{
	struct host_sha256 sha;

	host_sha256_begin(&sha);
	host_sha256_add(&sha, data, size);
	host_sha256_end(&sha, out);
}

void fs_blake2b_256(void const *data, size_t size, unsigned char out[32])
{
	crypto_blake2b(out, 32, data, size);
}

void fs_hmac_sha256(unsigned char const *key, size_t key_size, unsigned char const *a, size_t a_size,
	unsigned char const *b, size_t b_size, unsigned char out[32])
{
	unsigned char block[64], inner[32];
	struct host_sha256 sha;
	int i;

	memset(block, 0, sizeof(block));
	if (key_size > 64)
		fs_sha256(key, key_size, block);
	else
		memcpy(block, key, key_size);
	for (i = 0; i < 64; i++)
		block[i] ^= 0x36;
	host_sha256_begin(&sha);
	host_sha256_add(&sha, block, 64);
	host_sha256_add(&sha, a, a_size);
	if (b_size)
		host_sha256_add(&sha, b, b_size);
	host_sha256_end(&sha, inner);
	for (i = 0; i < 64; i++)
		block[i] ^= 0x36 ^ 0x5c;
	host_sha256_begin(&sha);
	host_sha256_add(&sha, block, 64);
	host_sha256_add(&sha, inner, 32);
	host_sha256_end(&sha, out);
	fs_wipe(block, sizeof(block));
	fs_wipe(inner, sizeof(inner));
	fs_wipe(&sha, sizeof(sha));
}

void fs_x25519_keypair(unsigned char secret[32], unsigned char public_key[32])
{
	fs_random(secret, 32);
	crypto_x25519_public_key(public_key, secret);
}

int fs_x25519_shared(unsigned char out[32], unsigned char const secret[32], unsigned char const peer_public[32])
{
	static const unsigned char zero[32];

	crypto_x25519(out, secret, peer_public);
	return !fs_equal(out, zero, 32);
}

void fs_random(void *bytes, size_t size)
{
	posix_random_bytes(bytes, (posix_ulong)size);
}

void fs_wipe(void *bytes, size_t size)
{
	crypto_wipe(bytes, size);
}

int fs_equal(void const *a, void const *b, size_t size)
{
	unsigned char const *x = a, *y = b;
	unsigned char difference = 0;
	size_t i;

	for (i = 0; i < size; i++)
		difference |= (unsigned char)(x[i] ^ y[i]);
	return difference == 0;
}
