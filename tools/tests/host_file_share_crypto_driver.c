/* tools/test_host_file_share.py's: the file connection's ciphers and HMAC
over inputs named on the command line, printed in hex for Python's
cryptography to compare. */
#include "host_file_share_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t from_hex(char const *text, unsigned char *out, size_t maximum)
{
	size_t n = strlen(text) / 2, i;

	if (n > maximum)
		n = maximum;
	for (i = 0; i < n; i++) {
		unsigned value;

		sscanf(text + 2 * i, "%2x", &value);
		out[i] = (unsigned char)value;
	}
	return n;
}

static void hex(unsigned char const *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		printf("%02x", p[i]);
	printf("\n");
}

/* aead <suite> <key> <nonce> <additional> <plaintext>: the ciphertext and tag,
then whether opening it works, and whether opening it with a bit flipped fails */
int main(int argc, char **argv)
{
	static unsigned char key[32], nonce[12], additional[4096], in[1 << 20], out[1 << 20], back[1 << 20], tag[16];
	struct fs_aead aead;
	size_t additional_size, size;

	if (argc == 2 && !strcmp(argv[1], "fast")) {
		printf("%d %s\n", fs_crypto_aes_fast(), fs_crypto_aes_kind());
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "hmac")) {
		unsigned char k[256], d[4096], mac[32];
		size_t kn = from_hex(argv[2], k, sizeof(k)), dn = from_hex(argv[3], d, sizeof(d));

		fs_hmac_sha256(k, kn, d, dn / 2, d + dn / 2, dn - dn / 2, mac);
		hex(mac, 32);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "x25519")) {
		unsigned char secret[32], peer[32], shared[32];

		from_hex(argv[2], secret, 32);
		from_hex(argv[3], peer, 32);
		printf("%d ", fs_x25519_shared(shared, secret, peer));
		hex(shared, 32);
		return 0;
	}
	if (argc != 7 || strcmp(argv[1], "aead"))
		return 2;
	from_hex(argv[3], key, 32);
	from_hex(argv[4], nonce, 12);
	additional_size = from_hex(argv[5], additional, sizeof(additional));
	size = from_hex(argv[6], in, sizeof(in));
	if (!fs_aead_init(&aead, atoi(argv[2]), key)) {
		printf("unavailable\n");
		return 0;
	}
	fs_aead_seal(&aead, nonce, additional, additional_size, in, size, out, tag);
	hex(out, size);
	hex(tag, 16);
	printf("%d\n", fs_aead_open(&aead, nonce, additional, additional_size, out, size, back, tag) &&
		!memcmp(back, in, size));
	if (size)
		out[size / 2] ^= 1;
	else
		tag[0] ^= 1;
	printf("%d\n", fs_aead_open(&aead, nonce, additional, additional_size, out, size, back, tag));
	return 0;
}
