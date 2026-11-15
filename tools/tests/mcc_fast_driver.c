/*
MCC_FAST_DRIVER.C (test)

port/linux/game/mcc_fast.c built natively (tools/tests/mcc_fast_shim: its
Winsock over POSIX): a host and a joiner over loopback, the package's bytes
compared, the host's progress, and a joiner with no fast host to reach.
tools/test_mcc_fast.py builds it.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "mcc_fast.h"
#include "mcc_share_session.h"

FILE *mcc_share_open_native(char const *path) { return fopen(path, "rb"); }

unsigned long system_milliseconds(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)now.tv_sec * 1000ul + (unsigned long)(now.tv_nsec / 1000000);
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
#define LOOPBACK 0x7F000001ul

static struct mcc_share_host_transfer transfer;

int main(int argc, char **argv)
{
	static unsigned char *files[2], received[3 << 20];
	uint32_t sizes[2] = { 1500000, 777777 };
	unsigned long began;
	uint32_t got = 0;
	int i;

	if (argc < 3)
		return 2;
	if (!strcmp(argv[1], "refused")) {
		/* nothing listens: the host's own way at once */
		CHECK(mcc_fast_client_begin(LOOPBACK, 7, "mcc_maps\\pack\\level", 1, sizes));
		for (began = system_milliseconds(); system_milliseconds() - began < 5000 &&
			mcc_fast_client_state() == MCC_FAST_CONNECTING;)
			usleep(10000);
		CHECK(mcc_fast_client_state() == MCC_FAST_FAILED_EARLY);
		return 0;
	}
	for (i = 0; i < 2; i++) {
		char path[512];
		FILE *out;
		uint32_t at;

		files[i] = malloc(sizes[i]);
		for (at = 0; at < sizes[i]; at++)
			files[i][at] = (unsigned char)(at * 31 + i * 7 + (at >> 9));
		snprintf(path, sizeof(path), "%s/file%d", argv[2], i);
		out = fopen(path, "wb");
		CHECK(out && fwrite(files[i], 1, sizes[i], out) == sizes[i]);
		fclose(out);
		snprintf(transfer.package.files[i].path, sizeof(transfer.package.files[i].path), "%s", path);
		transfer.files[i].size = sizes[i];
	}
	transfer.used = 1;
	transfer.machine = 1;
	transfer.session = 77;
	transfer.offer_complete = 1;
	transfer.package.count = 2;
	snprintf(transfer.level, sizeof(transfer.level), "mcc_maps\\pack\\level");
	/* ("slow": the first file takes longer than the 1 s idle timeout of this build to send) */
	mcc_fast_host_frame(1, !strcmp(argv[1], "slow") ? 1 << 20 : 50 << 20);
	/* (the listener up) */
	for (i = 0; i < 50; i++) { mcc_fast_host_publish(&transfer, LOOPBACK); mcc_fast_host_end_frame(); usleep(10000); }
	if (!strcmp(argv[1], "wrong_session")) {
		/* a hello for no transfer of this machine: refused, nothing sent */
		CHECK(mcc_fast_client_begin(LOOPBACK, 78, transfer.level, 2, sizes));
		for (began = system_milliseconds(); system_milliseconds() - began < 15000 &&
			mcc_fast_client_state() == MCC_FAST_CONNECTING;) {
			mcc_fast_host_publish(&transfer, LOOPBACK);
			mcc_fast_host_end_frame();
			usleep(10000);
		}
		CHECK(mcc_fast_client_state() == MCC_FAST_FAILED_EARLY);
		return 0;
	}
	CHECK(mcc_fast_client_begin(LOOPBACK, 77, transfer.level, 2, sizes));
	for (began = system_milliseconds(); system_milliseconds() - began < 20000 && got < sizes[0] + sizes[1];) {
		mcc_fast_host_publish(&transfer, LOOPBACK);
		mcc_fast_host_end_frame();
		got += mcc_fast_client_take(received + got, (uint32_t)sizeof(received) - got);
		CHECK(mcc_fast_client_state() != MCC_FAST_FAILED_EARLY && mcc_fast_client_state() != MCC_FAST_FAILED_LATE);
		usleep(2000);
	}
	CHECK(got == sizes[0] + sizes[1]);
	CHECK(!memcmp(received, files[0], sizes[0]) && !memcmp(received + sizes[0], files[1], sizes[1]));
	for (began = system_milliseconds(); system_milliseconds() - began < 2000 && mcc_fast_client_state() != MCC_FAST_DONE;)
		usleep(5000);
	CHECK(mcc_fast_client_state() == MCC_FAST_DONE);
	{
		uint64_t sent = 0;
		int done = 0;
		unsigned long at = 0;

		CHECK(mcc_fast_host_progress(1, 77, &sent, &done, &at));
		CHECK(sent == sizes[0] + sizes[1] && done == 2);
	}
	return 0;
}
