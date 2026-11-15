/*
MCC_FAST.C

An MCC package between two SlipstreamCE machines over a connection of its
own, on a thread of its own (mcc_fast.h). OpenCE's MCC sharing
(mcc_share_session.c) carries a package in 3 KB chunks over the game's own
connection, 32 at a time, handled once a frame at each end: a few megabytes
a second at best, less as either end's frames slow. Two SlipstreamCE
machines can do better without OpenCE seeing anything:

- The offer still comes as OpenCE's (the manifest, over the game's
  connection), and the joiner accepts it as before; then it connects to the
  host on MCC_FAST_PORT. An OpenCE host listens on no such port, so the
  connection is refused (over the internet play tunnel, the host's end
  refuses the stream: p2p.c opens only the game's own ports), and the joiner
  goes on with OpenCE's chunks from the start.
- The joiner says which transfer it is (the session number and level the
  offer gave it); the host serves it only to that machine's address, and
  only while that transfer is offered.
- The joiner asks for each file in turn, whole; the host sends its bytes as
  they are, paced by the upload's cap. Nothing is checked here: the joiner's
  staged download checks each file's size and SHA-256 as it does OpenCE's
  chunks (mcc_share_stage.c), so a wrong byte fails the download as one from
  the host's chunks would.
- The host's transfer counts what this connection sends as progress, and is
  marked fully asked for once every file has been sent, so that its timers,
  the joiner's DONE and the admission after it run as they do for OpenCE's
  chunks (mcc_share.c).

Messages, little-endian:
  joiner hello   'SMC1', version 1, 3 zero, session (4), level (64)
  host reply     'SMC1', version 1, result (0 served), 1 zero
  joiner request 'SMC1', file, 3 zero, offset (4), length (4)
  host data      the bytes asked for, as they are
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "bungie_net/network/transport_endpoint_winsock.h"
#include "mcc_fast.h"
#include "mcc_share_session.h"
#include "mcc_share_storage.h"
#include "../src/p2p.h"

#include <pthread.h>
#include <stdint.h>
#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the C library's allocator, not the game's tracked one: this thread
allocates and frees) */
#undef malloc
#undef free

unsigned long system_milliseconds(void);

/* (a connection that says nothing this long before it is served is closed,
and a joiner the host sends nothing this long fails; shorter in its test) */
#ifndef MCC_FAST_IDLE_TIMEOUT
#define MCC_FAST_IDLE_TIMEOUT 30000
#endif

enum
{
	MCC_FAST_PORT = P2P_MCC_FAST_PORT,
	MCC_FAST_VERSION = 1,
	ENTRIES = MCC_SHARE_HOST_TRANSFERS,
	HOST_CONNECTIONS = 8,
	RING_BYTES = 4 << 20,
	BLOCK_BYTES = 64 << 10,
	CONNECT_TIMEOUT = 6000,
	HELLO_TIMEOUT = 8000,
	IDLE_TIMEOUT = MCC_FAST_IDLE_TIMEOUT,
	BUSY_WAIT = 3,
	QUIET_WAIT = 25,
	IDLE_SLEEP = 20,
	LISTEN_RETRY = 10000,
};

#define MCC_FAST_MAGIC 0x31434D53UL /* 'SMC1' */

struct fast_hello
{
	uint32_t magic;
	byte version;
	byte pad[3];
	uint32_t session;
	char level[MCC_SHARE_LEVEL_SIZE];
};

struct fast_reply
{
	uint32_t magic;
	byte version;
	byte result;
	byte pad[2];
};

struct fast_request
{
	uint32_t magic;
	byte file;
	byte pad[3];
	uint32_t offset;
	uint32_t length;
};

typedef char fast_hello_size[sizeof(struct fast_hello) == 76 ? 1 : -1];
typedef char fast_reply_size[sizeof(struct fast_reply) == 8 ? 1 : -1];
typedef char fast_request_size[sizeof(struct fast_request) == 16 ? 1 : -1];

/* a transfer the host's game thread published (paths from the heap) */
struct fast_entry
{
	boolean used;
	boolean published;
	long machine;
	unsigned long address;
	unsigned long session;
	char level[MCC_SHARE_LEVEL_SIZE];
	int count;
	char (*paths)[1024];
	unsigned long sizes[MCC_SHARE_STAGE_FILES];
	/* written by this thread */
	uint64_t sent;
	int files_done;
	unsigned long advanced_at;
};

struct fast_host_connection
{
	boolean used;
	SOCKET socket;
	unsigned long address;
	unsigned long opened_at;
	unsigned long heard_at;
	int entry;
	boolean served;
	byte input[sizeof(struct fast_hello)];
	int input_size;
	/* the request being served */
	FILE *file;
	int file_index;
	unsigned long remaining;
	unsigned long file_end;
	byte *block;
	unsigned long block_size, block_sent;
	byte reply[sizeof(struct fast_reply)];
	int reply_size;
};

static struct
{
	pthread_mutex_t lock;
	boolean thread_started;
	/* the host */
	boolean hosting;
	long limit;
	SOCKET listener;
	boolean listener_open;
	unsigned long listen_tried_at;
	struct fast_entry entries[ENTRIES];
	struct fast_host_connection connections[HOST_CONNECTIONS];
	long tokens;
	unsigned long tokens_at;
	/* the joiner */
	enum mcc_fast_client_state state;
	SOCKET client;
	boolean client_open;
	boolean hello_sent;
	boolean reply_heard;
	boolean got_data;
	unsigned long client_address;
	unsigned long client_session;
	char client_level[MCC_SHARE_LEVEL_SIZE];
	int client_count;
	unsigned long client_sizes[MCC_SHARE_STAGE_FILES];
	int client_file;
	unsigned long client_remaining;
	boolean client_requested;
	unsigned long client_started;
	unsigned long client_heard;
	byte client_input[sizeof(struct fast_reply)];
	int client_input_size;
	byte *ring;
	unsigned long ring_head, ring_used;
	char why[160];
} fast = { PTHREAD_MUTEX_INITIALIZER };

static unsigned long swap_long(unsigned long value)
{
	return ((value & 0xFF) << 24) | ((value & 0xFF00) << 8) | ((value >> 8) & 0xFF00) | ((value >> 24) & 0xFF);
}

static boolean elapsed(unsigned long since, unsigned long now, unsigned long span)
{
	return now - since >= span;
}

static boolean would_block(void)
{
	return WSAGetLastError() == WSAEWOULDBLOCK;
}

static void socket_nonblocking(SOCKET socket_handle)
{
	unsigned long nonblocking = 1;

	ioctlsocket(socket_handle, FIONBIO, &nonblocking);
}

/* ---------- the host */

static void host_connection_close(struct fast_host_connection *connection, char const *why)
{
	if (!connection->used)
		return;
	if (why)
		error(_error_silent, "mcc fast: a joiner's connection closed: %s", why);
	closesocket(connection->socket);
	if (connection->file)
		fclose(connection->file);
	free(connection->block);
	csmemset(connection, 0, sizeof(*connection));
}

static void host_listener_update(unsigned long now)
{
	struct sockaddr_in address;
	int reuse = 1;

	if (!fast.hosting)
	{
		short index;

		if (fast.listener_open)
		{
			closesocket(fast.listener);
			fast.listener_open = FALSE;
		}
		for (index = 0; index < HOST_CONNECTIONS; index++)
			host_connection_close(&fast.connections[index], NULL);
		return;
	}
	if (fast.listener_open || (fast.listen_tried_at && !elapsed(fast.listen_tried_at, now, LISTEN_RETRY)))
		return;
	fast.listen_tried_at = now | 1;
	fast.listener = socket(AF_INET, SOCK_STREAM, 0);
	if (fast.listener == INVALID_SOCKET)
		return;
	csmemset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = (unsigned short)(((MCC_FAST_PORT & 0xFF) << 8) | (MCC_FAST_PORT >> 8));
	setsockopt(fast.listener, SOL_SOCKET, SO_REUSEADDR, (char const *)&reuse, sizeof(reuse));
	if (bind(fast.listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(fast.listener, 4) != 0)
	{
		error(_error_silent, "mcc fast: cannot take connections on port %d (in use?)", MCC_FAST_PORT);
		closesocket(fast.listener);
		return;
	}
	socket_nonblocking(fast.listener);
	fast.listener_open = TRUE;
}

static void host_accept(unsigned long now)
{
	while (fast.listener_open)
	{
		struct sockaddr_in from;
		int from_length = sizeof(from);
		SOCKET socket_handle;
		short index;

		csmemset(&from, 0, sizeof(from));
		socket_handle = accept(fast.listener, (struct sockaddr *)&from, &from_length);
		if (socket_handle == INVALID_SOCKET)
			return;
		for (index = 0; index < HOST_CONNECTIONS && fast.connections[index].used; index++)
			;
		if (index == HOST_CONNECTIONS || from.sin_family != AF_INET)
		{
			closesocket(socket_handle);
			continue;
		}
		socket_nonblocking(socket_handle);
		csmemset(&fast.connections[index], 0, sizeof(fast.connections[index]));
		fast.connections[index].used = TRUE;
		fast.connections[index].socket = socket_handle;
		fast.connections[index].address = swap_long(from.sin_addr.s_addr);
		fast.connections[index].opened_at = now;
		fast.connections[index].heard_at = now;
		fast.connections[index].entry = NONE;
	}
}

/* the transfer a hello names, from the address it came from: its index, or NONE */
static int host_entry_of(struct fast_hello const *hello, unsigned long address)
{
	int index;

	for (index = 0; index < ENTRIES; index++)
	{
		struct fast_entry const *entry = &fast.entries[index];

		if (entry->used && entry->address == address && entry->session == hello->session &&
			!strncmp(entry->level, hello->level, sizeof(entry->level)))
		{
			return index;
		}
	}
	return NONE;
}

/* (the request whole in input) its file opened at its offset */
static boolean host_request_begin(struct fast_host_connection *connection, struct fast_request const *request)
{
	struct fast_entry *entry = &fast.entries[connection->entry];
	long size = -1;

	if (request->magic != MCC_FAST_MAGIC || request->file >= entry->count ||
		request->offset > entry->sizes[request->file] ||
		request->length > entry->sizes[request->file] - request->offset || !request->length)
	{
		return FALSE;
	}
	if (connection->file)
		fclose(connection->file);
	connection->file = mcc_share_open_native(entry->paths[request->file]);
	if (connection->file && !fseek(connection->file, 0, SEEK_END))
		size = ftell(connection->file);
	if (!connection->file || size != (long)entry->sizes[request->file] ||
		fseek(connection->file, (long)request->offset, SEEK_SET))
	{
		return FALSE;
	}
	if (!connection->block && !(connection->block = malloc(BLOCK_BYTES)))
		return FALSE;
	connection->file_index = request->file;
	connection->remaining = request->length;
	connection->file_end = request->offset + request->length == entry->sizes[request->file];
	connection->block_size = connection->block_sent = 0;
	return TRUE;
}

static void host_read(struct fast_host_connection *connection, unsigned long now)
{
	for (;;)
	{
		int wanted = connection->served ? (int)sizeof(struct fast_request) : (int)sizeof(struct fast_hello);
		int got;

		if (connection->served && connection->remaining)
			return;
		got = recv(connection->socket, (char *)connection->input + connection->input_size,
			wanted - connection->input_size, 0);
		if (got == 0 || (got < 0 && !would_block()))
		{
			host_connection_close(connection, connection->served ? NULL : "lost before its hello");
			return;
		}
		if (got < 0)
			return;
		connection->heard_at = now;
		connection->input_size += got;
		if (connection->input_size < wanted)
			continue;
		connection->input_size = 0;
		if (!connection->served)
		{
			struct fast_hello hello;

			csmemcpy(&hello, connection->input, sizeof(hello));
			if (hello.magic != MCC_FAST_MAGIC || hello.version != MCC_FAST_VERSION ||
				!memchr(hello.level, 0, sizeof(hello.level)))
			{
				host_connection_close(connection, "not a SlipstreamCE MCC hello");
				return;
			}
			/* (waited for while the transfer is not yet published: its offer
			may be a frame ahead of this connection) */
			csmemcpy(connection->input, &hello, sizeof(hello));
			connection->input_size = sizeof(hello);
			return;
		}
		{
			struct fast_request request;

			csmemcpy(&request, connection->input, sizeof(request));
			if (!host_request_begin(connection, &request))
			{
				host_connection_close(connection, "a request this host cannot serve");
				return;
			}
		}
	}
}

/* a hello whole and waiting: served once its transfer is published */
static void host_hello_answer(struct fast_host_connection *connection, unsigned long now)
{
	struct fast_hello hello;
	struct fast_reply reply;

	if (connection->served || connection->input_size != (int)sizeof(hello))
		return;
	csmemcpy(&hello, connection->input, sizeof(hello));
	connection->entry = host_entry_of(&hello, connection->address);
	if (connection->entry == NONE)
	{
		if (elapsed(connection->opened_at, now, HELLO_TIMEOUT))
			host_connection_close(connection, "no transfer of that session for its machine");
		return;
	}
	csmemset(&reply, 0, sizeof(reply));
	reply.magic = MCC_FAST_MAGIC;
	reply.version = MCC_FAST_VERSION;
	csmemcpy(connection->reply, &reply, sizeof(reply));
	connection->reply_size = sizeof(reply);
	connection->served = TRUE;
	connection->input_size = 0;
	error(_error_silent, "mcc fast: machine #%ld takes %s over its own connection",
		fast.entries[connection->entry].machine, fast.entries[connection->entry].level);
}

static void host_write(struct fast_host_connection *connection, unsigned long now)
{
	struct fast_entry *entry;

	while (connection->reply_size)
	{
		int sent = send(connection->socket, (char const *)connection->reply +
			(sizeof(connection->reply) - connection->reply_size), connection->reply_size, 0);

		if (sent < 0)
		{
			if (!would_block())
				host_connection_close(connection, "lost");
			return;
		}
		connection->reply_size -= sent;
	}
	if (!connection->served || connection->entry == NONE)
		return;
	entry = &fast.entries[connection->entry];
	while (connection->remaining || connection->block_sent < connection->block_size)
	{
		int sent;

		if (connection->block_sent == connection->block_size)
		{
			unsigned long bytes = connection->remaining < BLOCK_BYTES ? connection->remaining : BLOCK_BYTES;

			/* (the upload's cap: a block goes while the allowance is positive) */
			if (fast.tokens <= 0)
				return;
			if (fread(connection->block, 1, bytes, connection->file) != bytes)
			{
				host_connection_close(connection, "a file could not be read");
				return;
			}
			connection->block_size = bytes;
			connection->block_sent = 0;
			connection->remaining -= bytes;
			fast.tokens -= (long)bytes;
		}
		sent = send(connection->socket, (char const *)connection->block + connection->block_sent,
			(int)(connection->block_size - connection->block_sent), 0);
		if (sent < 0)
		{
			if (!would_block())
				host_connection_close(connection, "lost");
			return;
		}
		connection->block_sent += (unsigned long)sent;
		entry->sent += (uint64_t)sent;
		entry->advanced_at = now;
		if (!connection->remaining && connection->block_sent == connection->block_size)
		{
			if (connection->file_end)
				entry->files_done = connection->file_index + 1 > entry->files_done ?
					connection->file_index + 1 : entry->files_done;
			fclose(connection->file);
			connection->file = NULL;
		}
	}
}

static void host_update(unsigned long now)
{
	short index;

	fast.tokens += (long)((now - fast.tokens_at < 1000 ? now - fast.tokens_at : 1000) * (unsigned long)fast.limit / 1000);
	if (fast.tokens > (fast.limit / 4 > 2 * BLOCK_BYTES ? fast.limit / 4 : 2 * BLOCK_BYTES))
		fast.tokens = fast.limit / 4 > 2 * BLOCK_BYTES ? fast.limit / 4 : 2 * BLOCK_BYTES;
	fast.tokens_at = now;
	host_accept(now);
	for (index = 0; index < HOST_CONNECTIONS; index++)
	{
		struct fast_host_connection *connection = &fast.connections[index];

		if (!connection->used)
			continue;
		host_read(connection, now);
		if (!connection->used)
			continue;
		host_hello_answer(connection, now);
		if (!connection->used)
			continue;
		if (connection->served && connection->entry != NONE && !fast.entries[connection->entry].used)
		{
			host_connection_close(connection, "its transfer ended");
			continue;
		}
		host_write(connection, now);
		/* (one that has not said which transfer it is; one served lives as
		long as its transfer, whose own timers, mcc_share_session.c, let go
		of a joiner gone quiet: the joiner says nothing here while a file
		comes, nor until all of it sent has reached it, so a count of its
		silence cut a long file at its end, and one from the last byte sent
		a joiner slower than 30 seconds to take in what was on its way) */
		if (connection->used && !connection->served && elapsed(connection->heard_at, now, IDLE_TIMEOUT))
			host_connection_close(connection, "silent");
	}
}

/* ---------- the joiner */

static void client_fail(char const *why)
{
	if (fast.client_open)
		closesocket(fast.client);
	fast.client_open = FALSE;
	snprintf(fast.why, sizeof(fast.why), "%s", why);
	fast.state = fast.got_data ? MCC_FAST_FAILED_LATE : MCC_FAST_FAILED_EARLY;
	error(_error_silent, "mcc fast: %s%s", why, fast.got_data ? "" : ": the host's own way instead");
}

static void client_update(unsigned long now)
{
	if (fast.state != MCC_FAST_CONNECTING && fast.state != MCC_FAST_RECEIVING)
		return;
	if (fast.state == MCC_FAST_CONNECTING && elapsed(fast.client_started, now, fast.hello_sent ? HELLO_TIMEOUT : CONNECT_TIMEOUT))
	{
		client_fail(fast.hello_sent ? "the host did not answer its hello" : "could not reach the host's port");
		return;
	}
	if (fast.state == MCC_FAST_RECEIVING && elapsed(fast.client_heard, now, IDLE_TIMEOUT))
	{
		client_fail("the host sent nothing for 30 seconds");
		return;
	}
	if (!fast.hello_sent)
	{
		fd_set writeable, failed;
		struct timeval timeout;
		struct fast_hello hello;

		writeable.fd_array[0] = fast.client;
		writeable.fd_count = 1;
		failed.fd_array[0] = fast.client;
		failed.fd_count = 1;
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		if (select(1, NULL, &writeable, &failed, &timeout) <= 0)
			return;
		if (failed.fd_count && __WSAFDIsSet(fast.client, &failed))
		{
			client_fail("the host takes no such connection (an OpenCE host)");
			return;
		}
		csmemset(&hello, 0, sizeof(hello));
		hello.magic = MCC_FAST_MAGIC;
		hello.version = MCC_FAST_VERSION;
		hello.session = fast.client_session;
		csmemcpy(hello.level, fast.client_level, sizeof(hello.level));
		/* (small, and the socket just took a connect: whole or not at all) */
		if (send(fast.client, (char const *)&hello, sizeof(hello), 0) != (int)sizeof(hello))
		{
			client_fail("its hello could not be sent");
			return;
		}
		fast.hello_sent = TRUE;
	}
	if (!fast.reply_heard)
	{
		int got = recv(fast.client, (char *)fast.client_input + fast.client_input_size,
			(int)sizeof(fast.client_input) - fast.client_input_size, 0);
		struct fast_reply reply;

		if (got == 0 || (got < 0 && !would_block()))
		{
			client_fail("the host takes no such connection (an OpenCE host)");
			return;
		}
		if (got < 0)
			return;
		fast.client_input_size += got;
		if (fast.client_input_size < (int)sizeof(reply))
			return;
		csmemcpy(&reply, fast.client_input, sizeof(reply));
		if (reply.magic != MCC_FAST_MAGIC || reply.version != MCC_FAST_VERSION || reply.result)
		{
			client_fail("the host refused its hello");
			return;
		}
		fast.reply_heard = TRUE;
		fast.state = MCC_FAST_RECEIVING;
		fast.client_heard = now;
		error(_error_silent, "mcc fast: taking %s from the host over its own connection", fast.client_level);
	}
	for (;;)
	{
		unsigned long tail, room;
		int got;

		if (!fast.client_requested)
		{
			struct fast_request request;

			if (fast.client_file >= fast.client_count)
			{
				closesocket(fast.client);
				fast.client_open = FALSE;
				fast.state = MCC_FAST_DONE;
				error(_error_silent, "mcc fast: every file of %s received", fast.client_level);
				return;
			}
			csmemset(&request, 0, sizeof(request));
			request.magic = MCC_FAST_MAGIC;
			request.file = (byte)fast.client_file;
			request.offset = 0;
			request.length = fast.client_sizes[fast.client_file];
			if (send(fast.client, (char const *)&request, sizeof(request), 0) != (int)sizeof(request))
			{
				if (!would_block())
					client_fail("a request could not be sent");
				return;
			}
			fast.client_requested = TRUE;
			fast.client_remaining = request.length;
		}
		if (!fast.client_remaining)
		{
			fast.client_file++;
			fast.client_requested = FALSE;
			continue;
		}
		/* (into the ring, contiguous room from its tail: none, wait for the game's thread) */
		tail = (fast.ring_head + fast.ring_used) % RING_BYTES;
		room = RING_BYTES - fast.ring_used;
		if (room > RING_BYTES - tail)
			room = RING_BYTES - tail;
		if (room > fast.client_remaining)
			room = fast.client_remaining;
		if (!room)
			return;
		got = recv(fast.client, (char *)fast.ring + tail, (int)room, 0);
		if (got == 0 || (got < 0 && !would_block()))
		{
			client_fail("lost the host");
			return;
		}
		if (got < 0)
			return;
		fast.got_data = TRUE;
		fast.client_heard = now;
		fast.ring_used += (unsigned long)got;
		fast.client_remaining -= (unsigned long)got;
	}
}

/* ---------- the thread */

static unsigned long __stdcall mcc_fast_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		fd_set readable, writeable;
		struct timeval timeout;
		boolean busy = FALSE;
		short index;
		unsigned long now = system_milliseconds();

		readable.fd_count = 0;
		writeable.fd_count = 0;
		pthread_mutex_lock(&fast.lock);
		host_listener_update(now);
		if (fast.listener_open)
			readable.fd_array[readable.fd_count++] = fast.listener;
		for (index = 0; index < HOST_CONNECTIONS; index++)
		{
			struct fast_host_connection const *connection = &fast.connections[index];

			if (!connection->used)
				continue;
			readable.fd_array[readable.fd_count++] = connection->socket;
			if (connection->reply_size || connection->remaining || connection->block_sent < connection->block_size)
			{
				writeable.fd_array[writeable.fd_count++] = connection->socket;
				busy = TRUE;
			}
		}
		if (fast.client_open && (fast.state == MCC_FAST_CONNECTING || fast.state == MCC_FAST_RECEIVING))
		{
			readable.fd_array[readable.fd_count++] = fast.client;
			if (!fast.hello_sent)
				writeable.fd_array[writeable.fd_count++] = fast.client;
			busy = TRUE;
		}
		pthread_mutex_unlock(&fast.lock);
		if (!readable.fd_count)
		{
			Sleep(IDLE_SLEEP);
			continue;
		}
		timeout.tv_sec = 0;
		timeout.tv_usec = (busy ? BUSY_WAIT : QUIET_WAIT) * 1000;
		select(0, &readable, writeable.fd_count ? &writeable : NULL, NULL, &timeout);
		pthread_mutex_lock(&fast.lock);
		now = system_milliseconds();
		host_update(now);
		client_update(now);
		pthread_mutex_unlock(&fast.lock);
	}
	return 0;
}

static void thread_start(void)
{
	if (fast.thread_started)
		return;
	fast.thread_started = TRUE;
	if (!CreateThread(NULL, 0, mcc_fast_thread, NULL, 0, NULL))
		error(_error_silent, "mcc fast: no thread for its connections");
}

/* ---------- the host's game thread */

void mcc_fast_host_frame(int hosting, long limit)
{
	if (!hosting && !fast.thread_started)
		return;
	pthread_mutex_lock(&fast.lock);
	thread_start();
	fast.hosting = hosting != 0;
	fast.limit = limit > 0 ? limit : 1024 * 1024;
	if (!fast.tokens_at)
		fast.tokens_at = system_milliseconds();
	pthread_mutex_unlock(&fast.lock);
}

void mcc_fast_host_publish(struct mcc_share_host_transfer const *transfer, unsigned long address)
{
	int index, free_index = NONE, i;
	struct fast_entry *entry = NULL;

	if (!transfer || !transfer->used || transfer->hashing || !transfer->offer_complete || !address)
		return;
	pthread_mutex_lock(&fast.lock);
	for (index = 0; index < ENTRIES; index++)
	{
		if (fast.entries[index].used && fast.entries[index].machine == transfer->machine &&
			fast.entries[index].session == transfer->session)
		{
			entry = &fast.entries[index];
			break;
		}
		if (!fast.entries[index].used && free_index == NONE)
			free_index = index;
	}
	if (!entry && free_index != NONE)
	{
		entry = &fast.entries[free_index];
		csmemset(entry, 0, sizeof(*entry));
		entry->paths = malloc((size_t)transfer->package.count * sizeof(*entry->paths));
		if (!entry->paths)
		{
			pthread_mutex_unlock(&fast.lock);
			return;
		}
		entry->used = TRUE;
		entry->machine = transfer->machine;
		entry->session = transfer->session;
		snprintf(entry->level, sizeof(entry->level), "%s", transfer->level);
		entry->count = transfer->package.count;
		for (i = 0; i < entry->count; i++)
		{
			snprintf(entry->paths[i], sizeof(entry->paths[i]), "%s", transfer->package.files[i].path);
			entry->sizes[i] = transfer->files[i].size;
		}
	}
	if (entry)
	{
		entry->address = address;
		entry->published = TRUE;
	}
	pthread_mutex_unlock(&fast.lock);
}

void mcc_fast_host_end_frame(void)
{
	int index;

	pthread_mutex_lock(&fast.lock);
	for (index = 0; index < ENTRIES; index++)
	{
		struct fast_entry *entry = &fast.entries[index];

		if (entry->used && !entry->published)
		{
			free(entry->paths);
			csmemset(entry, 0, sizeof(*entry));
		}
		entry->published = FALSE;
	}
	pthread_mutex_unlock(&fast.lock);
}

int mcc_fast_host_progress(long machine, uint32_t session, uint64_t *sent, int *files_done,
	unsigned long *advanced_at)
{
	int index, found = 0;

	pthread_mutex_lock(&fast.lock);
	for (index = 0; index < ENTRIES && !found; index++)
	{
		struct fast_entry const *entry = &fast.entries[index];

		if (entry->used && entry->machine == machine && entry->session == session && entry->sent)
		{
			*sent = entry->sent;
			*files_done = entry->files_done;
			*advanced_at = entry->advanced_at;
			found = 1;
		}
	}
	pthread_mutex_unlock(&fast.lock);
	return found;
}

/* ---------- the joiner's game thread */

int mcc_fast_client_begin(unsigned long address, uint32_t session, char const *level, int count,
	uint32_t const *sizes)
{
	struct sockaddr_in remote;
	SOCKET socket_handle;
	int i;

	if (!address || !level || count < 1 || count > MCC_SHARE_STAGE_FILES)
		return 0;
	mcc_fast_client_stop();
	pthread_mutex_lock(&fast.lock);
	thread_start();
	if (!fast.ring && !(fast.ring = malloc(RING_BYTES)))
	{
		pthread_mutex_unlock(&fast.lock);
		return 0;
	}
	socket_handle = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_handle == INVALID_SOCKET)
	{
		pthread_mutex_unlock(&fast.lock);
		return 0;
	}
	socket_nonblocking(socket_handle);
	csmemset(&remote, 0, sizeof(remote));
	remote.sin_family = AF_INET;
	remote.sin_port = (unsigned short)(((MCC_FAST_PORT & 0xFF) << 8) | (MCC_FAST_PORT >> 8));
	remote.sin_addr.s_addr = swap_long(address);
	if (connect(socket_handle, (struct sockaddr *)&remote, sizeof(remote)) != 0 && !would_block())
	{
		closesocket(socket_handle);
		pthread_mutex_unlock(&fast.lock);
		return 0;
	}
	fast.client = socket_handle;
	fast.client_open = TRUE;
	fast.state = MCC_FAST_CONNECTING;
	fast.hello_sent = fast.reply_heard = fast.got_data = fast.client_requested = FALSE;
	fast.client_address = address;
	fast.client_session = session;
	snprintf(fast.client_level, sizeof(fast.client_level), "%s", level);
	fast.client_count = count;
	for (i = 0; i < count; i++)
		fast.client_sizes[i] = sizes[i];
	fast.client_file = 0;
	fast.client_remaining = 0;
	fast.client_started = fast.client_heard = system_milliseconds();
	fast.client_input_size = 0;
	fast.ring_head = fast.ring_used = 0;
	fast.why[0] = 0;
	pthread_mutex_unlock(&fast.lock);
	error(_error_silent, "mcc fast: asking the host for %s over its own connection", level);
	return 1;
}

enum mcc_fast_client_state mcc_fast_client_state(void)
{
	enum mcc_fast_client_state state;

	pthread_mutex_lock(&fast.lock);
	state = fast.state;
	pthread_mutex_unlock(&fast.lock);
	return state;
}

uint32_t mcc_fast_client_take(void *buffer, uint32_t max)
{
	uint32_t taken = 0;

	pthread_mutex_lock(&fast.lock);
	while (taken < max && fast.ring_used)
	{
		unsigned long bytes = fast.ring_used;

		if (bytes > RING_BYTES - fast.ring_head)
			bytes = RING_BYTES - fast.ring_head;
		if (bytes > max - taken)
			bytes = max - taken;
		csmemcpy((byte *)buffer + taken, fast.ring + fast.ring_head, bytes);
		fast.ring_head = (fast.ring_head + bytes) % RING_BYTES;
		fast.ring_used -= bytes;
		taken += (uint32_t)bytes;
	}
	pthread_mutex_unlock(&fast.lock);
	return taken;
}

char const *mcc_fast_client_why(void)
{
	return fast.why;
}

void mcc_fast_client_stop(void)
{
	pthread_mutex_lock(&fast.lock);
	if (fast.client_open)
		closesocket(fast.client);
	fast.client_open = FALSE;
	fast.state = MCC_FAST_IDLE;
	fast.ring_head = fast.ring_used = 0;
	pthread_mutex_unlock(&fast.lock);
}
