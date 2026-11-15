/* Files between two SlipstreamCE machines over a TCP connection of their own,
 * encrypted as the internet play tunnel is (docs/file_share_tcp_plan.md). In
 * the host of every build, on threads of its own; the guest decides what goes
 * where (port/linux/game/mcc_share.c) through host_imports.list's host_fs_*.
 *
 * Stage 1: an MCC package's files from the game's host to a joiner, after the
 * joiner accepted OpenCE's offer (the manifest still comes over the game's
 * connection). The joiner's guest names each file's .part path in its staged
 * download, its size and SHA-256; this side writes it there, hashes it as it
 * comes (the CPU's SHA-256), and says when each is whole and right, and the
 * guest's stage takes it as done (mcc_share_stage_external_done) without
 * reading it again.
 *
 * The connection, on FS_PORT (5154):
 *   joiner hello  'SLFS', version 1, suites (bit 0 AES-256-GCM with the CPU's
 *                 instructions, bit 1 ChaCha20-Poly1305), 2 zero, the
 *                 transfer's session (4), an ephemeral X25519 key (32), a
 *                 nonce (32)                                         76 bytes
 *   host reply    'SLFS', version 1, the suite chosen, a result (0 served,
 *                 1 no such transfer, 2 no suite in common), 1 zero, its
 *                 ephemeral key (32), its nonce (32)                 72 bytes
 * Then both: secret = X25519; prk = HMAC-SHA256(key = the two nonces, joiner's
 * first, data = secret || the transfer's token); each direction's key =
 * HMAC-SHA256(prk, its label || SHA-256(hello || reply)), so that a hello or a
 * reply altered on the way gives keys that open nothing. The token (32 random
 * bytes) went from the host to the joiner over the game's connection
 * (network_extensions.c's data message): over the internet that connection is
 * inside the tunnel's encryption, so a stranger at the port cannot make a key.
 *
 * Records: a 4-byte length (the ciphertext's, its 16-byte tag included,
 * little-endian), the additional data of the AEAD; its nonce 4 zero bytes and
 * a 64-bit counter of the direction's records. Up to FS_RECORD bytes of
 * plaintext, frames:
 *   WANT  1, 0, file (2), offset (4), length (4)    joiner: bytes of a file
 *   DATA  2, 0, file (2), offset (4), the bytes     host: what was wanted
 *   END   3, 0, 0, 0                                 either: nothing more
 *   KEEP  5, 0, 0, 0                                 joiner: still there (a
 *                                                    swarm's source may have
 *                                                    nothing to ask a while)
 *   READY 4, 0, 0, 0                                 host: its first record,
 *                                                    before anything is written
 * (a joiner writes nothing before READY opens: with a wrong token or a
 * forged host it fails before any file is touched, and the old way is taken)
 * The host sends what was wanted in the order asked, paced at the guest's
 * limit (the upload's cap). */
#ifndef HOST_FILE_SHARE_TEST
#include "host.h"
#define fs_log(...) host_logf(HOST_LOG_INFO, __VA_ARGS__)
/* (the guest's addresses are this process's, below 4 GB) */
#define GUEST_TEXT(address) ((char const *)(uintptr_t)(address))
#define GUEST_BYTES(address) ((unsigned char *)(uintptr_t)(address))
#else
#define fs_log(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
/* (the test's own "guest addresses": tools/tests/host_file_share_driver.c) */
void *host_file_share_test_pointer(unsigned int address);
#define GUEST_TEXT(address) ((char const *)host_file_share_test_pointer(address))
#define GUEST_BYTES(address) ((unsigned char *)host_file_share_test_pointer(address))
#endif
#include "host_file_share.h"
#include "host_file_share_crypto.h"
#include "host_mcc_hash.h"
#include "posix.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if (defined(__ANDROID__) || defined(__linux__)) && !defined(__SWITCH__)
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#if defined(__SWITCH__)
/* (port/switch/host/host_thread.c's: off the game's cores) */
void host_thread_place_on_helper_core(void);
#endif

/* (tests listen elsewhere) */
#ifndef HOST_FILE_SHARE_PORT
#define HOST_FILE_SHARE_PORT 5154
#endif

enum {
	FS_PORT = HOST_FILE_SHARE_PORT,
	FS_VERSION = 1,
	FS_HELLO_SIZE = 76,
	FS_REPLY_SIZE = 72,
	FS_RECORD = 65536,
	FS_TAG = 16,
	FS_HEADER = 4,
	FS_DATA_HEADER = 8,
	FS_WANT_SIZE = 12,
	FS_FILES = 168,
	/* (MCC transfers, and Custom Edition maps' joiners: a few at once) */
	FS_SERVES = 16,
	/* (the host: connections at once, of all its joiners, and of one
	 * transfer) */
	FS_HOST_CONNECTIONS = 32,
	FS_SESSION_CONNECTIONS = 16,
	FS_PATH = 1024,
	FS_SOCKET_BUFFER = 4 << 20,
	FS_FILE_BUFFER = 1 << 20,
	FS_CONNECT_MILLISECONDS = 6000,
	/* (a direct try at the host's forwarded internet address, before the
	 * usual way: short) */
	FS_DIRECT_CONNECT_MILLISECONDS = 3000,
	FS_DIRECT_HELLO_MILLISECONDS = 4000,
	FS_HELLO_MILLISECONDS = 8000,
	FS_IDLE_MILLISECONDS = 30000,
	FS_LISTEN_RETRY_MILLISECONDS = 10000,
	FS_SUITE_BIT_AES = 1,
	FS_SUITE_BIT_CHACHA = 2,
	FS_RESULT_SERVED = 0,
	FS_RESULT_UNKNOWN = 1,
	FS_RESULT_NO_SUITE = 2,
	FS_RESULT_BUSY = 3,
	FS_FRAME_WANT = 1,
	FS_FRAME_DATA = 2,
	FS_FRAME_END = 3,
	FS_FRAME_READY = 4,
	FS_FRAME_KEEP_ALIVE = 5,
	/* (a Custom Edition map's: map_share.c's pieces, BLAKE2b's) */
	FS_PIECE_BYTES = 256 * 1024,
	/* (sixteen connections a transfer, four on the Switch, whose socket
	 * service has little memory; four pieces each in flight) */
#if defined(__SWITCH__)
	FS_PIECE_CONNECTIONS = 4,
#else
	FS_PIECE_CONNECTIONS = 16,
#endif
	FS_PIECE_SLOTS = 64,
	/* (an MCC package's files: in ranges of 1 MB, a few more held than
	 * connections, for the order they are written in) */
	FS_FETCH_RANGE = 1 << 20,
	FS_FETCH_RANGES = FS_PIECE_CONNECTIONS + 4,
	FS_KEEP_ALIVE_MILLISECONDS = 10000,
};

static unsigned char const fs_magic[4] = { 'S', 'L', 'F', 'S' };

/* (Winsock's, which posix_socket_setsockopt and _getsockopt take) */
#define FS_SOL_SOCKET 0xffff
#define FS_SO_SNDBUF 0x1001
#define FS_SO_RCVBUF 0x1002
#define FS_SO_ERROR 0x1007
#define FS_SO_REUSEADDR 0x0004

struct fs_sockaddr {
	uint16_t family;
	uint16_t port;
	uint32_t ip;
	unsigned char zero[8];
};

static unsigned long fs_milliseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)now.tv_sec * 1000ul + (unsigned long)(now.tv_nsec / 1000000);
}

static void fs_sleep(unsigned long milliseconds)
{
	struct timespec time;

	time.tv_sec = (time_t)(milliseconds / 1000);
	time.tv_nsec = (long)(milliseconds % 1000) * 1000000l;
	nanosleep(&time, NULL);
}

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static uint32_t get32(unsigned char const *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put16(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

static unsigned get16(unsigned char const *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8;
}

/* ---------- sockets */

/* 1 ready, 0 not within the time, -1 an error */
static int fs_wait(int socket, unsigned long milliseconds, int writing)
{
	int list[1], count = 1, none = 0;

	list[0] = socket;
	if (posix_socket_select(writing ? NULL : list, writing ? &none : &count, writing ? list : NULL,
		writing ? &count : &none, NULL, &none, (posix_long)(milliseconds / 1000),
		(posix_long)(milliseconds % 1000) * 1000, 0) < 0)
		return -1;
	return count > 0;
}

static void fs_socket_tune(int socket)
{
	int size;

	/* (the largest the system takes: a BSD one, the PS5's, refuses a size past
	 * its limit outright and keeps its small default, far worse than asking
	 * less) */
	for (size = FS_SOCKET_BUFFER; size >= 256 * 1024; size /= 2)
		if (!posix_socket_setsockopt(socket, FS_SOL_SOCKET, FS_SO_SNDBUF, &size, sizeof(size)))
			break;
	for (size = FS_SOCKET_BUFFER; size >= 256 * 1024; size /= 2)
		if (!posix_socket_setsockopt(socket, FS_SOL_SOCKET, FS_SO_RCVBUF, &size, sizeof(size)))
			break;
	posix_socket_set_nodelay(socket);
#if (defined(__ANDROID__) || defined(__linux__)) && !defined(__SWITCH__)
	/* BBR where the kernel lets an app choose it (the S21 FE's, not the Z
	 * Fold's): it does not halve its window at each loss as CUBIC does, which
	 * over a long internet path holds a connection to a few MB/s (the sender's
	 * choice decides; the socket is the system's descriptor here) */
	setsockopt(socket, IPPROTO_TCP, TCP_CONGESTION, "bbr", 3);
#endif
}

/* all of it, or 0 (stopped, timed out, the connection gone) */
static int fs_send_all(int socket, void const *data, size_t size, volatile int const *stop)
{
	unsigned char const *at = data;
	unsigned long began = fs_milliseconds();

	while (size) {
		int ready = fs_wait(socket, 100, 1), sent;

		if (ready < 0 || (stop && *stop) || fs_milliseconds() - began > FS_IDLE_MILLISECONDS)
			return 0;
		if (!ready)
			continue;
		sent = posix_socket_send(socket, at, (int)(size > 0x100000 ? 0x100000 : size), 0);
		if (sent <= 0)
			return 0;
		at += sent;
		size -= (size_t)sent;
		began = fs_milliseconds();
	}
	return 1;
}

/* exactly size bytes within milliseconds of quiet, or 0 */
static int fs_receive_all(int socket, void *data, size_t size, unsigned long milliseconds, volatile int const *stop)
{
	unsigned char *at = data;
	unsigned long began = fs_milliseconds();

	while (size) {
		int ready = fs_wait(socket, 100, 0), got;

		if (ready < 0 || (stop && *stop) || fs_milliseconds() - began > milliseconds)
			return 0;
		if (!ready)
			continue;
		got = posix_socket_recv(socket, at, (int)size, 0);
		if (got <= 0)
			return 0;
		at += got;
		size -= (size_t)got;
		began = fs_milliseconds();
	}
	return 1;
}

static int fs_connect(uint32_t address, unsigned port, unsigned long milliseconds, volatile int const *stop)
{
	struct fs_sockaddr to;
	int socket = posix_socket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 0);
	unsigned long began = fs_milliseconds();

	if (socket < 0)
		return -1;
	memset(&to, 0, sizeof(to));
	to.family = 2;
	to.port = (uint16_t)(((port & 255) << 8) | ((port >> 8) & 255));
	to.ip = address;
	fs_socket_tune(socket);
	posix_socket_set_nonblocking(socket, 1);
	if (posix_socket_connect(socket, &to, sizeof(to)) != 0) {
		for (;;) {
			int ready = fs_wait(socket, 100, 1), error = 0, length = sizeof(error);

			if (ready > 0) {
				if (posix_socket_getsockopt(socket, FS_SOL_SOCKET, FS_SO_ERROR, &error, &length) != 0 || error)
					ready = -1;
				else
					break;
			}
			if (ready < 0 || (stop && *stop) || fs_milliseconds() - began > milliseconds) {
				posix_socket_close(socket);
				return -1;
			}
		}
	}
	return socket;
}

/* ---------- the keys and the records */

struct fs_channel {
	int socket;
	struct fs_aead send_aead, receive_aead;
	uint64_t sent, received;
	unsigned char *plain;   /* FS_RECORD */
	unsigned char *wire;    /* FS_HEADER + FS_RECORD + FS_TAG */
};

static int fs_channel_buffers(struct fs_channel *channel)
{
	channel->plain = malloc(FS_RECORD);
	channel->wire = malloc(FS_HEADER + FS_RECORD + FS_TAG);
	return channel->plain && channel->wire;
}

static void fs_channel_free(struct fs_channel *channel)
{
	if (channel->socket >= 0)
		posix_socket_close(channel->socket);
	channel->socket = -1;
	fs_aead_wipe(&channel->send_aead);
	fs_aead_wipe(&channel->receive_aead);
	free(channel->plain);
	free(channel->wire);
	channel->plain = channel->wire = NULL;
}

/* the two directions' keys, from the hello and the reply as sent */
static int fs_channel_keys(struct fs_channel *channel, int joiner, int suite, unsigned char const secret[32],
	unsigned char const peer_public[32], unsigned char const token[32], unsigned char const hello[FS_HELLO_SIZE],
	unsigned char const reply[FS_REPLY_SIZE])
{
	unsigned char shared[32], material[64], salt[64], prk[32], transcript[32], key_joiner[32], key_host[32];
	unsigned char both[FS_HELLO_SIZE + FS_REPLY_SIZE];
	static char const label_joiner[] = "slfs joiner key", label_host[] = "slfs host key";
	unsigned char labelled[sizeof(label_joiner) + 32];
	int good;

	if (!fs_x25519_shared(shared, secret, peer_public))
		return 0;
	memcpy(both, hello, FS_HELLO_SIZE);
	memcpy(both + FS_HELLO_SIZE, reply, FS_REPLY_SIZE);
	fs_sha256(both, sizeof(both), transcript);
	/* (the joiner's nonce, then the host's) */
	memcpy(salt, hello + 44, 32);
	memcpy(salt + 32, reply + 40, 32);
	memcpy(material, shared, 32);
	memcpy(material + 32, token, 32);
	fs_hmac_sha256(salt, sizeof(salt), material, sizeof(material), NULL, 0, prk);
	memcpy(labelled, label_joiner, sizeof(label_joiner) - 1);
	memcpy(labelled + sizeof(label_joiner) - 1, transcript, 32);
	fs_hmac_sha256(prk, 32, labelled, sizeof(label_joiner) - 1 + 32, NULL, 0, key_joiner);
	memcpy(labelled, label_host, sizeof(label_host) - 1);
	memcpy(labelled + sizeof(label_host) - 1, transcript, 32);
	fs_hmac_sha256(prk, 32, labelled, sizeof(label_host) - 1 + 32, NULL, 0, key_host);
	good = fs_aead_init(&channel->send_aead, suite, joiner ? key_joiner : key_host) &&
		fs_aead_init(&channel->receive_aead, suite, joiner ? key_host : key_joiner);
	fs_wipe(shared, sizeof(shared));
	fs_wipe(material, sizeof(material));
	fs_wipe(prk, sizeof(prk));
	fs_wipe(key_joiner, sizeof(key_joiner));
	fs_wipe(key_host, sizeof(key_host));
	channel->sent = channel->received = 0;
	return good;
}

static void fs_nonce(unsigned char nonce[12], uint64_t counter)
{
	int i;

	memset(nonce, 0, 4);
	for (i = 0; i < 8; i++)
		nonce[4 + i] = (unsigned char)(counter >> (8 * i));
}

/* the record of channel->plain's first size bytes */
static int fs_channel_send(struct fs_channel *channel, size_t size, volatile int const *stop)
{
	unsigned char nonce[12];

	put32(channel->wire, (uint32_t)(size + FS_TAG));
	fs_nonce(nonce, channel->sent++);
	fs_aead_seal(&channel->send_aead, nonce, channel->wire, FS_HEADER, channel->plain, size,
		channel->wire + FS_HEADER, channel->wire + FS_HEADER + size);
	return fs_send_all(channel->socket, channel->wire, FS_HEADER + size + FS_TAG, stop);
}

/* the next record into channel->plain: its size, or -1 (a wrong tag, too
 * large, the connection gone, quiet past milliseconds) */
static long fs_channel_receive(struct fs_channel *channel, unsigned long milliseconds, volatile int const *stop)
{
	unsigned char nonce[12];
	uint32_t length;

	if (!fs_receive_all(channel->socket, channel->wire, FS_HEADER, milliseconds, stop))
		return -1;
	length = get32(channel->wire);
	if (length < FS_TAG || length > FS_RECORD + FS_TAG ||
		!fs_receive_all(channel->socket, channel->wire + FS_HEADER, length, FS_IDLE_MILLISECONDS, stop))
		return -1;
	fs_nonce(nonce, channel->received++);
	if (!fs_aead_open(&channel->receive_aead, nonce, channel->wire, FS_HEADER, channel->wire + FS_HEADER,
		length - FS_TAG, channel->plain, channel->wire + FS_HEADER + length - FS_TAG))
		return -1;
	return (long)(length - FS_TAG);
}

static int fs_suites_here(void)
{
	return (fs_crypto_aes_fast() ? FS_SUITE_BIT_AES : 0) | FS_SUITE_BIT_CHACHA;
}

/* ---------- the host's rate (docs/file_share_tcp_plan.md, stage 2)

Outside a game the file connections send as fast as the line takes, up to the
guest's limit (the upload limit: 250 MB/s at most). In a game they leave the
game its traffic and 5% of the line: 0.95 * C - G, where G is the game's own
bytes a second (its datagrams, counted by the guest: xnet.c) and C the line's
top speed:

- measured outside a game: the bytes the connections sent in each half second
  in which the socket, not the pacing, held them back for a tenth of it or
  more (the line was full); C takes the highest of the last ten seconds;
- in a game, optimistic: not known yet, C is the whole limit (as fast as the
  line takes); a full socket only raises it (a joiner that stalls, or TCP
  ramping up over a long path, backs the socket up too); the players' pings
  alone lower it (the host's round trips to its clients, which the guest
  gives, averaged over each half second): 30 ms (or 15% of a long one) more
  than the least of the last minute for a second running while files go out,
  and C is a tenth below the most that went out in the last two seconds;
  otherwise it goes up 10% a half second while the allowance is used. With no player's ping known (nobody
  playing), nothing but the game's traffic holds it back. */

enum {
	FS_RATE_BUCKET_MILLISECONDS = 500,
	FS_RATE_BUCKETS = 20,
	FS_RATE_DELAY_MILLISECONDS = 30,
	FS_RATE_BASE_MILLISECONDS = 60000,
	FS_RATE_LOG_MILLISECONDS = 5000,
	FS_RATE_FLOOR = 256 * 1024,
	FS_RATE_TRICKLE = 16 * 1024,
};

struct fs_rate {
	/* what the guest says each frame: the cap (bytes a second, 0 none), a
	 * game on, the game's bytes a second, its players' round trip (ms, 0 none) */
	double cap;
	int in_game;
	double game;
	unsigned round_trip;
	/* the half second going on: its start, the bytes sent, how long sends
	 * waited on the socket */
	unsigned long bucket_at;
	double bucket_bytes;
	unsigned long bucket_waited;
	/* the last ten seconds' half seconds: their rates, and whether the line held
	 * them back */
	double rates[FS_RATE_BUCKETS];
	int limited[FS_RATE_BUCKETS];
	int next;
	/* C (0: not known), whether a full line ever measured it, the least round
	 * trip lately and since when, and the allowance now */
	double capacity;
	int measured;
	unsigned base_round_trip;
	unsigned long base_at;
	double budget;
	/* the last half second's rate, for the log */
	double last_rate;
	/* the players' round trips given in the half second going on (their
	 * average is the one compared), and how many half seconds in a row it
	 * was too far above the least */
	double round_trip_sum;
	unsigned round_trip_samples;
	int delayed_buckets;
};

/* (lock held) the guest's facts, a frame's */
static void fs_rate_facts(struct fs_rate *rate, int in_game, double game, unsigned round_trip)
{
	rate->in_game = in_game != 0;
	rate->game = game;
	rate->round_trip = round_trip;
	if (round_trip) {
		rate->round_trip_sum += round_trip;
		rate->round_trip_samples++;
	}
}

static double fs_rate_measured(struct fs_rate const *rate)
{
	double best = 0.0;
	int i;

	for (i = 0; i < FS_RATE_BUCKETS; i++)
		if (rate->limited[i] && rate->rates[i] > best)
			best = rate->rates[i];
	return best;
}

/* C for the allowance to be the whole limit (none: as much as the line takes) */
static double fs_rate_full(struct fs_rate const *rate)
{
	return rate->cap > 0.0 ? rate->cap / 0.95 + rate->game : 1e12;
}

static double fs_rate_allowance(struct fs_rate const *rate)
{
	double allowed;

	if (!rate->in_game)
		return rate->cap;
	allowed = 0.95 * rate->capacity - rate->game;
	/* (a trickle when the game takes it all: the connection paused, not
	 * timed out) */
	if (allowed < FS_RATE_TRICKLE)
		allowed = FS_RATE_TRICKLE;
	if (rate->cap > 0.0 && allowed > rate->cap)
		allowed = rate->cap;
	return allowed;
}

/* (lock held) a send of bytes that waited this long on the socket */
static void fs_rate_sent(struct fs_rate *rate, size_t bytes, unsigned long waited)
{
	rate->bucket_bytes += (double)bytes;
	rate->bucket_waited += waited;
}

/* (lock held) the facts brought up to now: a half second closed, C and the
 * allowance worked out again */
static void fs_rate_tick(struct fs_rate *rate, unsigned long now)
{
	unsigned long length = now - rate->bucket_at;
	double bucket_rate, measured, recent = 0.0;
	int limited, sending, delayed, i;
	unsigned round_trip = 0;

	if (!rate->bucket_at || length > 10 * FS_RATE_BUCKET_MILLISECONDS) {
		rate->bucket_at = now;
		rate->bucket_bytes = 0.0;
		rate->bucket_waited = 0;
		if (rate->in_game && !rate->capacity)
			rate->capacity = fs_rate_full(rate);
		rate->budget = fs_rate_allowance(rate);
		return;
	}
	if (length < FS_RATE_BUCKET_MILLISECONDS) {
		if (rate->in_game && !rate->capacity)
			rate->capacity = fs_rate_full(rate);
		rate->budget = fs_rate_allowance(rate);
		return;
	}
	/* (the half second's round trip, its samples' average: one reading of
	 * an internet ping jitters by tens of milliseconds) */
	if (rate->round_trip_samples)
		round_trip = (unsigned)(rate->round_trip_sum / rate->round_trip_samples + 0.5);
	rate->round_trip_sum = 0.0;
	rate->round_trip_samples = 0;
	if (round_trip && (!rate->base_round_trip || round_trip < rate->base_round_trip ||
		now - rate->base_at > FS_RATE_BASE_MILLISECONDS)) {
		rate->base_round_trip = round_trip;
		rate->base_at = now;
	}
	bucket_rate = rate->bucket_bytes * 1000.0 / (double)length;
	limited = rate->bucket_waited * 10 >= length && rate->bucket_bytes > 0.0;
	sending = rate->bucket_bytes > 0.0;
	rate->rates[rate->next] = bucket_rate;
	rate->limited[rate->next] = limited;
	rate->next = (rate->next + 1) % FS_RATE_BUCKETS;
	rate->last_rate = bucket_rate;
	rate->bucket_at = now;
	rate->bucket_bytes = 0.0;
	rate->bucket_waited = 0;
	measured = fs_rate_measured(rate);
	if (measured > 0.0)
		rate->measured = 1;
	/* (too far above the least: 30 ms, or 15% of a long one, for a second
	 * running) */
	{
		unsigned margin = rate->base_round_trip * 15 / 100;

		if (margin < FS_RATE_DELAY_MILLISECONDS)
			margin = FS_RATE_DELAY_MILLISECONDS;
		if (round_trip && rate->base_round_trip && round_trip > rate->base_round_trip + margin)
			rate->delayed_buckets++;
		else
			rate->delayed_buckets = 0;
	}
	delayed = rate->delayed_buckets >= 2;
	/* (the most that went out in the last two seconds) */
	for (i = 1; i <= 4; i++) {
		double past = rate->rates[(rate->next - i + FS_RATE_BUCKETS) % FS_RATE_BUCKETS];

		if (past > recent)
			recent = past;
	}
	if (!rate->in_game) {
		/* (the line measured while nothing holds the sending back) */
		if (measured > 0.0)
			rate->capacity = measured;
	} else {
		/* (the line not known: as fast as the limit lets, until the pings say
		otherwise) */
		if (!rate->capacity)
			rate->capacity = fs_rate_full(rate);
		/* (a full socket raises C, never lowers it: a joiner that stalls a
		moment, or TCP still ramping up over a long path, backs the socket up
		too, and said 0.3 MB/s of a line that carried 2.5; the pings alone lower
		it) */
		if (limited && bucket_rate + rate->game > rate->capacity)
			rate->capacity = bucket_rate + rate->game;
		/* (the players' pings up for a second: a tenth below the most that
		went out in the last two, not below the estimate, which may be far
		above it, nor below one slow half second; another second before the
		next cut) */
		if (sending && delayed) {
			double going = 0.9 * (recent + rate->game) / 0.95;

			if (going < rate->capacity)
				rate->capacity = going;
			rate->delayed_buckets = 0;
		} else if (sending && bucket_rate >= 0.8 * rate->budget)
			rate->capacity *= 1.1;
		if (rate->capacity < FS_RATE_FLOOR + rate->game)
			rate->capacity = FS_RATE_FLOOR + rate->game;
		if (rate->cap > 0.0 && rate->capacity > rate->cap / 0.95 + rate->game)
			rate->capacity = rate->cap / 0.95 + rate->game;
	}
	rate->budget = fs_rate_allowance(rate);
}

/* ---------- the host: what it serves */

struct fs_serve_file {
	char path[FS_PATH];
	uint32_t size;
};

static struct {
	pthread_mutex_t lock;
	/* the listener, and whether the game hosts (the guest says each frame) */
	int listener;
	int hosting;
	unsigned long listen_failed_at;
	int listen_thread_running;
	volatile int stop_listening;
	/* the upload's cap (bytes a second), the allowance worked out from it
	 * (fs_rate), and the bucket every connection sends from */
	uint32_t limit;
	struct fs_rate rate;
	unsigned long rate_logged_at;
	double tokens;
	unsigned long tokens_at;
	int connections;
	struct {
		int used, ready, ending;
		uint32_t session;
		unsigned char token[32];
		int count;
		struct fs_serve_file *files;
		uint64_t sent;
		int files_done;
		int users;
		/* (for the log: when its first connection of these opened, and what
		 * they sent) */
		unsigned long users_since;
		uint64_t users_sent;
		int users_most;
		int finished;
	} serves[FS_SERVES];
} host = { .lock = PTHREAD_MUTEX_INITIALIZER, .listener = -1 };

/* (lock held) */
static int serve_index(uint32_t session)
{
	int i;

	for (i = 0; i < FS_SERVES; i++)
		if (host.serves[i].used && !host.serves[i].ending && host.serves[i].session == session)
			return i;
	return -1;
}

/* (lock held) a serve let go once nothing uses it */
static void serve_release(int index)
{
	if (host.serves[index].users || !host.serves[index].ending)
		return;
	free(host.serves[index].files);
	fs_wipe(host.serves[index].token, 32);
	memset(&host.serves[index], 0, sizeof(host.serves[index]));
}

/* waits until bytes may go at the allowance (fs_rate: shared by the
connections); 0 when stopped */
static int host_take_tokens(size_t bytes, volatile int const *stop)
{
	for (;;) {
		double burst, allowance;
		unsigned long now;

		pthread_mutex_lock(&host.lock);
		now = fs_milliseconds();
		allowance = host.rate.budget;
		burst = allowance / 4.0 > 2.0 * FS_RECORD ? allowance / 4.0 : 2.0 * FS_RECORD;
		host.tokens += (double)(now - host.tokens_at < 1000 ? now - host.tokens_at : 1000) * allowance / 1000.0;
		host.tokens_at = now;
		if (host.tokens > burst)
			host.tokens = burst;
		/* (no cap and no game: whatever the line takes) */
		if (host.tokens >= (double)bytes || (!host.limit && !host.rate.in_game)) {
			host.tokens -= (double)bytes;
			pthread_mutex_unlock(&host.lock);
			return 1;
		}
		pthread_mutex_unlock(&host.lock);
		if (stop && *stop)
			return 0;
		fs_sleep(1);
	}
}

struct host_want {
	unsigned file;
	uint32_t offset, length;
};

/* one joiner's connection */
static void *host_connection_main(void *parameter)
{
	struct fs_channel channel;
	unsigned char hello[FS_HELLO_SIZE], reply[FS_REPLY_SIZE], secret[32], token[32];
	struct host_want *wants = NULL;
	int want_count = 0, want_at = 0, serve = -1, suite = 0, ended = 0;
	FILE *file = NULL;
	unsigned open_file = (unsigned)-1;
	uint32_t file_offset = 0;
	volatile int const *stop = &host.stop_listening;
	unsigned long quiet_since = fs_milliseconds(), began = fs_milliseconds();
	uint64_t sent_here = 0;

#if defined(__SWITCH__)
	host_thread_place_on_helper_core();
#endif
	memset(&channel, 0, sizeof(channel));
	channel.socket = (int)(intptr_t)parameter;
	fs_socket_tune(channel.socket);
	posix_socket_set_nonblocking(channel.socket, 1);
	wants = malloc(sizeof(*wants) * FS_FILES * 2);
	if (!wants || !fs_channel_buffers(&channel) ||
		!fs_receive_all(channel.socket, hello, sizeof(hello), FS_HELLO_MILLISECONDS, stop) ||
		memcmp(hello, fs_magic, 4) || hello[4] != FS_VERSION)
		goto done;
	memcpy(reply, fs_magic, 4);
	reply[4] = FS_VERSION;
	reply[5] = 0;
	reply[6] = FS_RESULT_UNKNOWN;
	reply[7] = 0;
	pthread_mutex_lock(&host.lock);
	serve = serve_index(get32(hello + 8));
	if (serve >= 0 && host.serves[serve].ready && host.serves[serve].users >= FS_SESSION_CONNECTIONS) {
		serve = -1;
		reply[6] = FS_RESULT_BUSY;
	} else if (serve >= 0 && host.serves[serve].ready) {
		host.serves[serve].users++;
		memcpy(token, host.serves[serve].token, 32);
		reply[6] = FS_RESULT_SERVED;
	} else
		serve = -1;
	pthread_mutex_unlock(&host.lock);
	if (serve >= 0) {
		int theirs = hello[5], ours = fs_suites_here();

		suite = (theirs & ours & FS_SUITE_BIT_AES) ? FS_SUITE_AES256_GCM :
			(theirs & ours & FS_SUITE_BIT_CHACHA) ? FS_SUITE_CHACHA20_POLY1305 : 0;
		if (!suite)
			reply[6] = FS_RESULT_NO_SUITE;
		reply[5] = (unsigned char)suite;
	}
	fs_x25519_keypair(secret, reply + 8);
	fs_random(reply + 40, 32);
	if (!fs_send_all(channel.socket, reply, sizeof(reply), stop) || reply[6] != FS_RESULT_SERVED ||
		!fs_channel_keys(&channel, 0, suite, secret, hello + 12, token, hello, reply))
		goto done;
	fs_wipe(secret, sizeof(secret));
	fs_wipe(token, sizeof(token));
	channel.plain[0] = FS_FRAME_READY;
	channel.plain[1] = channel.plain[2] = channel.plain[3] = 0;
	if (!fs_channel_send(&channel, 4, stop))
		goto done;
	{
		int first;

		pthread_mutex_lock(&host.lock);
		first = host.serves[serve].users == 1;
		if (first) {
			host.serves[serve].users_since = fs_milliseconds();
			host.serves[serve].users_sent = 0;
			host.serves[serve].users_most = 0;
		}
		if (host.serves[serve].users > host.serves[serve].users_most)
			host.serves[serve].users_most = host.serves[serve].users;
		pthread_mutex_unlock(&host.lock);
		if (first)
			fs_log("file share: serving session %u (%s)", (unsigned)get32(hello + 8),
				suite == FS_SUITE_AES256_GCM ? fs_crypto_aes_kind() : "ChaCha20-Poly1305");
	}
	while (!*stop && !ended) {
		/* (what the joiner says: its wants, or its end) */
		int ready = fs_wait(channel.socket, want_at < want_count ? 0 : 100, 0);

		if (ready < 0)
			break;
		if (ready > 0) {
			long size = fs_channel_receive(&channel, FS_IDLE_MILLISECONDS, stop), at = 0;

			if (size < 0)
				break;
			quiet_since = fs_milliseconds();
			while (at + 4 <= size) {
				unsigned char const *frame = channel.plain + at;

				if (frame[0] == FS_FRAME_END) {
					ended = 1;
					break;
				}
				if (frame[0] == FS_FRAME_KEEP_ALIVE) {
					at += 4;
					continue;
				}
				if (frame[0] != FS_FRAME_WANT || at + FS_WANT_SIZE > size)
					goto done;
				/* (the wants served let go: a swarm's source is asked piece after
				piece for as long as the download lasts) */
				if (want_count >= FS_FILES * 2 && want_at) {
					memmove(wants, wants + want_at, sizeof(*wants) * (size_t)(want_count - want_at));
					want_count -= want_at;
					want_at = 0;
				}
				if (want_count >= FS_FILES * 2)
					goto done;
				wants[want_count].file = get16(frame + 2);
				wants[want_count].offset = get32(frame + 4);
				wants[want_count].length = get32(frame + 8);
				want_count++;
				at += FS_WANT_SIZE;
			}
		}
		if (want_at < want_count) {
			struct host_want *want = &wants[want_at];
			char path[FS_PATH];
			uint32_t size = 0, chunk;
			int valid;

			pthread_mutex_lock(&host.lock);
			valid = !host.serves[serve].ending && want->file < (unsigned)host.serves[serve].count;
			if (valid) {
				size = host.serves[serve].files[want->file].size;
				memcpy(path, host.serves[serve].files[want->file].path, FS_PATH);
			}
			pthread_mutex_unlock(&host.lock);
			if (!valid || want->offset > size || want->length > size - want->offset)
				break;
			if (want->length == 0) {
				want_at++;
				pthread_mutex_lock(&host.lock);
				if (!want->offset || want->offset == size)
					host.serves[serve].files_done++;
				pthread_mutex_unlock(&host.lock);
				continue;
			}
			if (open_file != want->file || file_offset != want->offset) {
				if (file)
					fclose(file);
				file = fopen(path, "rb");
				if (!file || fseek(file, (long)want->offset, SEEK_SET)) {
					fs_log("file share: cannot read %s", path);
					goto done;
				}
				setvbuf(file, NULL, _IOFBF, FS_FILE_BUFFER);
				open_file = want->file;
				file_offset = want->offset;
			}
			chunk = want->length < FS_RECORD - FS_DATA_HEADER ? want->length : FS_RECORD - FS_DATA_HEADER;
			if (!host_take_tokens(FS_HEADER + FS_DATA_HEADER + chunk + FS_TAG, stop))
				break;
			channel.plain[0] = FS_FRAME_DATA;
			channel.plain[1] = 0;
			put16(channel.plain + 2, want->file);
			put32(channel.plain + 4, want->offset);
			if (fread(channel.plain + FS_DATA_HEADER, 1, chunk, file) != chunk) {
				fs_log("file share: %s ended early", path);
				goto done;
			}
			{
				unsigned long sending_at = fs_milliseconds();

				if (!fs_channel_send(&channel, FS_DATA_HEADER + chunk, stop))
					break;
				pthread_mutex_lock(&host.lock);
				fs_rate_sent(&host.rate, FS_HEADER + FS_DATA_HEADER + chunk + FS_TAG, fs_milliseconds() - sending_at);
				pthread_mutex_unlock(&host.lock);
			}
			want->offset += chunk;
			want->length -= chunk;
			file_offset += chunk;
			sent_here += chunk;
			quiet_since = fs_milliseconds();
			pthread_mutex_lock(&host.lock);
			host.serves[serve].sent += chunk;
			if (!want->length) {
				if (want->offset == size)
					host.serves[serve].files_done++;
				want_at++;
			}
			pthread_mutex_unlock(&host.lock);
		} else if (fs_milliseconds() - quiet_since > FS_IDLE_MILLISECONDS)
			break;
	}
	if (ended && serve >= 0) {
		pthread_mutex_lock(&host.lock);
		host.serves[serve].finished = 1;
		pthread_mutex_unlock(&host.lock);
	}
done:
	(void)began;
	if (file)
		fclose(file);
	free(wants);
	fs_wipe(secret, sizeof(secret));
	fs_wipe(token, sizeof(token));
	fs_channel_free(&channel);
	pthread_mutex_lock(&host.lock);
	if (serve >= 0) {
		host.serves[serve].users_sent += sent_here;
		host.serves[serve].users--;
		/* (the transfer's last connection: what they all sent) */
		if (!host.serves[serve].users && host.serves[serve].users_sent) {
			unsigned long elapsed = fs_milliseconds() - host.serves[serve].users_since;

			fs_log("file share: sent %.1f MB in %.1f s (%.1f MB/s) over %d connections",
				host.serves[serve].users_sent / 1048576.0, elapsed / 1000.0,
				elapsed ? host.serves[serve].users_sent / 1048576.0 / (elapsed / 1000.0) : 0.0,
				host.serves[serve].users_most);
		}
		serve_release(serve);
	}
	host.connections--;
	pthread_mutex_unlock(&host.lock);
	return NULL;
}

static void *host_listen_main(void *parameter)
{
	int listener = (int)(intptr_t)parameter;

#if defined(__SWITCH__)
	host_thread_place_on_helper_core();
#endif
	while (!host.stop_listening) {
		struct fs_sockaddr from;
		int length = sizeof(from), socket, ready = fs_wait(listener, 200, 0);
		pthread_t thread;
		pthread_attr_t attributes;

		if (ready <= 0)
			continue;
		socket = posix_socket_accept(listener, &from, &length);
		if (socket < 0)
			continue;
		pthread_mutex_lock(&host.lock);
		if (host.connections >= FS_HOST_CONNECTIONS) {
			pthread_mutex_unlock(&host.lock);
			posix_socket_close(socket);
			continue;
		}
		host.connections++;
		pthread_mutex_unlock(&host.lock);
		pthread_attr_init(&attributes);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&thread, &attributes, host_connection_main, (void *)(intptr_t)socket) != 0) {
			posix_socket_close(socket);
			pthread_mutex_lock(&host.lock);
			host.connections--;
			pthread_mutex_unlock(&host.lock);
		}
		pthread_attr_destroy(&attributes);
	}
	posix_socket_close(listener);
	pthread_mutex_lock(&host.lock);
	host.listen_thread_running = 0;
	pthread_mutex_unlock(&host.lock);
	return NULL;
}

/* (the guest's game thread) */
static void host_listen(void)
{
	struct fs_sockaddr address;
	int listener, one = 1;
	pthread_t thread;
	pthread_attr_t attributes;

	if (host.listen_failed_at && fs_milliseconds() - host.listen_failed_at < FS_LISTEN_RETRY_MILLISECONDS)
		return;
	listener = posix_socket(2, 1, 0);
	memset(&address, 0, sizeof(address));
	address.family = 2;
	address.port = (uint16_t)(((FS_PORT & 255) << 8) | (FS_PORT >> 8));
	if (listener >= 0)
		posix_socket_setsockopt(listener, FS_SOL_SOCKET, FS_SO_REUSEADDR, &one, sizeof(one));
	if (listener < 0 || posix_socket_bind(listener, &address, sizeof(address)) != 0 ||
		posix_socket_listen(listener, 8) != 0) {
		if (listener >= 0)
			posix_socket_close(listener);
		host.listen_failed_at = fs_milliseconds();
		fs_log("file share: cannot listen on port %d (error %d)", FS_PORT, posix_socket_last_error());
		return;
	}
	posix_socket_set_nonblocking(listener, 1);
	host.stop_listening = 0;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_mutex_lock(&host.lock);
	host.listen_thread_running = 1;
	pthread_mutex_unlock(&host.lock);
	if (pthread_create(&thread, &attributes, host_listen_main, (void *)(intptr_t)listener) != 0) {
		posix_socket_close(listener);
		pthread_mutex_lock(&host.lock);
		host.listen_thread_running = 0;
		pthread_mutex_unlock(&host.lock);
		host.listen_failed_at = fs_milliseconds();
	} else {
		host.listener = listener;
		host.listen_failed_at = 0;
		fs_log("file share: listening on port %d (AES: %s)", FS_PORT, fs_crypto_aes_kind());
	}
	pthread_attr_destroy(&attributes);
}

int host_fs_host_frame(int hosting, unsigned int limit)
{
	int i, listening;

	pthread_mutex_lock(&host.lock);
	if (host.limit != limit) {
		host.limit = limit;
		host.tokens_at = fs_milliseconds();
	}
	host.rate.cap = (double)limit;
	fs_rate_tick(&host.rate, fs_milliseconds());
	if (host.connections && fs_milliseconds() - host.rate_logged_at > FS_RATE_LOG_MILLISECONDS) {
		host.rate_logged_at = fs_milliseconds();
		fs_log("file share: rate %.1f MB/s of %.1f allowed%s; the line %.1f MB/s%s, the game %.0f KB/s, ping %u ms "
			"(least %u)", host.rate.last_rate / 1048576.0, host.rate.budget / 1048576.0,
			host.rate.in_game ? " (a game on)" : "", host.rate.capacity / 1048576.0,
			host.rate.measured ? "" : " (not measured yet)", host.rate.game / 1024.0, host.rate.round_trip,
			host.rate.base_round_trip);
	}
	listening = host.listen_thread_running;
	if (!hosting) {
		for (i = 0; i < FS_SERVES; i++)
			if (host.serves[i].used && !host.serves[i].ending) {
				host.serves[i].ending = 1;
				serve_release(i);
			}
	}
	pthread_mutex_unlock(&host.lock);
	if (hosting && !listening && host.listener < 0)
		host_listen();
	else if (!hosting && host.listener >= 0) {
		/* (the listening thread closes it) */
		host.stop_listening = 1;
		host.listener = -1;
	}
	host.hosting = hosting;
	return host.listener >= 0;
}

void host_fs_rate_facts(int in_game, unsigned int game_bytes, unsigned int round_trip)
{
	pthread_mutex_lock(&host.lock);
	fs_rate_facts(&host.rate, in_game, (double)game_bytes, round_trip);
	pthread_mutex_unlock(&host.lock);
}

unsigned int host_fs_allowance(void)
{
	double allowance;

	pthread_mutex_lock(&host.lock);
	allowance = host.rate.budget;
	pthread_mutex_unlock(&host.lock);
	return allowance > 4294967295.0 ? 4294967295u : (unsigned int)allowance;
}

int host_fs_serve_begin(unsigned int session, unsigned int token_address, int count)
{
	int i, slot = -1;

	if (count < 1 || count > FS_FILES)
		return -1;
	pthread_mutex_lock(&host.lock);
	for (i = 0; i < FS_SERVES && slot < 0; i++)
		if (host.serves[i].used && !host.serves[i].ending && host.serves[i].session == session)
			slot = i;
	for (i = 0; i < FS_SERVES && slot < 0; i++)
		if (!host.serves[i].used)
			slot = i;
	if (slot >= 0) {
		struct fs_serve_file *files = calloc((size_t)count, sizeof(*files));

		if (!files || (host.serves[slot].used && host.serves[slot].users)) {
			free(files);
			slot = -1;
		} else {
			free(host.serves[slot].files);
			memset(&host.serves[slot], 0, sizeof(host.serves[slot]));
			host.serves[slot].used = 1;
			host.serves[slot].session = session;
			host.serves[slot].count = count;
			host.serves[slot].files = files;
			memcpy(host.serves[slot].token, GUEST_BYTES(token_address), 32);
		}
	}
	pthread_mutex_unlock(&host.lock);
	return slot;
}

static int serve_file_set(uint32_t session, int index, char const *path, uint32_t size)
{
	int slot, good = 0;

	pthread_mutex_lock(&host.lock);
	slot = serve_index(session);
	if (slot >= 0 && index >= 0 && index < host.serves[slot].count && !host.serves[slot].ready &&
		strlen(path) < FS_PATH) {
		strcpy(host.serves[slot].files[index].path, path);
		host.serves[slot].files[index].size = size;
		good = 1;
	}
	pthread_mutex_unlock(&host.lock);
	return good;
}

int host_fs_serve_file(unsigned int session, int index, unsigned int path_address, unsigned int size)
{
	return serve_file_set(session, index, GUEST_TEXT(path_address), size);
}

int host_fs_serve_ready(unsigned int session)
{
	int slot;

	pthread_mutex_lock(&host.lock);
	slot = serve_index(session);
	if (slot >= 0)
		host.serves[slot].ready = 1;
	pthread_mutex_unlock(&host.lock);
	return slot >= 0;
}

static int serve_progress(uint32_t session, uint32_t out[4])
{
	int slot;

	pthread_mutex_lock(&host.lock);
	slot = serve_index(session);
	if (slot >= 0) {
		out[0] = (uint32_t)host.serves[slot].sent;
		out[1] = (uint32_t)(host.serves[slot].sent >> 32);
		out[2] = (uint32_t)host.serves[slot].files_done;
		out[3] = (uint32_t)(host.serves[slot].users | host.serves[slot].finished << 16);
	}
	pthread_mutex_unlock(&host.lock);
	return slot >= 0;
}

int host_fs_serve_progress(unsigned int session, unsigned int out_address)
{
	uint32_t out[4];

	if (!serve_progress(session, out))
		return 0;
	memcpy(GUEST_BYTES(out_address), out, sizeof(out));
	return 1;
}

void host_fs_serve_end(unsigned int session)
{
	int slot;

	pthread_mutex_lock(&host.lock);
	slot = serve_index(session);
	if (slot >= 0) {
		host.serves[slot].ending = 1;
		serve_release(slot);
	}
	pthread_mutex_unlock(&host.lock);
}

/* ---------- the joiner */

struct fs_fetch_file {
	unsigned file;
	char path[FS_PATH];
	uint32_t size;
	unsigned char sha256[32];
};

/* An MCC package's files over several connections at once (FS_PIECE_CONNECTIONS,
as a map's pieces): each file in FS_FETCH_RANGE ranges, each connection asking
one at a time; the ranges that came are put in order in memory (at most
FS_FETCH_RANGES of them) and written and hashed in order, so that a file has
one writer and its SHA-256 is worked out as it comes. A connection that drops
gives its range back to the others. */
static struct {
	pthread_mutex_t lock;
	int running;
	pthread_t threads[FS_PIECE_CONNECTIONS];
	int started[FS_PIECE_CONNECTIONS];
	volatile int stop;
	/* (a write or a hash that failed: every connection stops) */
	volatile int abort;
	uint32_t address;
	unsigned port;
	uint32_t direct_address;
	unsigned direct_port;
	uint32_t session;
	unsigned char token[32];
	int count, given;
	struct fs_fetch_file *files;
	/* the connections wanted, open now, and the first's READY (the others
	 * wait for it) or failure */
	int connections;
	int open;
	int ended;
	int first_ready, first_failed;
	/* the next range to hand out (the file's place in the list, its offset) */
	int next_index;
	uint32_t next_offset;
	/* the ranges handed out: free, wanted (by connection conn, or by none:
	 * given back), done (come whole, to write) */
	struct {
		int state;
		int conn;
		int index;
		uint32_t offset, length, received;
		unsigned char *buffer;
	} ranges[FS_FETCH_RANGES];
	/* the writer: the file it writes (the list's place), how far, the file
	 * open, its hash, and whether a connection is writing now */
	int write_index;
	uint32_t write_offset;
	FILE *file;
	struct host_sha256 sha;
	int writing;
	int wrote_any;
	/* what the guest reads (host_fs_fetch_status) */
	int state;
	int files_done;
	uint32_t current_bytes;
	uint64_t received;
	char why[160];
	unsigned long began;
} fetch = { .lock = PTHREAD_MUTEX_INITIALIZER };

enum {
	RANGE_FREE,
	RANGE_WANTED,
	RANGE_DONE,
};

/* (the joiner) the host's internet address and port the router forwards to its
file connection, for the next connection to try first (host_fs_direct) */
static struct {
	uint32_t address;
	unsigned port;
} fs_direct;

void host_fs_direct(unsigned int address, int port)
{
	fs_direct.address = address;
	fs_direct.port = port > 0 && port < 65536 ? (unsigned)port : 0;
}

/* one try of the joiner's side of the handshake, at one address, up to the
host's READY: the suite, or 0 (why says) */
static int joiner_attempt(struct fs_channel *channel, uint32_t address, unsigned port, unsigned long connect_milliseconds,
	unsigned long hello_milliseconds, uint32_t session, unsigned char const token[32], unsigned char const *wants,
	size_t wants_size, volatile int const *stop, char const **why, int quiet)
{
	unsigned char hello[FS_HELLO_SIZE], reply[FS_REPLY_SIZE], secret[32];
	int suite = 0;

	*why = "the host could not be reached";
	channel->socket = fs_connect(address, port, connect_milliseconds, stop);
	if (channel->socket < 0)
		return 0;
	memcpy(hello, fs_magic, 4);
	hello[4] = FS_VERSION;
	hello[5] = (unsigned char)fs_suites_here();
	hello[6] = hello[7] = 0;
	put32(hello + 8, session);
	fs_x25519_keypair(secret, hello + 12);
	fs_random(hello + 44, 32);
	*why = "the host did not answer";
	if (!fs_send_all(channel->socket, hello, sizeof(hello), stop) ||
		!fs_receive_all(channel->socket, reply, sizeof(reply), hello_milliseconds, stop))
		goto done;
	*why = "the host refused the connection";
	if (memcmp(reply, fs_magic, 4) || reply[4] != FS_VERSION || reply[6] != FS_RESULT_SERVED)
		goto done;
	*why = "the connection's keys could not be made";
	if ((reply[5] != FS_SUITE_AES256_GCM && reply[5] != FS_SUITE_CHACHA20_POLY1305) ||
		!fs_channel_keys(channel, 1, reply[5], secret, reply + 8, token, hello, reply))
		goto done;
	/* (what is wanted at once, before the READY) */
	if (wants_size) {
		memcpy(channel->plain, wants, wants_size);
		*why = "the connection was lost";
		if (!fs_channel_send(channel, wants_size, stop))
			goto done;
	}
	/* (the host's READY: its keys are the token's) */
	*why = "the host's connection did not prove it has the transfer's token";
	if (fs_channel_receive(channel, hello_milliseconds, stop) != 4 || channel->plain[0] != FS_FRAME_READY)
		goto done;
	suite = reply[5];
	if (!quiet)
		fs_log("file share: receiving session %u from the host (%s)", (unsigned)session,
			suite == FS_SUITE_AES256_GCM ? fs_crypto_aes_kind() : "ChaCha20-Poly1305");
done:
	fs_wipe(secret, sizeof(secret));
	if (!suite && channel->socket >= 0) {
		posix_socket_close(channel->socket);
		channel->socket = -1;
	}
	return suite;
}

/* the joiner's side of the handshake: the host's forwarded internet address
first, when it gave one (a direct connection, not through the internet play
tunnel), then the address the game reaches it at (its LAN address, or the
tunnel's local stand-in for it); the suite, or 0 (why says) */
static int joiner_open(struct fs_channel *channel, uint32_t direct_address, unsigned direct_port, uint32_t address,
	unsigned port, uint32_t session, unsigned char const token[32], unsigned char const *wants, size_t wants_size,
	volatile int const *stop, char const **why, int quiet)
{
	int suite;

	if (direct_address && direct_port) {
		suite = joiner_attempt(channel, direct_address, direct_port, FS_DIRECT_CONNECT_MILLISECONDS,
			FS_DIRECT_HELLO_MILLISECONDS, session, token, wants, wants_size, stop, why, quiet);
		if (suite) {
			if (!quiet)
				fs_log("file share: connected directly to the host's internet address");
			return suite;
		}
		if (stop && *stop)
			return 0;
		if (!quiet)
			fs_log("file share: no direct connection to the host (%s): the usual way", *why);
	}
	return joiner_attempt(channel, address, port, FS_CONNECT_MILLISECONDS, FS_HELLO_MILLISECONDS, session, token,
		wants, wants_size, stop, why, quiet);
}

/* (lock held) the writer's turn: the ranges that came, in order, written and
hashed (the lock let go while it writes: one connection writes at a time),
each file closed and checked as it ends; 0 when one failed (why says) */
static int fetch_write(char const **why)
{
	if (fetch.writing)
		return 1;
	fetch.writing = 1;
	while (fetch.write_index < fetch.count && !fetch.abort) {
		struct fs_fetch_file *wanted = &fetch.files[fetch.write_index];
		int found = -1, i;

		if (!fetch.file) {
			int descriptor = open(wanted->path, O_WRONLY | O_CREAT | O_EXCL, 0644);

			if (descriptor < 0 || !(fetch.file = fdopen(descriptor, "wb"))) {
				if (descriptor >= 0)
					close(descriptor);
				fs_log("file share: cannot create %s (errno %d)", wanted->path, errno);
				*why = "a downloaded file could not be written";
				goto failed;
			}
			fetch.wrote_any = 1;
			setvbuf(fetch.file, NULL, _IOFBF, FS_FILE_BUFFER);
			host_sha256_begin(&fetch.sha);
			fetch.write_offset = 0;
		}
		if (fetch.write_offset < wanted->size) {
			for (i = 0; i < FS_FETCH_RANGES && found < 0; i++)
				if (fetch.ranges[i].state == RANGE_DONE && fetch.ranges[i].index == fetch.write_index &&
					fetch.ranges[i].offset == fetch.write_offset)
					found = i;
			if (found < 0)
				break;
			pthread_mutex_unlock(&fetch.lock);
			if (fwrite(fetch.ranges[found].buffer, 1, fetch.ranges[found].length, fetch.file) !=
				fetch.ranges[found].length) {
				pthread_mutex_lock(&fetch.lock);
				*why = "a downloaded file could not be written";
				goto failed;
			}
			host_sha256_add(&fetch.sha, fetch.ranges[found].buffer, fetch.ranges[found].length);
			pthread_mutex_lock(&fetch.lock);
			fetch.write_offset += fetch.ranges[found].length;
			fetch.ranges[found].state = RANGE_FREE;
			fetch.current_bytes = fetch.write_offset;
		}
		if (fetch.write_offset == wanted->size) {
			unsigned char digest[32];
			int closed;

			pthread_mutex_unlock(&fetch.lock);
			closed = fclose(fetch.file);
			pthread_mutex_lock(&fetch.lock);
			fetch.file = NULL;
			host_sha256_end(&fetch.sha, digest);
			if (closed) {
				*why = "a downloaded file could not be written";
				goto failed;
			}
			if (!fs_equal(digest, wanted->sha256, 32)) {
				*why = "a file from the host did not match its SHA-256";
				goto failed;
			}
			fetch.write_index++;
			fetch.write_offset = 0;
			fetch.files_done = fetch.write_index;
			fetch.current_bytes = 0;
		}
	}
	if (fetch.write_index == fetch.count)
		fetch.state = HOST_FS_FETCH_DONE;
	fetch.writing = 0;
	return 1;
failed:
	fetch.writing = 0;
	fetch.abort = 1;
	fetch.state = HOST_FS_FETCH_FAILED_LATE;
	snprintf(fetch.why, sizeof(fetch.why), "%s", *why);
	return 0;
}

/* (lock held) a range for the connection: one given back, else the next of
the files (an empty file wanted with a length of 0: the host counts it as sent);
-1 for none now (all handed out, or the ranges held full), -2 for none left */
static int fetch_take_range(int conn, unsigned *empty_file)
{
	int i, slot = -1;

	*empty_file = (unsigned)-1;
	for (i = 0; i < FS_FETCH_RANGES; i++)
		if (fetch.ranges[i].state == RANGE_WANTED && fetch.ranges[i].conn < 0) {
			fetch.ranges[i].conn = conn;
			fetch.ranges[i].received = 0;
			return i;
		}
	while (fetch.next_index < fetch.count && !fetch.files[fetch.next_index].size) {
		*empty_file = fetch.files[fetch.next_index].file;
		fetch.next_index++;
		fetch.next_offset = 0;
		return -1;
	}
	if (fetch.next_index >= fetch.count)
		return -2;
	for (i = 0; i < FS_FETCH_RANGES && slot < 0; i++)
		if (fetch.ranges[i].state == RANGE_FREE)
			slot = i;
	if (slot < 0)
		return -1;
	if (!fetch.ranges[slot].buffer && !(fetch.ranges[slot].buffer = malloc(FS_FETCH_RANGE)))
		return -1;
	{
		uint32_t size = fetch.files[fetch.next_index].size;
		uint32_t length = size - fetch.next_offset < FS_FETCH_RANGE ? size - fetch.next_offset : FS_FETCH_RANGE;

		fetch.ranges[slot].state = RANGE_WANTED;
		fetch.ranges[slot].conn = conn;
		fetch.ranges[slot].index = fetch.next_index;
		fetch.ranges[slot].offset = fetch.next_offset;
		fetch.ranges[slot].length = length;
		fetch.ranges[slot].received = 0;
		fetch.next_offset += length;
		if (fetch.next_offset >= size) {
			fetch.next_index++;
			fetch.next_offset = 0;
		}
	}
	return slot;
}

static void *fetch_main(void *parameter)
{
	int conn = (int)(intptr_t)parameter;
	struct fs_channel channel;
	char const *why = "the host could not be reached";
	int opened = 0, range = -1;
	unsigned long sent_at = fs_milliseconds();

#if defined(__SWITCH__)
	host_thread_place_on_helper_core();
#endif
	memset(&channel, 0, sizeof(channel));
	channel.socket = -1;
	while (conn && !fetch.stop) {
		int go, give_up;

		pthread_mutex_lock(&fetch.lock);
		go = fetch.first_ready;
		give_up = fetch.first_failed || fetch.abort;
		pthread_mutex_unlock(&fetch.lock);
		if (give_up)
			goto done;
		if (go)
			break;
		fs_sleep(20);
	}
	if (fetch.stop || !fs_channel_buffers(&channel) ||
		!joiner_open(&channel, fetch.direct_address, fetch.direct_port, fetch.address, fetch.port, fetch.session,
			fetch.token, NULL, 0, &fetch.stop, &why, conn != 0)) {
		if (!conn) {
			pthread_mutex_lock(&fetch.lock);
			fetch.first_failed = 1;
			fetch.state = HOST_FS_FETCH_FAILED_EARLY;
			snprintf(fetch.why, sizeof(fetch.why), "%s", why);
			pthread_mutex_unlock(&fetch.lock);
			fs_log("file share: nothing received: %s", why);
		}
		goto done;
	}
	opened = 1;
	pthread_mutex_lock(&fetch.lock);
	fetch.open++;
	if (!conn) {
		fetch.first_ready = 1;
		fetch.state = HOST_FS_FETCH_RECEIVING;
	}
	pthread_mutex_unlock(&fetch.lock);
	while (!fetch.stop && !fetch.abort) {
		long size, data;

		if (range < 0) {
			unsigned empty_file;

			pthread_mutex_lock(&fetch.lock);
			range = fetch_take_range(conn, &empty_file);
			pthread_mutex_unlock(&fetch.lock);
			if (range == -2)
				break;
			if (range < 0 && empty_file != (unsigned)-1) {
				channel.plain[0] = FS_FRAME_WANT;
				channel.plain[1] = 0;
				put16(channel.plain + 2, empty_file);
				put32(channel.plain + 4, 0);
				put32(channel.plain + 8, 0);
				why = "the connection was lost";
				if (!fs_channel_send(&channel, FS_WANT_SIZE, &fetch.stop))
					goto done;
				sent_at = fs_milliseconds();
				pthread_mutex_lock(&fetch.lock);
				fetch_write(&why);
				pthread_mutex_unlock(&fetch.lock);
				continue;
			}
			if (range < 0) {
				/* (the ranges held full, waiting for the writer: or a keep-alive) */
				if (fs_milliseconds() - sent_at > FS_KEEP_ALIVE_MILLISECONDS) {
					channel.plain[0] = FS_FRAME_KEEP_ALIVE;
					channel.plain[1] = channel.plain[2] = channel.plain[3] = 0;
					if (!fs_channel_send(&channel, 4, &fetch.stop))
						goto done;
					sent_at = fs_milliseconds();
				}
				fs_sleep(2);
				continue;
			}
			channel.plain[0] = FS_FRAME_WANT;
			channel.plain[1] = 0;
			put16(channel.plain + 2, fetch.files[fetch.ranges[range].index].file);
			put32(channel.plain + 4, fetch.ranges[range].offset);
			put32(channel.plain + 8, fetch.ranges[range].length);
			why = "the connection was lost";
			if (!fs_channel_send(&channel, FS_WANT_SIZE, &fetch.stop))
				goto done;
			sent_at = fs_milliseconds();
		}
		size = fs_channel_receive(&channel, FS_IDLE_MILLISECONDS, &fetch.stop);
		why = fetch.stop ? "stopped" : "the connection was lost";
		if (size < FS_DATA_HEADER || channel.plain[0] != FS_FRAME_DATA)
			goto done;
		data = size - FS_DATA_HEADER;
		why = "the host sent something other than what was asked";
		pthread_mutex_lock(&fetch.lock);
		if (get16(channel.plain + 2) != fetch.files[fetch.ranges[range].index].file ||
			get32(channel.plain + 4) != fetch.ranges[range].offset + fetch.ranges[range].received ||
			(uint32_t)data > fetch.ranges[range].length - fetch.ranges[range].received) {
			pthread_mutex_unlock(&fetch.lock);
			goto done;
		}
		memcpy(fetch.ranges[range].buffer + fetch.ranges[range].received, channel.plain + FS_DATA_HEADER, (size_t)data);
		fetch.ranges[range].received += (uint32_t)data;
		fetch.received += (uint64_t)data;
		if (fetch.ranges[range].received == fetch.ranges[range].length) {
			fetch.ranges[range].state = RANGE_DONE;
			range = -1;
			if (!fetch_write(&why)) {
				pthread_mutex_unlock(&fetch.lock);
				goto done;
			}
		}
		pthread_mutex_unlock(&fetch.lock);
	}
	why = fetch.abort ? fetch.why : "stopped";
	channel.plain[0] = FS_FRAME_END;
	channel.plain[1] = channel.plain[2] = channel.plain[3] = 0;
	fs_channel_send(&channel, 4, &fetch.stop);
done:
	fs_channel_free(&channel);
	pthread_mutex_lock(&fetch.lock);
	/* (its range back to the others) */
	if (range >= 0 && fetch.ranges[range].state == RANGE_WANTED) {
		fetch.ranges[range].conn = -1;
		fetch.ranges[range].received = 0;
	}
	if (opened)
		fetch.open--;
	/* (the last of the connections gone: the transfer's end) */
	if (++fetch.ended == fetch.connections) {
		unsigned long elapsed = fs_milliseconds() - fetch.began;
		int done_all = fetch.write_index == fetch.count;

		if (!done_all && !fetch.stop && fetch.state != HOST_FS_FETCH_FAILED_LATE &&
			fetch.state != HOST_FS_FETCH_FAILED_EARLY) {
			fetch.state = fetch.wrote_any || fetch.files_done ? HOST_FS_FETCH_FAILED_LATE : HOST_FS_FETCH_FAILED_EARLY;
			snprintf(fetch.why, sizeof(fetch.why), "%s", why);
		}
		if (fetch.received)
			fs_log("file share: received %.1f MB in %.1f s (%.1f MB/s) over %d connections%s%s",
				fetch.received / 1048576.0, elapsed / 1000.0,
				elapsed ? fetch.received / 1048576.0 / (elapsed / 1000.0) : 0.0, fetch.connections,
				done_all || fetch.stop ? "" : ": failed, ", done_all || fetch.stop ? "" : fetch.why);
	}
	pthread_mutex_unlock(&fetch.lock);
	return NULL;
}

int host_fs_fetch_begin(unsigned int address, int port, unsigned int session, unsigned int token_address, int count)
{
	host_fs_fetch_stop();
	if (count < 1 || count > FS_FILES || port <= 0 || port > 65535 || !address)
		return 0;
	fetch.files = calloc((size_t)count, sizeof(*fetch.files));
	if (!fetch.files)
		return 0;
	fetch.address = address;
	fetch.port = (unsigned)port;
	fetch.direct_address = fs_direct.address;
	fetch.direct_port = fs_direct.port;
	fetch.session = session;
	memcpy(fetch.token, GUEST_BYTES(token_address), 32);
	fetch.count = count;
	fetch.given = 0;
	fetch.state = HOST_FS_FETCH_IDLE;
	fetch.files_done = 0;
	fetch.current_bytes = 0;
	fetch.received = 0;
	fetch.why[0] = 0;
	fetch.next_index = 0;
	fetch.next_offset = 0;
	fetch.write_index = 0;
	fetch.write_offset = 0;
	fetch.file = NULL;
	fetch.writing = 0;
	fetch.wrote_any = 0;
	fetch.abort = 0;
	fetch.open = 0;
	fetch.ended = 0;
	fetch.first_ready = fetch.first_failed = 0;
	return 1;
}

static int fetch_file_set(int index, int file, char const *path, uint32_t size, unsigned char const sha256[32])
{
	if (!fetch.files || fetch.running || index < 0 || index >= fetch.count || file < 0 || file > 0xffff ||
		strlen(path) >= FS_PATH)
		return 0;
	fetch.files[index].file = (unsigned)file;
	strcpy(fetch.files[index].path, path);
	fetch.files[index].size = size;
	memcpy(fetch.files[index].sha256, sha256, 32);
	if (index + 1 > fetch.given)
		fetch.given = index + 1;
	return 1;
}

int host_fs_fetch_file(int index, int file, unsigned int path_address, unsigned int size, unsigned int sha256_address)
{
	return fetch_file_set(index, file, GUEST_TEXT(path_address), size, GUEST_BYTES(sha256_address));
}

int host_fs_fetch_start(void)
{
	if (!fetch.files || fetch.running || fetch.given != fetch.count)
		return 0;
	fetch.stop = 0;
	fetch.began = fs_milliseconds();
	fetch.state = HOST_FS_FETCH_CONNECTING;
	fetch.connections = FS_PIECE_CONNECTIONS;
	{
		int i;

		for (i = 0; i < fetch.connections; i++) {
			fetch.started[i] = pthread_create(&fetch.threads[i], NULL, fetch_main, (void *)(intptr_t)i) == 0;
			if (!fetch.started[i] && !i) {
				fetch.state = HOST_FS_FETCH_FAILED_EARLY;
				return 0;
			}
		}
	}
	fetch.running = 1;
	return 1;
}

static void fetch_status(struct host_fs_fetch_status *status)
{
	pthread_mutex_lock(&fetch.lock);
	memset(status, 0, sizeof(*status));
	status->state = fetch.state;
	status->files_done = fetch.files_done;
	status->current_bytes = fetch.current_bytes;
	status->received_low = (uint32_t)fetch.received;
	status->received_high = (uint32_t)(fetch.received >> 32);
	memcpy(status->why, fetch.why, sizeof(status->why));
	pthread_mutex_unlock(&fetch.lock);
}

int host_fs_fetch_status(unsigned int out_address)
{
	struct host_fs_fetch_status status;

	fetch_status(&status);
	memcpy(GUEST_BYTES(out_address), &status, sizeof(status));
	return status.state;
}

void host_fs_fetch_stop(void)
{
	int i;

	if (fetch.running) {
		fetch.stop = 1;
		for (i = 0; i < fetch.connections; i++)
			if (fetch.started[i])
				pthread_join(fetch.threads[i], NULL);
		fetch.running = 0;
	}
	memset(fetch.started, 0, sizeof(fetch.started));
	if (fetch.file) {
		fclose(fetch.file);
		fetch.file = NULL;
	}
	for (i = 0; i < FS_FETCH_RANGES; i++) {
		free(fetch.ranges[i].buffer);
		memset(&fetch.ranges[i], 0, sizeof(fetch.ranges[i]));
	}
	free(fetch.files);
	fetch.files = NULL;
	fs_wipe(fetch.token, sizeof(fetch.token));
	fetch.count = fetch.given = 0;
	pthread_mutex_lock(&fetch.lock);
	fetch.state = HOST_FS_FETCH_IDLE;
	pthread_mutex_unlock(&fetch.lock);
}

/* ---------- the joiner: a Custom Edition map's pieces

Over several connections to the host at once (FS_PIECE_CONNECTIONS): one TCP
connection over a long internet path settles at a few MB/s whatever the line
(CUBIC halves its window at each loss; 3.2 MB/s on a 265 Mbit/s upload), as
speed tests open many. The first proves the token (its READY) and the others
open after it; each takes its share of the pieces asked (FS_PIECE_SLOTS over
the connections), and one that drops gives its unfinished pieces back to the
others. The host takes at most FS_SESSION_CONNECTIONS of a transfer: a
connection it refuses ends quietly. */

static struct {
	pthread_mutex_t lock;
	int running;
	pthread_t threads[FS_PIECE_CONNECTIONS];
	int started[FS_PIECE_CONNECTIONS];
	volatile int stop;
	uint32_t address;
	unsigned port;
	uint32_t direct_address;
	unsigned direct_port;
	uint32_t session;
	unsigned char token[32];
	int state;
	/* the connections wanted, open now, and whether the first opened (the
	 * others wait for it) or failed (they do not try) */
	int connections;
	int open;
	int ended;
	int first_ready, first_failed;
	char why[96];
	/* the pieces asked, in the order asked: free, asked (not sent), sent
	 * (coming, on connection conn), done (checked: taken next) */
	struct {
		int state;
		int conn;
		unsigned file;
		uint32_t offset, length, received;
		unsigned char hash[32];
		unsigned char *buffer;
		int matched;
		unsigned long order;
	} slots[FS_PIECE_SLOTS];
	unsigned long next_order;
	uint64_t received;
	unsigned long began;
} pieces = { .lock = PTHREAD_MUTEX_INITIALIZER };

enum {
	PIECE_FREE,
	PIECE_ASKED,
	PIECE_SENT,
	PIECE_DONE,
};

/* (lock held) the connection's sent piece asked first, which its next data is
 * of, or -1 */
static int pieces_oldest_sent(int conn)
{
	int i, oldest = -1;

	for (i = 0; i < FS_PIECE_SLOTS; i++)
		if (pieces.slots[i].state == PIECE_SENT && pieces.slots[i].conn == conn &&
			(oldest < 0 || pieces.slots[i].order < pieces.slots[oldest].order))
			oldest = i;
	return oldest;
}

/* (lock held) the connection's unfinished pieces given back to the others */
static void pieces_give_back(int conn)
{
	int i;

	for (i = 0; i < FS_PIECE_SLOTS; i++)
		if (pieces.slots[i].state == PIECE_SENT && pieces.slots[i].conn == conn) {
			pieces.slots[i].state = PIECE_ASKED;
			pieces.slots[i].received = 0;
		}
}

static void *pieces_main(void *parameter)
{
	int conn = (int)(intptr_t)parameter;
	struct fs_channel channel;
	char const *why = "stopped";
	int opened = 0, share;
	unsigned long sent_at = fs_milliseconds();

#if defined(__SWITCH__)
	host_thread_place_on_helper_core();
#endif
	memset(&channel, 0, sizeof(channel));
	channel.socket = -1;
	/* (the others after the first: the token proved, the host there) */
	while (conn && !pieces.stop) {
		int go, give_up;

		pthread_mutex_lock(&pieces.lock);
		go = pieces.first_ready;
		give_up = pieces.first_failed;
		pthread_mutex_unlock(&pieces.lock);
		if (give_up)
			goto done;
		if (go)
			break;
		fs_sleep(20);
	}
	if (pieces.stop || !fs_channel_buffers(&channel) ||
		!joiner_open(&channel, pieces.direct_address, pieces.direct_port, pieces.address, pieces.port, pieces.session,
			pieces.token, NULL, 0, &pieces.stop, &why, conn != 0)) {
		pthread_mutex_lock(&pieces.lock);
		if (!conn) {
			pieces.first_failed = 1;
			pieces.state = HOST_FS_FETCH_FAILED_EARLY;
			snprintf(pieces.why, sizeof(pieces.why), "%s", why);
		}
		pthread_mutex_unlock(&pieces.lock);
		if (!conn)
			fs_log("file share: no pieces received: %s", why);
		goto done;
	}
	opened = 1;
	pthread_mutex_lock(&pieces.lock);
	pieces.open++;
	if (!conn) {
		pieces.first_ready = 1;
		pieces.state = HOST_FS_FETCH_RECEIVING;
	}
	share = FS_PIECE_SLOTS / pieces.connections;
	if (share < 2)
		share = 2;
	pthread_mutex_unlock(&pieces.lock);
	while (!pieces.stop) {
		size_t at = 0;
		int i, ready, mine = 0;

		/* (the pieces asked since, its share of them, wanted in the order
		 * asked) */
		pthread_mutex_lock(&pieces.lock);
		for (i = 0; i < FS_PIECE_SLOTS; i++)
			mine += pieces.slots[i].state == PIECE_SENT && pieces.slots[i].conn == conn;
		while (mine < share) {
			int first = -1;

			for (i = 0; i < FS_PIECE_SLOTS; i++)
				if (pieces.slots[i].state == PIECE_ASKED &&
					(first < 0 || pieces.slots[i].order < pieces.slots[first].order))
					first = i;
			if (first < 0 || at + FS_WANT_SIZE > FS_RECORD)
				break;
			channel.plain[at] = FS_FRAME_WANT;
			channel.plain[at + 1] = 0;
			put16(channel.plain + at + 2, pieces.slots[first].file);
			put32(channel.plain + at + 4, pieces.slots[first].offset);
			put32(channel.plain + at + 8, pieces.slots[first].length);
			at += FS_WANT_SIZE;
			pieces.slots[first].state = PIECE_SENT;
			pieces.slots[first].conn = conn;
			pieces.slots[first].received = 0;
			mine++;
		}
		pthread_mutex_unlock(&pieces.lock);
		if (!at && fs_milliseconds() - sent_at > FS_KEEP_ALIVE_MILLISECONDS) {
			channel.plain[0] = FS_FRAME_KEEP_ALIVE;
			channel.plain[1] = channel.plain[2] = channel.plain[3] = 0;
			at = 4;
		}
		if (at) {
			why = "the connection was lost";
			if (!fs_channel_send(&channel, at, &pieces.stop))
				goto done;
			sent_at = fs_milliseconds();
		}
		ready = fs_wait(channel.socket, 20, 0);
		if (ready < 0)
			goto done;
		if (ready > 0) {
			long size = fs_channel_receive(&channel, FS_IDLE_MILLISECONDS, &pieces.stop), data;
			int slot;

			why = pieces.stop ? "stopped" : "the connection was lost";
			if (size < FS_DATA_HEADER || channel.plain[0] != FS_FRAME_DATA)
				goto done;
			data = size - FS_DATA_HEADER;
			why = "the host sent something other than what was asked";
			pthread_mutex_lock(&pieces.lock);
			slot = pieces_oldest_sent(conn);
			if (slot < 0 || get16(channel.plain + 2) != pieces.slots[slot].file ||
				get32(channel.plain + 4) != pieces.slots[slot].offset + pieces.slots[slot].received ||
				(uint32_t)data > pieces.slots[slot].length - pieces.slots[slot].received) {
				pthread_mutex_unlock(&pieces.lock);
				goto done;
			}
			memcpy(pieces.slots[slot].buffer + pieces.slots[slot].received, channel.plain + FS_DATA_HEADER,
				(size_t)data);
			pieces.slots[slot].received += (uint32_t)data;
			pieces.received += (uint64_t)data;
			if (pieces.slots[slot].received == pieces.slots[slot].length) {
				unsigned char digest[32];

				/* (still this connection's while it hashes: nothing else takes
				 * a sent piece of another's) */
				pthread_mutex_unlock(&pieces.lock);
				fs_blake2b_256(pieces.slots[slot].buffer, pieces.slots[slot].length, digest);
				pthread_mutex_lock(&pieces.lock);
				pieces.slots[slot].matched = fs_equal(digest, pieces.slots[slot].hash, 32);
				pieces.slots[slot].state = PIECE_DONE;
			}
			pthread_mutex_unlock(&pieces.lock);
		}
	}
	why = "stopped";
	channel.plain[0] = FS_FRAME_END;
	channel.plain[1] = channel.plain[2] = channel.plain[3] = 0;
	fs_channel_send(&channel, 4, NULL);
done:
	fs_channel_free(&channel);
	pthread_mutex_lock(&pieces.lock);
	pieces_give_back(conn);
	if (opened)
		pieces.open--;
	/* (the last of the connections gone, the download not stopped: failed) */
	if (++pieces.ended == pieces.connections) {
		unsigned long elapsed = fs_milliseconds() - pieces.began;

		if (!pieces.stop && pieces.state != HOST_FS_FETCH_FAILED_EARLY) {
			pieces.state = HOST_FS_FETCH_FAILED_LATE;
			snprintf(pieces.why, sizeof(pieces.why), "%s", why);
		}
		if (pieces.received)
			fs_log("file share: received %.1f MB of a map's pieces in %.1f s (%.1f MB/s) over %d connections%s%s",
				pieces.received / 1048576.0, elapsed / 1000.0,
				elapsed ? pieces.received / 1048576.0 / (elapsed / 1000.0) : 0.0, pieces.connections,
				pieces.stop ? "" : ": ", pieces.stop ? "" : why);
	}
	pthread_mutex_unlock(&pieces.lock);
	return NULL;
}

int host_fs_pieces_begin(unsigned int address, int port, unsigned int session, unsigned int token_address)
{
	int i;

	host_fs_pieces_stop();
	if (!address || port <= 0 || port > 65535)
		return 0;
	pieces.address = address;
	pieces.port = (unsigned)port;
	pieces.direct_address = fs_direct.address;
	pieces.direct_port = fs_direct.port;
	pieces.session = session;
	memcpy(pieces.token, GUEST_BYTES(token_address), 32);
	pieces.stop = 0;
	pieces.received = 0;
	pieces.began = fs_milliseconds();
	pieces.state = HOST_FS_FETCH_CONNECTING;
	pieces.connections = FS_PIECE_CONNECTIONS;
	pieces.open = 0;
	pieces.ended = 0;
	pieces.first_ready = pieces.first_failed = 0;
	pieces.why[0] = 0;
	for (i = 0; i < pieces.connections; i++) {
		pieces.started[i] = pthread_create(&pieces.threads[i], NULL, pieces_main, (void *)(intptr_t)i) == 0;
		if (!pieces.started[i] && !i) {
			pieces.state = HOST_FS_FETCH_FAILED_EARLY;
			return 0;
		}
	}
	pieces.running = 1;
	fs_log("file share: up to %d connections for the map's pieces", pieces.connections);
	return 1;
}

int host_fs_pieces_ask(int file, unsigned int offset, unsigned int length, unsigned int hash_address)
{
	int i, asked = 0;

	if (file < 0 || file > 0xffff || !length || length > FS_PIECE_BYTES)
		return 0;
	pthread_mutex_lock(&pieces.lock);
	if (pieces.running && (pieces.state == HOST_FS_FETCH_CONNECTING || pieces.state == HOST_FS_FETCH_RECEIVING))
		for (i = 0; i < FS_PIECE_SLOTS && !asked; i++) {
			if (pieces.slots[i].state != PIECE_FREE)
				continue;
			if (!pieces.slots[i].buffer && !(pieces.slots[i].buffer = malloc(FS_PIECE_BYTES)))
				break;
			pieces.slots[i].state = PIECE_ASKED;
			pieces.slots[i].conn = -1;
			pieces.slots[i].file = (unsigned)file;
			pieces.slots[i].offset = offset;
			pieces.slots[i].length = length;
			pieces.slots[i].received = 0;
			pieces.slots[i].matched = 0;
			pieces.slots[i].order = pieces.next_order++;
			memcpy(pieces.slots[i].hash, GUEST_BYTES(hash_address), 32);
			asked = 1;
		}
	pthread_mutex_unlock(&pieces.lock);
	return asked;
}

int host_fs_pieces_take(unsigned int buffer_address, unsigned int out_address)
{
	int i, taken = 0;

	pthread_mutex_lock(&pieces.lock);
	for (i = 0; i < FS_PIECE_SLOTS && !taken; i++) {
		uint32_t out[4];

		if (pieces.slots[i].state != PIECE_DONE)
			continue;
		out[0] = pieces.slots[i].file;
		out[1] = pieces.slots[i].offset;
		out[2] = pieces.slots[i].length;
		out[3] = (uint32_t)pieces.slots[i].matched;
		if (pieces.slots[i].matched)
			memcpy(GUEST_BYTES(buffer_address), pieces.slots[i].buffer, pieces.slots[i].length);
		memcpy(GUEST_BYTES(out_address), out, sizeof(out));
		pieces.slots[i].state = PIECE_FREE;
		taken = 1;
	}
	pthread_mutex_unlock(&pieces.lock);
	return taken;
}

int host_fs_pieces_state(void)
{
	int state;

	pthread_mutex_lock(&pieces.lock);
	state = pieces.state;
	pthread_mutex_unlock(&pieces.lock);
	return state;
}

void host_fs_pieces_stop(void)
{
	int i;

	if (pieces.running) {
		pieces.stop = 1;
		for (i = 0; i < pieces.connections; i++)
			if (pieces.started[i])
				pthread_join(pieces.threads[i], NULL);
		pieces.running = 0;
	}
	pthread_mutex_lock(&pieces.lock);
	for (i = 0; i < FS_PIECE_SLOTS; i++) {
		free(pieces.slots[i].buffer);
		memset(&pieces.slots[i], 0, sizeof(pieces.slots[i]));
	}
	memset(pieces.started, 0, sizeof(pieces.started));
	fs_wipe(pieces.token, sizeof(pieces.token));
	pieces.state = HOST_FS_FETCH_IDLE;
	pthread_mutex_unlock(&pieces.lock);
}

#ifdef HOST_FILE_SHARE_TEST
/* (tools/test_host_file_share.py's: the rate's steps, as a script gives them:
'f in_game game round_trip cap' (the facts), 's bytes waited' (a send), 't
milliseconds' (the clock moved on and the rate worked out); the allowance and
C after each t) */
void host_file_share_test_rate(FILE *script)
{
	struct fs_rate rate;
	unsigned long now = 1000;
	char kind;

	memset(&rate, 0, sizeof(rate));
	fs_rate_tick(&rate, now);
	while (fscanf(script, " %c", &kind) == 1) {
		if (kind == 'f') {
			int in_game;
			double game, cap;
			unsigned round_trip;

			if (fscanf(script, "%d %lf %u %lf", &in_game, &game, &round_trip, &cap) != 4)
				break;
			fs_rate_facts(&rate, in_game, game, round_trip);
			rate.cap = cap;
		} else if (kind == 's') {
			double bytes;
			unsigned long waited;

			if (fscanf(script, "%lf %lu", &bytes, &waited) != 2)
				break;
			fs_rate_sent(&rate, (size_t)bytes, waited);
		} else if (kind == 't') {
			unsigned long milliseconds;

			if (fscanf(script, "%lu", &milliseconds) != 1)
				break;
			now += milliseconds;
			fs_rate_tick(&rate, now);
			printf("budget=%.0f capacity=%.0f measured=%d\n", rate.budget, rate.capacity, rate.measured);
		}
	}
}
#endif

