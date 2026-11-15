/*
MAP_IMPORT_PAGE.C

The Switch's and the PS5's way to the map lists' IMPORT: neither has a file
picker, so while the IMPORT screen is up (map_import_page_wanted, called by
it each frame) the game serves a small web page on the local network, shown
there as a QR code and an address, as the Theater's replay page is
(network_replay_page.c; network.link_page turns both off). A phone or a
computer uploads Custom Edition maps (.map or .zip) and MCC packages (.zip)
through it (PUT /upload/<name>: the page's script sends each file as it is)
into the import inbox (port/linux/src/mcc_import.c), which the game takes
in as each one is whole; GET /status says how the import is going.

One connection at a time. An upload is read a piece at a time, across
frames (MAP_PAGE_FRAME_BYTES a frame at most), so the menus keep their pace,
into <name>.part, which the import passes over until it is whole. Sizes are
64-bit: an MCC package's .zip can pass 4 GB, and the guest's long is 32
bits. Nothing listens while the screen is down.
*/

#include "cseries.h"
#include "../src/posix.h" /* port: port/linux/src/posix.c */
#include "../src/mcc_import.h" /* port: port/linux/src/mcc_import.c */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int config_boolean(char const *name);
void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
FILE *platform_fopen_host(char const *path, char const *mode);
int platform_remove_host(char const *path);
int platform_rename_host(char const *old_path, char const *new_path);
/* menu_functions.c's: how the import is going, for the page */
void map_import_status(char *text, unsigned long size);

#if defined(HALO_SWITCH) || defined(HALO_PS5)

#ifdef HALO_PS5
#define MAP_PAGE_CONSOLE "PS5"
#else
#define MAP_PAGE_CONSOLE "Switch"
#endif

/* (posix.h's calls give Winsock's codes) */
#define MAP_PAGE_WOULD_BLOCK 10035
#define MAP_PAGE_AF_INET 2
#define MAP_PAGE_SOCK_STREAM 1
/* the largest file taken (as the import takes: 16 GB) */
#define MAP_PAGE_MAXIMUM_UPLOAD ((int64_t)16 << 30)

enum
{
	/* (after the replay page's ports, network_replay_page.c's 8090 to 8099) */
	MAP_PAGE_FIRST_PORT = 8100,
	MAP_PAGE_PORTS = 10,
	/* the screen wanted it this recently, or it closes */
	MAP_PAGE_WANTED_MS = 1000,
	/* how often an idle socket is looked at */
	MAP_PAGE_LOOK_MS = 250,
	/* a connection that does nothing this long is closed */
	MAP_PAGE_IDLE_MS = 15000,
	MAP_PAGE_HEAD_SIZE = 8192,
	/* the most of a response sent, or of an upload read, a frame */
	MAP_PAGE_FRAME_BYTES = 1024 * 1024,
	MAP_PAGE_CHUNK_SIZE = 64 * 1024,
	MAP_PAGE_PATH_SIZE = 1024,
	MAP_PAGE_NAME_SIZE = 128,
};

enum
{
	_map_page_reading,
	_map_page_sending,
	_map_page_receiving,
};

struct map_page_address
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
	char address[64];
	unsigned long wanted_at;
	unsigned long looked_at;
	unsigned long active_at;
	short state;
	/* the request's head (and an upload's first bytes), and an upload's
	pieces: on the heap from the page's opening, not the guest image's */
	char *head;
	int head_length;
	char *response;
	int response_length;
	int response_sent;
	/* an upload: its file, what is left of it, and where it goes */
	FILE *file;
	int64_t remaining;
	byte *chunk;
	char part_path[MAP_PAGE_PATH_SIZE];
	char final_path[MAP_PAGE_PATH_SIZE];
	char name[MAP_PAGE_NAME_SIZE];
} page = { -1, -1 };

static unsigned short map_page_network_short(unsigned short value)
{
	return (unsigned short)((value >> 8) | (value << 8));
}

static void map_page_close_client(void)
{
	if (page.client >= 0)
		posix_socket_close(page.client);
	page.client = -1;
	page.head_length = 0;
	free(page.response);
	page.response = NULL;
	page.response_length = page.response_sent = 0;
	if (page.file)
		fclose(page.file);
	page.file = NULL;
	page.remaining = 0;
	/* (an upload not finished is not taken in) */
	if (page.state == _map_page_receiving && page.part_path[0])
		platform_remove_host(page.part_path);
	page.part_path[0] = 0;
	page.state = _map_page_reading;
}

static void map_page_close(void)
{
	map_page_close_client();
	if (page.listening >= 0)
	{
		posix_socket_close(page.listening);
		platform_log("map import page: closed");
	}
	page.listening = -1;
	page.address[0] = 0;
}

static void map_page_open(void)
{
	unsigned long local = posix_local_ipv4_address();
	unsigned char const *octets = (unsigned char const *)&local;
	struct map_page_address address;
	int port;

	if (!local)
		return;
	if (!page.chunk && !(page.chunk = (byte *)malloc(MAP_PAGE_CHUNK_SIZE)))
		return;
	if (!page.head && !(page.head = (char *)malloc(MAP_PAGE_HEAD_SIZE + 1)))
		return;
	page.listening = posix_socket(MAP_PAGE_AF_INET, MAP_PAGE_SOCK_STREAM, 0);
	if (page.listening < 0)
		return;
	for (port = MAP_PAGE_FIRST_PORT; port < MAP_PAGE_FIRST_PORT + MAP_PAGE_PORTS; port++)
	{
		memset(&address, 0, sizeof(address));
		address.family = MAP_PAGE_AF_INET;
		address.port = map_page_network_short((unsigned short)port);
		address.address = local;
		if (posix_socket_bind(page.listening, &address, sizeof(address)) == 0)
			break;
	}
	if (port == MAP_PAGE_FIRST_PORT + MAP_PAGE_PORTS || posix_socket_listen(page.listening, 2) != 0 ||
		posix_socket_set_nonblocking(page.listening, 1) != 0)
	{
		platform_log("map import page: no port to serve it on (error %d)", posix_socket_last_error());
		posix_socket_close(page.listening);
		page.listening = -1;
		return;
	}
	snprintf(page.address, sizeof(page.address), "http://%u.%u.%u.%u:%d/", octets[0], octets[1], octets[2],
		octets[3], port);
	platform_log("map import page: serving %s", page.address);
}

/* a response to send, held whole */
static void map_page_respond(char const *status, char const *type, char const *body)
{
	size_t body_length = strlen(body);

	page.response = (char *)malloc(512 + body_length);
	if (!page.response)
	{
		map_page_close_client();
		return;
	}
	page.response_length = snprintf(page.response, 512,
		"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
		status, type, (unsigned long)body_length);
	memcpy(page.response + page.response_length, body, body_length);
	page.response_length += (int)body_length;
	page.response_sent = 0;
	page.state = _map_page_sending;
}

static void map_page_text(char const *status, char const *text)
{
	map_page_respond(status, "text/plain; charset=utf-8", text);
}

/* a file's name, %-decoded from a path's part (up to a space, '?' or the end), into name: FALSE for one that is not
a .map's or a .zip's, or has what a file system refuses (a folder's '/', control characters, leading dots) */
static boolean map_page_name(char const *path, char *name, int size)
{
	int length = 0, index;

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
	if (length < 5 || name[0] == '.' || name[0] == ' ')
		return FALSE;
	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)name[index];

		if (character < 0x20 || character > 0x7E || strchr("\\/:*?\"<>|", character))
			return FALSE;
	}
	return !csstrcasecmp(name + length - 4, ".map") || !csstrcasecmp(name + length - 4, ".zip");
}

/* GET /: the page, with its uploader */
static void map_page_index(void)
{
	map_page_respond("200 OK", "text/html; charset=utf-8",
		"<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, "
		"initial-scale=1\"><title>Halo: import maps</title><style>body{font-family:system-ui,sans-serif;"
		"background:#0b1a33;color:#e8eef8;margin:0;padding:24px 16px}main{max-width:640px;margin:auto}"
		"h1{font-size:1.4em}button{padding:12px 18px;font-size:1em;border:0;border-radius:8px;background:#2896ff;"
		"color:#fff;margin-top:10px}p.note{color:#9fb6d8}#status{white-space:pre-wrap}progress{width:100%}</style>"
		"</head><body><main><h1>Import maps to the " MAP_PAGE_CONSOLE "</h1>"
		"<p class=\"note\">Custom Edition maps: .map files, or .zip files of them. MCC maps: their package's .zip "
		"(a Workshop package with its ModInfo.json, or MCC's own halo1/maps resources).</p>"
		"<input type=\"file\" id=\"files\" multiple accept=\".map,.zip\"><br>"
		"<button id=\"send\" onclick=\"send()\">Upload</button><p><progress id=\"bar\" max=\"1\" value=\"0\"></progress>"
		"</p><p id=\"status\"></p><p class=\"note\">This page is the game's, on your local network, while its IMPORT "
		"screen is open. Answer the game's questions on the " MAP_PAGE_CONSOLE ".</p>"
		"<script>const status=document.getElementById('status'),bar=document.getElementById('bar');"
		"function put(file){return new Promise((done,failed)=>{const request=new XMLHttpRequest();"
		"request.open('PUT','/upload/'+encodeURIComponent(file.name));"
		"request.upload.onprogress=e=>{if(e.lengthComputable)bar.value=e.loaded/e.total;};"
		"request.onload=()=>done(request.responseText);request.onerror=()=>failed('the connection was lost');"
		"request.send(file);});}"
		"async function send(){document.getElementById('send').disabled=true;"
		"for(const file of document.getElementById('files').files){status.textContent='Sending '+file.name+'...';"
		"bar.value=0;try{status.textContent=file.name+': '+await put(file);}catch(e){status.textContent=file.name+': '+e;}}"
		"document.getElementById('send').disabled=false;watch();}"
		"async function watch(){for(let i=0;i<600;i++){try{const text=await(await fetch('/status')).text();"
		"if(text)status.textContent=text;}catch(e){}await new Promise(r=>setTimeout(r,1000));}}"
		"</script></main></body></html>");
}

/* PUT /upload/<name>: its body to <name>.part in the inbox, then <name> (a number added if that is taken) */
static void map_page_upload_begin(char const *path, char const *body, int body_received)
{
	char const *length_header = strstr(page.head, "Content-Length:");
	int64_t length = length_header ? strtoll(length_header + 15, NULL, 10) : -1;
	char inbox[MAP_PAGE_PATH_SIZE], stem[MAP_PAGE_NAME_SIZE];
	struct posix_file_information information;
	int number;

	if (!length_header)
		length_header = strstr(page.head, "content-length:"), length = length_header ? strtoll(length_header + 15, NULL, 10) : -1;
	if (!map_page_name(path, page.name, sizeof(page.name)))
	{
		map_page_text("400 Bad Request", "Only .map and .zip files can be imported.");
		return;
	}
	if (length < 1 || length > MAP_PAGE_MAXIMUM_UPLOAD)
	{
		map_page_text("400 Bad Request", "It is empty, or larger than 16 GB.");
		return;
	}
	if (!mcc_import_inbox(inbox, sizeof(inbox)))
	{
		map_page_text("500 Internal Server Error", "The import folder cannot be made.");
		return;
	}
	snprintf(stem, sizeof(stem), "%.*s", (int)strlen(page.name) - 4, page.name);
	snprintf(page.final_path, sizeof(page.final_path), "%s/%s", inbox, page.name);
	for (number = 2; posix_stat(page.final_path, &information) == 0 && number < 100; number++)
		snprintf(page.final_path, sizeof(page.final_path), "%s/%s (%d)%s", inbox, stem, number, page.name + strlen(stem));
	snprintf(page.part_path, sizeof(page.part_path), "%s.part", page.final_path);
	page.file = platform_fopen_host(page.part_path, "wb");
	if (!page.file)
	{
		page.part_path[0] = 0;
		map_page_text("500 Internal Server Error", "It cannot be written (is the card full?).");
		return;
	}
	page.state = _map_page_receiving;
	page.remaining = length;
	if (body_received > length)
		body_received = (int)length;
	if (body_received > 0)
	{
		if (fwrite(body, (size_t)body_received, 1, page.file) != 1)
		{
			map_page_close_client();
			return;
		}
		page.remaining -= body_received;
	}
	platform_log("map import page: receiving %s (%lu MB)", page.name, (unsigned long)(length >> 20));
}

/* an upload whole: its file named, for the import to take in */
static void map_page_upload_end(void)
{
	boolean written = fclose(page.file) == 0;

	page.file = NULL;
	page.state = _map_page_reading;
	if (!written || platform_rename_host(page.part_path, page.final_path) != 0)
	{
		platform_remove_host(page.part_path);
		page.part_path[0] = 0;
		map_page_text("500 Internal Server Error", "It cannot be kept (is the card full?).");
		return;
	}
	page.part_path[0] = 0;
	platform_log("map import page: received %s", page.name);
	map_page_text("200 OK", "Received: the game is importing it.");
}

/* GET /status: how the import is going */
static void map_page_status(void)
{
	char text[MCC_IMPORT_TEXT_SIZE];

	map_import_status(text, sizeof(text));
	map_page_text("200 OK", text);
}

static void map_page_answer(void)
{
	char *body = strstr(page.head, "\r\n\r\n") + 4;
	int body_received = page.head_length - (int)(body - page.head);

	if (!strncmp(page.head, "PUT /upload/", 12))
		map_page_upload_begin(page.head + 12, body, body_received);
	else if (!strncmp(page.head, "GET /status", 11))
		map_page_status();
	else if (!strncmp(page.head, "GET / ", 6) || !strncmp(page.head, "GET /?", 6))
		map_page_index();
	else
		map_page_text("404 Not Found", "Not found.");
}

static void map_page_send(void)
{
	int budget = MAP_PAGE_FRAME_BYTES;

	while (budget > 0 && page.response_sent < page.response_length)
	{
		int put = posix_socket_send(page.client, page.response + page.response_sent,
			MIN(page.response_length - page.response_sent, budget), 0);

		if (put < 0)
		{
			if (posix_socket_last_error() != MAP_PAGE_WOULD_BLOCK)
				map_page_close_client();
			return;
		}
		if (put == 0)
			return;
		page.active_at = system_milliseconds();
		budget -= put;
		page.response_sent += put;
	}
	if (page.response_sent >= page.response_length)
		map_page_close_client();
}

static void map_page_receive(void)
{
	int budget = MAP_PAGE_FRAME_BYTES;

	while (budget > 0 && page.remaining > 0)
	{
		int want = page.remaining < MAP_PAGE_CHUNK_SIZE ? (int)page.remaining : MAP_PAGE_CHUNK_SIZE;
		int got = posix_socket_recv(page.client, page.chunk, want, 0);

		if (got < 0)
		{
			if (posix_socket_last_error() != MAP_PAGE_WOULD_BLOCK)
				map_page_close_client();
			return;
		}
		if (got == 0)
		{
			/* (closed before the end) */
			map_page_close_client();
			return;
		}
		page.active_at = system_milliseconds();
		if (fwrite(page.chunk, (size_t)got, 1, page.file) != 1)
		{
			map_page_close_client();
			return;
		}
		page.remaining -= got;
		budget -= got;
	}
	if (page.remaining <= 0)
		map_page_upload_end();
}

static void map_page_read(void)
{
	int room = MAP_PAGE_HEAD_SIZE - page.head_length;
	int got = room > 0 ? posix_socket_recv(page.client, page.head + page.head_length, room, 0) : 0;

	if (got < 0)
	{
		if (posix_socket_last_error() != MAP_PAGE_WOULD_BLOCK)
			map_page_close_client();
		return;
	}
	if (got == 0 && room > 0)
	{
		map_page_close_client();
		return;
	}
	page.head_length += got;
	page.head[page.head_length] = 0;
	page.active_at = system_milliseconds();
	if (strstr(page.head, "\r\n\r\n"))
		map_page_answer();
	else if (page.head_length >= MAP_PAGE_HEAD_SIZE)
		map_page_close_client();
}

void map_import_page_wanted(void)
{
	if (!config_boolean("network.link_page"))
		return;
	page.wanted_at = system_milliseconds();
	if (page.listening < 0 && system_milliseconds() - page.looked_at >= MAP_PAGE_LOOK_MS)
	{
		page.looked_at = system_milliseconds();
		map_page_open();
	}
}

void map_import_page_update(void)
{
	unsigned long now = system_milliseconds();

	if (page.listening < 0)
		return;
	if (now - page.wanted_at > MAP_PAGE_WANTED_MS)
	{
		map_page_close();
		return;
	}
	if (page.client >= 0)
	{
		if (now - page.active_at > MAP_PAGE_IDLE_MS)
		{
			map_page_close_client();
			return;
		}
		switch (page.state)
		{
		case _map_page_reading: map_page_read(); break;
		case _map_page_sending: map_page_send(); break;
		case _map_page_receiving: map_page_receive(); break;
		}
		if (page.client >= 0 && page.state == _map_page_sending)
			map_page_send();
		return;
	}
	if (now - page.looked_at < MAP_PAGE_LOOK_MS)
		return;
	page.looked_at = now;
	page.client = posix_socket_accept(page.listening, NULL, NULL);
	if (page.client >= 0)
	{
		posix_socket_set_nonblocking(page.client, 1);
		page.active_at = now;
		page.state = _map_page_reading;
		page.head_length = 0;
		page.head[0] = 0;
		map_page_read();
	}
}

char const *map_import_page_address(void)
{
	return page.address;
}

#else

void map_import_page_wanted(void)
{
}

void map_import_page_update(void)
{
}

char const *map_import_page_address(void)
{
	return "";
}

#endif
