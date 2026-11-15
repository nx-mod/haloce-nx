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

int main(int argc, char **argv)
{
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
	if (!host_fs_fetch_begin(0x0100007f, HOST_FILE_SHARE_PORT, !strcmp(mode, "unknown-session") ? session + 1 : session,
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
