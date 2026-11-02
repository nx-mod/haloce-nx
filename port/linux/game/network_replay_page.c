/*
NETWORK_REPLAY_PAGE.C

The Switch's and the PS5's way to take replays off the console and put
others on it (port/linux/REPLAY.md): neither has a file manager another app
can open, so while the main menu's Theater is up (network_replay_page_wanted,
called by it each frame) the game serves a small web page on the local
network, shown there as a QR code and an address, as Direct Link's page is
(port/linux/src/link_page.c; network.link_page turns both off). It lists the
replays, each a download, and uploads a replay a phone or a computer has
(PUT /upload/<name>: the page's script sends the file as it is).

One connection at a time. A download is sent and an upload read a piece at
a time, across frames (REPLAY_PAGE_FRAME_BYTES a frame at most), so the
menus keep their pace; an upload goes to <name>.part, which becomes the
replay only once it is whole and begins as a replay does. Nothing listens
while the Theater is down.
*/

#include "cseries.h"
#include "network_replay.h"
#include "../src/posix.h" /* port: port/linux/src/posix.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xtl.h>

int config_boolean(char const *name);
void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
FILE *platform_fopen_host(char const *path, char const *mode);
int platform_remove_host(char const *path);
int platform_rename_host(char const *old_path, char const *new_path);

#if defined(HALO_SWITCH) || defined(HALO_PS5)

#ifdef HALO_PS5
#define REPLAY_PAGE_CONSOLE "PS5"
#else
#define REPLAY_PAGE_CONSOLE "Switch"
#endif

/* (posix.h's calls give Winsock's codes) */
#define REPLAY_PAGE_WOULD_BLOCK 10035
#define REPLAY_PAGE_AF_INET 2
#define REPLAY_PAGE_SOCK_STREAM 1

enum
{
	/* (after Direct Link's ports, link_page.c's 8080 to 8089) */
	REPLAY_PAGE_FIRST_PORT = 8090,
	REPLAY_PAGE_PORTS = 10,
	/* the Theater wanted it this recently, or it closes */
	REPLAY_PAGE_WANTED_MS = 1000,
	/* how often an idle socket is looked at */
	REPLAY_PAGE_LOOK_MS = 250,
	/* a connection that does nothing this long is closed */
	REPLAY_PAGE_IDLE_MS = 10000,
	REPLAY_PAGE_HEAD_SIZE = 8192,
	/* the most of a download sent, or of an upload read, a frame */
	REPLAY_PAGE_FRAME_BYTES = 512 * 1024,
	REPLAY_PAGE_CHUNK_SIZE = 32 * 1024,
	/* the largest replay taken */
	REPLAY_PAGE_MAXIMUM_UPLOAD = 256 * 1024 * 1024,
	REPLAY_PAGE_PATH_SIZE = 640,
	REPLAY_PAGE_LISTED = 256,
};

/* a connection's state */
enum
{
	_replay_page_reading,
	_replay_page_sending,
	_replay_page_receiving,
};

struct replay_page_address
{
	unsigned short family;
	unsigned short port;
	unsigned long address;
	unsigned char zero[8];
};

static struct
{
	int listening;
	int client;
	unsigned short port;
	char address[64];
	unsigned long wanted_at;
	unsigned long looked_at;
	unsigned long active_at;
	short state;
	/* the request's head (and an upload's first bytes) */
	char head[REPLAY_PAGE_HEAD_SIZE + 1];
	int head_length;
	/* what is being sent: a response held (its head, or a page), then a
	file's rest */
	char *response;
	int response_length;
	int response_sent;
	FILE *file;
	long remaining;
	byte chunk[REPLAY_PAGE_CHUNK_SIZE];
	int chunk_length;
	int chunk_sent;
	/* an upload: where it goes, and whether it began as a replay does */
	char part_path[REPLAY_PAGE_PATH_SIZE];
	char final_path[REPLAY_PAGE_PATH_SIZE];
	char name[NETWORK_REPLAY_NAME_SIZE];
	byte first_bytes[8];
	int first_length;
} page = { -1, -1 };

static unsigned short replay_page_network_short(
	unsigned short value)
{
	return (unsigned short)((value >> 8) | (value << 8));
}

static void replay_page_close_client(
	void)
{
	if (page.client >= 0)
		posix_socket_close(page.client);
	page.client = -1;
	page.head_length = 0;
	if (page.response)
		free(page.response);
	page.response = NULL;
	page.response_length = page.response_sent = 0;
	if (page.file)
		fclose(page.file);
	page.file = NULL;
	page.remaining = 0;
	page.chunk_length = page.chunk_sent = 0;
	/* (an upload not finished is no replay) */
	if (page.state == _replay_page_receiving && page.part_path[0])
		platform_remove_host(page.part_path);
	page.part_path[0] = 0;
	page.state = _replay_page_reading;
}

static void replay_page_close(
	void)
{
	replay_page_close_client();
	if (page.listening >= 0)
	{
		posix_socket_close(page.listening);
		platform_log("replay page: closed");
	}
	page.listening = -1;
	page.address[0] = 0;
}

static void replay_page_open(
	void)
{
	unsigned long local = posix_local_ipv4_address();
	unsigned char const *octets = (unsigned char const *)&local;
	struct replay_page_address address;
	int port;

	if (!local)
		return;
	page.listening = posix_socket(REPLAY_PAGE_AF_INET, REPLAY_PAGE_SOCK_STREAM, 0);
	if (page.listening < 0)
		return;
	for (port = REPLAY_PAGE_FIRST_PORT; port < REPLAY_PAGE_FIRST_PORT + REPLAY_PAGE_PORTS; port++)
	{
		memset(&address, 0, sizeof(address));
		address.family = REPLAY_PAGE_AF_INET;
		address.port = replay_page_network_short((unsigned short)port);
		address.address = local;
		if (posix_socket_bind(page.listening, &address, sizeof(address)) == 0)
			break;
	}
	if (port == REPLAY_PAGE_FIRST_PORT + REPLAY_PAGE_PORTS || posix_socket_listen(page.listening, 2) != 0 ||
		posix_socket_set_nonblocking(page.listening, 1) != 0)
	{
		platform_log("replay page: no port to serve it on (error %d)", posix_socket_last_error());
		posix_socket_close(page.listening);
		page.listening = -1;
		return;
	}
	page.port = (unsigned short)port;
	snprintf(page.address, sizeof(page.address), "http://%u.%u.%u.%u:%d/", octets[0], octets[1], octets[2],
		octets[3], port);
	platform_log("replay page: serving %s", page.address);
}

/* a response to send: held whole (a page, or a download's head) */
static void replay_page_respond(
	char const *status,
	char const *type,
	char const *body,
	long body_length,
	char const *extra_headers)
{
	int head_room = 512 + (extra_headers ? (int)strlen(extra_headers) : 0);
	long body_part = body ? body_length : 0;

	page.response = (char *)malloc((size_t)(head_room + body_part + 1));
	if (!page.response)
	{
		replay_page_close_client();
		return;
	}
	page.response_length = snprintf(page.response, (size_t)head_room,
		"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\nCache-Control: no-store\r\n%sConnection: close\r\n\r\n",
		status, type, body_length, extra_headers ? extra_headers : "");
	if (body_part)
	{
		memcpy(page.response + page.response_length, body, (size_t)body_part);
		page.response_length += (int)body_part;
	}
	page.response_sent = 0;
	page.state = _replay_page_sending;
}

static void replay_page_text(
	char const *status,
	char const *text)
{
	replay_page_respond(status, "text/plain; charset=utf-8", text, (long)strlen(text), NULL);
}

/* a replay's name, %-decoded from a path's part (up to a space, '?' or the
end), into name; FALSE for one that is not a replay's (only letters,
digits, '-', '_' and '.', ending .ocreplay) */
static boolean replay_page_name(
	char const *path,
	char *name,
	int size)
{
	static char const extension[] = ".ocreplay";
	int length = 0;
	int index;

	for (; *path && *path != ' ' && *path != '?' && length < size - 1; path++)
	{
		int character = (unsigned char)*path;

		if (character == '%' && path[1] && path[2])
		{
			char hex[3] = { path[1], path[2], 0 };

			character = (int)strtol(hex, NULL, 16);
			path += 2;
		}
		name[length++] = (char)character;
	}
	name[length] = 0;
	if (length <= (int)strlen(extension) || strcmp(name + length - strlen(extension), extension))
		return FALSE;
	for (index = 0; index < length; index++)
	{
		char character = name[index];

		if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.'))
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* text for a page: its &, <, > and " as HTML has them, its line breaks
(network_replay_describe's) as " · " */
static void replay_page_escape(
	char *destination,
	size_t size,
	char const *source)
{
	size_t used = 0;

	for (; *source && used + 12 < size; source++)
	{
		char const *as = *source == '&' ? "&amp;" : *source == '<' ? "&lt;" : *source == '>' ? "&gt;" :
			*source == '"' ? "&quot;" : *source == '\n' ? " &middot; " : NULL;

		if (as)
		{
			memcpy(destination + used, as, strlen(as));
			used += strlen(as);
		}
		else
			destination[used++] = *source;
	}
	destination[used] = 0;
}

/* the page: the replays, each a download, and the upload */
static void replay_page_list(
	void)
{
	static char names[REPLAY_PAGE_LISTED][NETWORK_REPLAY_NAME_SIZE];
	long count = network_replay_list(names, REPLAY_PAGE_LISTED);
	size_t size = 4096 + (size_t)count * (3 * NETWORK_REPLAY_NAME_SIZE + 1024);
	char *body = (char *)malloc(size);
	size_t length;
	long index;

	if (!body)
	{
		replay_page_close_client();
		return;
	}
	length = (size_t)snprintf(body, size,
		"<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, "
		"initial-scale=1\"><title>Halo: replays</title><style>body{font-family:system-ui,sans-serif;"
		"background:#0b1a33;color:#e8eef8;margin:0;padding:24px 16px}main{max-width:640px;margin:auto}"
		"h1{font-size:1.4em}h2{font-size:1.1em;margin-top:28px}a{color:#7cc0ff}ul{padding:0;list-style:none}"
		"li{background:#13284a;margin:6px 0;padding:10px 12px;border-radius:8px}span{color:#9fb6d8;display:block;"
		"margin-top:4px;font-size:.9em}"
		"button{padding:12px 18px;font-size:1em;border:0;border-radius:8px;background:#2896ff;color:#fff;margin-top:10px}"
		"p.note{color:#9fb6d8}</style></head><body><main><h1>Replays on the " REPLAY_PAGE_CONSOLE "</h1>"
		"<p class=\"note\">%ld replay%s. Tap one to download it.</p><ul>", count, count == 1 ? "" : "s");
	for (index = 0; index < count && length < size; index++)
	{
		char description[512];
		char escaped[900];

		/* (its map, game type, when, length, server, player and size) */
		network_replay_describe(names[index], description, sizeof(description));
		replay_page_escape(escaped, sizeof(escaped), description);
		length += (size_t)snprintf(body + length, size - length,
			"<li><a href=\"/replays/%s\" download=\"%s\">%s</a><span>%s</span></li>", names[index], names[index],
			names[index], escaped);
	}
	if (length < size)
	{
		length += (size_t)snprintf(body + length, size - length,
			"</ul><h2>Put replays on the " REPLAY_PAGE_CONSOLE "</h2><input type=\"file\" id=\"files\" multiple "
			"accept=\".ocreplay\"><br><button onclick=\"send()\">Upload</button><p id=\"status\"></p>"
			"<p class=\"note\">This page is the game's, on your local network, while its Theater is open.</p>"
			"<script>async function send(){const status=document.getElementById('status');"
			"for(const file of document.getElementById('files').files){status.textContent='Sending '+file.name+'...';"
			"try{const answer=await fetch('/upload/'+encodeURIComponent(file.name),{method:'PUT',body:file});"
			"status.textContent=file.name+': '+await answer.text();}catch(e){status.textContent=file.name+': '+e;}}"
			"setTimeout(()=>location.reload(),1500);}</script></main></body></html>");
	}
	if (length >= size)
		length = size - 1;
	replay_page_respond("200 OK", "text/html; charset=utf-8", body, (long)length, NULL);
	free(body);
}

/* GET /replays/<name>: the file, sent a piece at a time */
static void replay_page_download(
	char const *path)
{
	char name[NETWORK_REPLAY_NAME_SIZE];
	char file_path[REPLAY_PAGE_PATH_SIZE + NETWORK_REPLAY_NAME_SIZE];
	char headers[3 * NETWORK_REPLAY_NAME_SIZE];
	long size;

	if (!replay_page_name(path, name, sizeof(name)))
	{
		replay_page_text("404 Not Found", "No such replay.");
		return;
	}
	network_replay_folder(file_path, REPLAY_PAGE_PATH_SIZE);
	snprintf(file_path + strlen(file_path), sizeof(file_path) - strlen(file_path), "/%s", name);
	page.file = platform_fopen_host(file_path, "rb");
	if (!page.file || fseek(page.file, 0, SEEK_END) != 0 || (size = ftell(page.file)) < 0 ||
		fseek(page.file, 0, SEEK_SET) != 0)
	{
		if (page.file)
			fclose(page.file);
		page.file = NULL;
		replay_page_text("404 Not Found", "No such replay.");
		return;
	}
	snprintf(headers, sizeof(headers), "Content-Disposition: attachment; filename=\"%s\"\r\n", name);
	/* (the head now, the file after it) */
	replay_page_respond("200 OK", "application/octet-stream", NULL, size, headers);
	page.remaining = size;
	platform_log("replay page: sending %s (%ld KB)", name, size / 1024);
}

/* PUT /upload/<name>: its body to <name>.part, then the replay */
static void replay_page_upload_begin(
	char const *path,
	char const *body,
	int body_received)
{
	char const *length_header = strstr(page.head, "Content-Length:");
	long length = length_header ? strtol(length_header + 15, NULL, 10) : -1;
	struct posix_file_information information;
	char folder[REPLAY_PAGE_PATH_SIZE];

	if (!replay_page_name(path, page.name, sizeof(page.name)))
	{
		replay_page_text("400 Bad Request", "That is not a replay's name (it ends .ocreplay).");
		return;
	}
	if (length < 16 || length > REPLAY_PAGE_MAXIMUM_UPLOAD)
	{
		replay_page_text("400 Bad Request", "That is not a replay's size.");
		return;
	}
	network_replay_folder(folder, sizeof(folder));
	posix_make_directory(folder);
	snprintf(page.final_path, sizeof(page.final_path), "%s/%s", folder, page.name);
	snprintf(page.part_path, sizeof(page.part_path), "%s/%s.part", folder, page.name);
	if (posix_stat(page.final_path, &information) == 0)
	{
		page.part_path[0] = 0;
		replay_page_text("409 Conflict", "The " REPLAY_PAGE_CONSOLE " has a replay of that name already.");
		return;
	}
	page.file = platform_fopen_host(page.part_path, "wb");
	if (!page.file)
	{
		page.part_path[0] = 0;
		replay_page_text("500 Internal Server Error", "The replay cannot be written.");
		return;
	}
	page.state = _replay_page_receiving;
	page.remaining = length;
	page.first_length = 0;
	if (body_received > length)
		body_received = (int)length;
	if (body_received > 0)
	{
		int first = MIN(body_received, (int)sizeof(page.first_bytes));

		memcpy(page.first_bytes, body, (size_t)first);
		page.first_length = first;
		if (fwrite(body, (size_t)body_received, 1, page.file) != 1)
		{
			replay_page_close_client();
			return;
		}
		page.remaining -= body_received;
	}
	platform_log("replay page: receiving %s (%ld KB)", page.name, length / 1024);
}

/* an upload whole: the replay, if it begins as one does */
static void replay_page_upload_end(
	void)
{
	boolean written = fclose(page.file) == 0;

	page.file = NULL;
	page.state = _replay_page_reading;
	if (!written || page.first_length < 8 || memcmp(page.first_bytes, "OCREPLAY", 8))
	{
		platform_remove_host(page.part_path);
		page.part_path[0] = 0;
		replay_page_text("400 Bad Request", "That file is not a replay.");
		return;
	}
	/* (the paths are the card's as they are, not the game's: DeleteFileA,
	MoveFileA and the game's remove and rename would put them under the data
	folder again. A replay of
	that name was refused as the upload began) */
	if (platform_rename_host(page.part_path, page.final_path) != 0)
	{
		platform_remove_host(page.part_path);
		page.part_path[0] = 0;
		replay_page_text("500 Internal Server Error", "The replay cannot be kept.");
		return;
	}
	page.part_path[0] = 0;
	platform_log("replay page: received %s", page.name);
	replay_page_text("200 OK", "Received: it is in the Theater now.");
}

/* the request's head whole: what it asks */
static void replay_page_answer(
	void)
{
	char *head_end = strstr(page.head, "\r\n\r\n");
	char *body = head_end + 4;
	int body_received = page.head_length - (int)(body - page.head);

	if (!strncmp(page.head, "GET /replays/", 13))
		replay_page_download(page.head + 13);
	else if (!strncmp(page.head, "PUT /upload/", 12))
		replay_page_upload_begin(page.head + 12, body, body_received);
	else if (!strncmp(page.head, "GET / ", 6) || !strncmp(page.head, "GET /?", 6))
		replay_page_list();
	else
		replay_page_text("404 Not Found", "Not found.");
}

/* what can be sent now, of the response and the file after it */
static void replay_page_send(
	void)
{
	int budget = REPLAY_PAGE_FRAME_BYTES;

	while (budget > 0)
	{
		char const *data;
		int length;
		int put;

		if (page.response_sent < page.response_length)
		{
			data = page.response + page.response_sent;
			length = page.response_length - page.response_sent;
		}
		else
		{
			if (page.chunk_sent >= page.chunk_length)
			{
				if (!page.file || page.remaining <= 0)
				{
					/* (all of it sent) */
					replay_page_close_client();
					return;
				}
				page.chunk_length = (int)fread(page.chunk, 1, (size_t)MIN(page.remaining, (long)sizeof(page.chunk)),
					page.file);
				page.chunk_sent = 0;
				if (page.chunk_length <= 0)
				{
					replay_page_close_client();
					return;
				}
				page.remaining -= page.chunk_length;
			}
			data = (char const *)page.chunk + page.chunk_sent;
			length = page.chunk_length - page.chunk_sent;
		}
		put = posix_socket_send(page.client, data, MIN(length, budget), 0);
		if (put < 0)
		{
			if (posix_socket_last_error() != REPLAY_PAGE_WOULD_BLOCK)
				replay_page_close_client();
			return;
		}
		if (put == 0)
			return;
		page.active_at = system_milliseconds();
		budget -= put;
		if (page.response_sent < page.response_length)
			page.response_sent += put;
		else
			page.chunk_sent += put;
	}
}

/* what has come of an upload's body */
static void replay_page_receive(
	void)
{
	int budget = REPLAY_PAGE_FRAME_BYTES;

	while (budget > 0 && page.remaining > 0)
	{
		int got = posix_socket_recv(page.client, page.chunk, (int)MIN(page.remaining, (long)sizeof(page.chunk)), 0);

		if (got < 0)
		{
			if (posix_socket_last_error() != REPLAY_PAGE_WOULD_BLOCK)
				replay_page_close_client();
			return;
		}
		if (got == 0)
		{
			/* (closed before the end) */
			replay_page_close_client();
			return;
		}
		page.active_at = system_milliseconds();
		if (page.first_length < (int)sizeof(page.first_bytes))
		{
			int first = MIN(got, (int)sizeof(page.first_bytes) - page.first_length);

			memcpy(page.first_bytes + page.first_length, page.chunk, (size_t)first);
			page.first_length += first;
		}
		if (fwrite(page.chunk, (size_t)got, 1, page.file) != 1)
		{
			replay_page_close_client();
			return;
		}
		page.remaining -= got;
		budget -= got;
	}
	if (page.remaining <= 0)
		replay_page_upload_end();
}

/* the request's head, read as it comes */
static void replay_page_read(
	void)
{
	int room = REPLAY_PAGE_HEAD_SIZE - page.head_length;
	int got = room > 0 ? posix_socket_recv(page.client, page.head + page.head_length, room, 0) : 0;

	if (got < 0)
	{
		if (posix_socket_last_error() != REPLAY_PAGE_WOULD_BLOCK)
			replay_page_close_client();
		return;
	}
	if (got == 0 && room > 0)
	{
		replay_page_close_client();
		return;
	}
	page.head_length += got;
	page.head[page.head_length] = 0;
	page.active_at = system_milliseconds();
	if (strstr(page.head, "\r\n\r\n"))
		replay_page_answer();
	else if (page.head_length >= REPLAY_PAGE_HEAD_SIZE)
		replay_page_close_client();
}

void network_replay_page_wanted(
	void)
{
	if (!config_boolean("network.link_page"))
		return;
	page.wanted_at = system_milliseconds();
	if (page.listening < 0 && system_milliseconds() - page.looked_at >= REPLAY_PAGE_LOOK_MS)
	{
		page.looked_at = system_milliseconds();
		replay_page_open();
	}
}

void network_replay_page_update(
	void)
{
	unsigned long now = system_milliseconds();

	if (page.listening < 0)
		return;
	if (now - page.wanted_at > REPLAY_PAGE_WANTED_MS)
	{
		replay_page_close();
		return;
	}
	if (page.client >= 0)
	{
		if (now - page.active_at > REPLAY_PAGE_IDLE_MS)
		{
			replay_page_close_client();
			return;
		}
		switch (page.state)
		{
		case _replay_page_reading: replay_page_read(); break;
		case _replay_page_sending: replay_page_send(); break;
		case _replay_page_receiving: replay_page_receive(); break;
		}
		/* (an upload's answer, once it is whole) */
		if (page.client >= 0 && page.state == _replay_page_sending)
			replay_page_send();
		return;
	}
	if (now - page.looked_at < REPLAY_PAGE_LOOK_MS)
		return;
	page.looked_at = now;
	page.client = posix_socket_accept(page.listening, NULL, NULL);
	if (page.client >= 0)
	{
		posix_socket_set_nonblocking(page.client, 1);
		page.active_at = now;
		page.state = _replay_page_reading;
		page.head_length = 0;
		page.head[0] = 0;
		replay_page_read();
	}
}

char const *network_replay_page_address(
	void)
{
	return page.address;
}

#else

void network_replay_page_wanted(
	void)
{
}

void network_replay_page_update(
	void)
{
}

char const *network_replay_page_address(
	void)
{
	return "";
}

#endif
