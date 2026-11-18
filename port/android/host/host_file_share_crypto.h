/* The file connection's cryptography (host_file_share.c; docs/file_share_tcp_plan.md):
 * AES-256-GCM with the CPU's instructions (ARMv8's AESE/AESMC and PMULL,
 * x86-64's AES-NI and PCLMULQDQ), ChaCha20-Poly1305 (RFC 8439, Monocypher's)
 * where a CPU has none, X25519 (Monocypher's) and HMAC-SHA256 (the host's
 * SHA-256, host_mcc_hash.c). In the host of every build. */
#ifndef HOST_FILE_SHARE_CRYPTO_H
#define HOST_FILE_SHARE_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

enum {
	FS_SUITE_AES256_GCM = 1,
	FS_SUITE_CHACHA20_POLY1305 = 2,
};

struct fs_aead {
	int suite;
	unsigned char key[32];
	/* AES-256's round keys, and GHASH's H to H^4 (each byte's bits reversed:
	 * x^i at bit i of the two 64-bit halves) */
	unsigned char round_keys[15][16];
	uint64_t h[4][2];
};

/* whether this CPU has AES-256-GCM's instructions (checked once) */
int fs_crypto_aes_fast(void);
/* the name of what does AES here, for the log */
char const *fs_crypto_aes_kind(void);

/* 0 for a suite this CPU cannot do (AES-256-GCM without its instructions) */
int fs_aead_init(struct fs_aead *aead, int suite, unsigned char const key[32]);
/* out may be in; tag is 16 bytes after nothing (the caller places it) */
void fs_aead_seal(struct fs_aead const *aead, unsigned char const nonce[12], unsigned char const *additional,
	size_t additional_size, unsigned char const *in, size_t size, unsigned char *out, unsigned char tag[16]);
/* 1 when the tag is right (out then holds the plaintext), else 0 (out's
 * contents are not to be used) */
int fs_aead_open(struct fs_aead const *aead, unsigned char const nonce[12], unsigned char const *additional,
	size_t additional_size, unsigned char const *in, size_t size, unsigned char *out, unsigned char const tag[16]);
void fs_aead_wipe(struct fs_aead *aead);

/* HMAC-SHA256 of two pieces (the second may be empty) */
void fs_hmac_sha256(unsigned char const *key, size_t key_size, unsigned char const *a, size_t a_size,
	unsigned char const *b, size_t b_size, unsigned char out[32]);
void fs_sha256(unsigned char const *data, size_t size, unsigned char out[32]);
/* BLAKE2b's 32-byte hash (Monocypher's): a Custom Edition map's pieces'
 * (port/linux/game/map_share_files.c) */
void fs_blake2b_256(void const *data, size_t size, unsigned char out[32]);

/* X25519: a new key pair, and the shared secret (0 if it is all zeros: a
 * public key of low order) */
void fs_x25519_keypair(unsigned char secret[32], unsigned char public_key[32]);
int fs_x25519_shared(unsigned char out[32], unsigned char const secret[32], unsigned char const peer_public[32]);

void fs_random(void *bytes, size_t size);
void fs_wipe(void *bytes, size_t size);
/* constant time: 1 when equal */
int fs_equal(void const *a, void const *b, size_t size);

#endif
