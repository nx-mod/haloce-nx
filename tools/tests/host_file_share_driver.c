/* tools/test_host_file_share.py's: a host and a joiner of host_file_share.c
in one process over loopback, with real files. */
#include "host_file_share.h"
#include "host_file_share_crypto.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* guest addresses, as host_file_share.c takes them: indices into this */
static void *pointers[64];
static unsigned pointer_count = 1;
void *host_file_share_test_pointer(unsigned int address) { return address < pointer_count ? pointers[address] : NULL; }
static unsigned address_of(void *pointer) { pointers[pointer_count] = pointer; return pointer_count++; }

static void pause_ms(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, NULL); }

static const uint32_t sizes[] = { 0, 1, 70000, 3u << 20, 17, 65528, 65529 };
#define COUNT ((int)(sizeof(sizes) / sizeof(sizes[0])))

void host_file_share_test_rate(FILE *script);

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "rate")) {
		host_file_share_test_rate(stdin);
		return 0;
	}
	char const *dir = argv[1], *mode = argv[2];
	unsigned limit = argc > 3 ? (unsigned)atoi(argv[3]) : 0;
	unsigned char token[32], joiner_token[32], digests[COUNT][32];
	char source[COUNT][512], target[COUNT][512];
	uint32_t session = 0x12345678;
	struct host_fs_fetch_status status;
	uint32_t progress[4] = { 0 };
	int i, state, polls = 0;
	unsigned status_address = address_of(&status);

	fs_random(token, 32);
	memcpy(joiner_token, token, 32);
	if (!strcmp(mode, "wrong-token"))
		joiner_token[5] ^= 1;
	for (i = 0; i < COUNT; i++) {
		unsigned char *data = malloc(sizes[i] + 1);
		FILE *file;
		uint32_t k;

		for (k = 0; k < sizes[i]; k++)
			data[k] = (unsigned char)(k * 31 + i);
		snprintf(source[i], sizeof(source[i]), "%s/source%d", dir, i);
		snprintf(target[i], sizeof(target[i]), "%s/target%d.part", dir, i);
		file = fopen(source[i], "wb");
		fwrite(data, 1, sizes[i], file);
		fclose(file);
		fs_sha256(data, sizes[i], digests[i]);
		free(data);
	}
	if (!strcmp(mode, "bad-hash"))
		digests[3][0] ^= 1;
	if (host_fs_host_frame(1, limit) != 1) {
		printf("no listener\n");
		return 1;
	}
	if (host_fs_serve_begin(session, address_of(token), COUNT) < 0)
		return 2;
	for (i = 0; i < COUNT; i++)
		host_fs_serve_file(session, i, address_of(source[i]), sizes[i]);
	host_fs_serve_ready(session);
	if (!strcmp(mode, "pieces") || !strcmp(mode, "many-pieces")) {
		/* a map's pieces, as map_share.c's swarm asks them: every piece of the
		 * 3 MB file, the fifth with a wrong hash; each taken and checked */
		enum { PIECE = 256 * 1024 };
		static unsigned char whole[3u << 20], piece[PIECE], hashes[16][32];
		uint32_t out[4];
		unsigned buffer_address = address_of(piece), out_address = address_of(out);
		unsigned hash_addresses[16];
		int count = (int)((sizes[3] + PIECE - 1) / PIECE), asked = 0, taken = 0, matched = 0, wrong = 0;
		/* (many-pieces: each piece asked 40 times, far more than the host's
		 * wants hold at once, as a swarm's source is asked all download long) */
		int rounds = !strcmp(mode, "many-pieces") ? 40 : 1, total;
		FILE *file = fopen(source[3], "rb");

		if (!file || fread(whole, 1, sizes[3], file) != sizes[3])
			return 5;
		fclose(file);
		for (i = 0; i < count; i++) {
			uint32_t length = sizes[3] - (uint32_t)i * PIECE < PIECE ? sizes[3] - (uint32_t)i * PIECE : PIECE;

			fs_blake2b_256(whole + (size_t)i * PIECE, length, hashes[i]);
			hash_addresses[i] = address_of(hashes[i]);
		}
		if (rounds == 1)
			hashes[4][0] ^= 1;
		total = count * rounds;
		if (!host_fs_pieces_begin(0x0100007f, HOST_FILE_SHARE_PORT, session, address_of(token)))
			return 6;
		while (taken < total && polls++ < 3000) {
			while (asked < total) {
				int index = asked % count;
				uint32_t length = sizes[3] - (uint32_t)index * PIECE < PIECE ? sizes[3] - (uint32_t)index * PIECE : PIECE;

				if (!host_fs_pieces_ask(3, (uint32_t)index * PIECE, length, hash_addresses[index]))
					break;
				asked++;
			}
			while (host_fs_pieces_take(buffer_address, out_address)) {
				taken++;
				if (out[0] != 3 || out[1] % PIECE)
					wrong++;
				else if (out[3]) {
					matched++;
					if (memcmp(piece, whole + out[1], out[2]))
						wrong++;
				} else if (out[1] != 4u * PIECE)
					wrong++;
			}
			if (host_fs_pieces_state() == HOST_FS_FETCH_FAILED_EARLY || host_fs_pieces_state() == HOST_FS_FETCH_FAILED_LATE)
				break;
			pause_ms(5);
		}
		printf("pieces taken=%d matched=%d wrong=%d state=%d\n", taken, matched, wrong, host_fs_pieces_state());
		host_fs_pieces_stop();
		host_fs_serve_end(session);
		host_fs_host_frame(0, 0);
		pause_ms(300);
		return 0;
	}
	/* (direct-closed: the forwarded address answers nothing, the usual one
	 * does; direct-works: the forwarded address is the host, the usual one
	 * a closed port) */
	if (!strcmp(mode, "direct-closed"))
		host_fs_direct(0x0100007f, 1);
	if (!strcmp(mode, "direct-works"))
		host_fs_direct(0x0100007f, HOST_FILE_SHARE_PORT);
	if (!host_fs_fetch_begin(0x0100007f, !strcmp(mode, "direct-works") ? 1 : HOST_FILE_SHARE_PORT,
		!strcmp(mode, "unknown-session") ? session + 1 : session,
		address_of(joiner_token), COUNT))
		return 3;
	for (i = 0; i < COUNT; i++)
		host_fs_fetch_file(i, i, address_of(target[i]), sizes[i], address_of(digests[i]));
	if (!host_fs_fetch_start())
		return 4;
	for (;;) {
		state = host_fs_fetch_status(status_address);
		if (!strcmp(mode, "stop") && status.received_low > 100000) {
			host_fs_fetch_stop();
			printf("stopped state=%d\n", host_fs_fetch_status(status_address));
			host_fs_host_frame(0, 0);
			return 0;
		}
		if (state == HOST_FS_FETCH_DONE || state == HOST_FS_FETCH_FAILED_EARLY || state == HOST_FS_FETCH_FAILED_LATE)
			break;
		if (++polls > 3000)
			break;
		pause_ms(10);
	}
	pause_ms(100);
	host_fs_serve_progress(session, address_of(progress));
	printf("state=%d files=%d received=%u why=%s\n", state, status.files_done, status.received_low, status.why);
	printf("sent=%u files_sent=%u flags=%x\n", progress[0], progress[2], progress[3]);
	if (state == HOST_FS_FETCH_DONE) {
		for (i = 0; i < COUNT; i++) {
			FILE *a = fopen(source[i], "rb"), *b = fopen(target[i], "rb");
			int ca, cb, same = a && b;

			while (same && (ca = fgetc(a)) != EOF) {
				cb = fgetc(b);
				same = ca == cb;
			}
			if (same && b)
				same = fgetc(b) == EOF;
			printf("file%d %s\n", i, same ? "same" : "DIFFERENT");
			if (a) fclose(a);
			if (b) fclose(b);
		}
	}
	host_fs_fetch_stop();
	host_fs_serve_end(session);
	host_fs_host_frame(0, 0);
	pause_ms(300);
	return 0;
}
