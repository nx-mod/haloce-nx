/*
P2P_BULK_WINDOW.C (test)

A map-sharing stream's send window over the internet play tunnel
(port/linux/src/p2p.c's stream_tune): sized to the upload's cap over twice
the round trip, within its bounds and its part of the budget all of them
share; its share halved when the round trip grows well past the least seen
or resends climb, once a round trip, and grown back slowly.
test_p2p_bulk_window.py takes the function (under_test.inc).
*/

#include "harness.h"

typedef int IINT32;
typedef unsigned int IUINT32;

enum
{
	STREAM_CHUNK_SIZE = 1024,
	BULK_WINDOW_MINIMUM = 32,
	BULK_WINDOW_MAXIMUM = 1024,
	BULK_WINDOW_BUDGET = 4096
};

typedef struct
{
	IINT32 rx_srtt;
	IUINT32 xmit;
} ikcpcb;

struct stream
{
	ikcpcb *kcp;
	int window;
	int share;
	IINT32 base_rtt;
	IUINT32 seen_xmit;
	unsigned long tuned_time;
};

static struct
{
	int hosting;
} p2p = { 1 };

static unsigned long clock_now = 100000;
static long share_upload = 1024, seed_upload = 256;
static int window_set = -1;

static unsigned long p2p_now(void)
{
	return clock_now;
}

static int elapsed(unsigned long since, unsigned long span)
{
	return clock_now - since >= span;
}

static long config_integer(char const *name)
{
	return !strcmp(name, "network.map_share_upload") ? share_upload : seed_upload;
}

static int ikcp_wndsize(ikcpcb *kcp, int send, int receive)
{
	(void)kcp;
	(void)receive;
	window_set = send;
	return 0;
}

#include "under_test.inc"

static ikcpcb kcp;
static struct stream stream;

static void reset(IINT32 rtt)
{
	memset(&kcp, 0, sizeof(kcp));
	memset(&stream, 0, sizeof(stream));
	kcp.rx_srtt = rtt;
	stream.kcp = &kcp;
	stream.window = 128;
	stream.share = 1000;
	stream.tuned_time = clock_now;
	window_set = -1;
}

/* a round trip passes and the stream is tuned */
static void tune(int bulk_count)
{
	clock_now += (unsigned long)(kcp.rx_srtt > 50 ? kcp.rx_srtt : 50);
	stream_tune(&stream, bulk_count);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	int i;

	CASE("cap-sized")
	{
		/* 1 MB/s (1024 messages a second) over twice 100 ms: 204 messages */
		reset(100);
		tune(1);
		CHECK(stream.window == 204 && window_set == 204, "1 MB/s at 100 ms: 204 messages (%d)", stream.window);
		share_upload = 5120;
		tune(1);
		CHECK(stream.window == 1024, "5 MB/s at 100 ms: 1024 messages (%d)", stream.window);
		return 0;
	}
	CASE("seeding-cap")
	{
		/* not hosting: the seeding cap's (256 KB/s over 200 ms, twice: 100) */
		p2p.hosting = 0;
		reset(200);
		tune(1);
		CHECK(stream.window == 102, "a seed's 256 KB/s at 200 ms: 102 messages (%d)", stream.window);
		return 0;
	}
	CASE("bounds")
	{
		share_upload = 20480;
		reset(400);
		tune(1);
		CHECK(stream.window == BULK_WINDOW_MAXIMUM, "never above the maximum (%d)", stream.window);
		share_upload = 16;
		reset(20);
		tune(1);
		CHECK(stream.window == BULK_WINDOW_MINIMUM, "never below the minimum (%d)", stream.window);
		return 0;
	}
	CASE("budget")
	{
		/* eight such streams share the budget: 512 messages each at most */
		share_upload = 20480;
		reset(400);
		tune(8);
		CHECK(stream.window == BULK_WINDOW_BUDGET / 8, "a part of the budget (%d)", stream.window);
		return 0;
	}
	CASE("round-trip-grows")
	{
		reset(100);
		tune(1);
		CHECK(stream.window == 204 && stream.base_rtt == 100, "settled (%d)", stream.window);
		/* the path's buffers fill: 100 ms becomes 300 */
		kcp.rx_srtt = 300;
		tune(1);
		CHECK(stream.share == 500, "the share halved (%d)", stream.share);
		CHECK(stream.window == 307, "half of 614 at 300 ms (%d)", stream.window);
		return 0;
	}
	CASE("resends")
	{
		reset(100);
		tune(1);
		kcp.xmit += 40;
		tune(1);
		CHECK(stream.share == 500 && stream.window == 102, "resends halve it (%d, %d)", stream.share, stream.window);
		return 0;
	}
	CASE("recovers")
	{
		reset(100);
		tune(1);
		kcp.rx_srtt = 300;
		tune(1);
		tune(1);
		CHECK(stream.share == 250, "halved twice (%d)", stream.share);
		kcp.rx_srtt = 100;
		for (i = 0; i < 15; i++)
			tune(1);
		CHECK(stream.share == 1000 && stream.window == 204, "back, a twentieth a round trip (%d, %d)", stream.share,
			stream.window);
		return 0;
	}
	CASE("floor")
	{
		reset(100);
		tune(1);
		kcp.rx_srtt = 1000;
		for (i = 0; i < 10; i++)
			tune(1);
		CHECK(stream.share == 125, "never below an eighth (%d)", stream.share);
		return 0;
	}
	CASE("once-a-round-trip")
	{
		reset(100);
		tune(1);
		kcp.rx_srtt = 300;
		clock_now += 10;
		stream_tune(&stream, 1);
		CHECK(stream.share == 1000, "not looked at again within a round trip (%d)", stream.share);
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
