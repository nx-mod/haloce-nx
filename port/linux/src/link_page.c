/*
LINK_PAGE.C

The Switch's and the PS5's way in for an invite link (network.link_page): the console has
no clipboard anything else can write to, no browser and no keyboard to paste
with, so while Direct Link's page is up (Join Game > Direct Link) the game
serves a small web page on the local network, shown on that page as a QR
code and an address (menu_functions.c). A phone or a computer opens it and
sends the link, and the game joins it as PASTE LINK does (p2p_join_invite).

Nothing of the game's network changes: a page and a link on the local
network, while the screen is up (link_page_wanted, called by it each frame);
nothing listens otherwise. One connection at a time, a request of a few KB
at most, and a few seconds for it, answered and closed. The listening socket
is looked at a few times a second, not each frame: on the Switch a socket's
call is a request to the system's service, on the PS5 a call to the host
(link_page_update, main_loop's).

The QR code is Project Nayuki's encoder (port/third_party/qrcodegen, MIT),
which only the Switch's and the PS5's builds have.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "p2p.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the screen's (menu_functions.c): the page while it is up */
void link_page_wanted(void);
/* main_loop's, each frame */
void link_page_update(void);
/* the page's address ("http://192.168.2.173:8080/"), "" while none is served */
char const *link_page_address(void);
/* the address's QR code: its side in modules (0 for none) and whether a module is dark */
int link_page_qr_size(void);
int link_page_qr_dark(int x, int y);
/* a QR code of any text (Server Setup's invite link): made, then as the address's */
int link_page_text_qr(char const *text);
int link_page_text_qr_dark(int x, int y);
/* a link it took, since this was last asked (1 once a link was sent and joined) */
int link_page_took_link(void);

#if defined(HALO_SWITCH) || defined(HALO_PS5)

#include "../../third_party/qrcodegen/qrcodegen.h"

/* the console, as the page names it */
#ifdef HALO_PS5
#define LINK_PAGE_CONSOLE "PS5"
#else
#define LINK_PAGE_CONSOLE "Switch"
#endif

unsigned long system_milliseconds(void);

enum
{
	LINK_PAGE_FIRST_PORT = 8080,
	LINK_PAGE_PORTS = 10,
	/* the page wanted this recently, or it closes */
	LINK_PAGE_WANTED_MS = 1000,
	/* how often an idle socket is looked at */
	LINK_PAGE_LOOK_MS = 250,
	LINK_PAGE_REQUEST_MS = 4000,
	LINK_PAGE_REQUEST_SIZE = 4096,
	/* (the address: version 3 holds 53 characters at medium correction) */
	LINK_PAGE_QR_VERSION = 4,
	LINK_PAGE_TEXT_QR_VERSION = 10,
};

static struct
{
	int listening;
	int client;
	unsigned short port;
	unsigned long wanted_at;
	unsigned long looked_at;
	unsigned long client_at;
	char request[LINK_PAGE_REQUEST_SIZE + 1];
	int request_length;
	char address[64];
	uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(LINK_PAGE_QR_VERSION)];
	int qr_ready;
	/* (any text's: an invite link, 76 characters, version 5) */
	uint8_t text_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(LINK_PAGE_TEXT_QR_VERSION)];
	int text_qr_ready;
	int took_link;
} link_page = { -1, -1 };

static unsigned short link_page_network_short(unsigned short value)
{
	return (unsigned short)((value >> 8) | (value << 8));
}

static void link_page_close_client(void)
{
	if (link_page.client >= 0)
		posix_socket_close(link_page.client);
	link_page.client = -1;
	link_page.request_length = 0;
}

static void link_page_close(void)
{
	link_page_close_client();
	if (link_page.listening >= 0)
	{
		posix_socket_close(link_page.listening);
		platform_log("link page: closed");
	}
	link_page.listening = -1;
	link_page.address[0] = 0;
	link_page.qr_ready = 0;
}

/* the socket, on the first free port from 8080, and the address and its code */
static void link_page_open(void)
{
	unsigned long local = posix_local_ipv4_address();
	unsigned char const *octets = (unsigned char const *)&local;
	struct sockaddr_in address;
	int port;

	if (!local)
		return;
	link_page.listening = posix_socket(AF_INET, SOCK_STREAM, 0);
	if (link_page.listening < 0)
		return;
	for (port = LINK_PAGE_FIRST_PORT; port < LINK_PAGE_FIRST_PORT + LINK_PAGE_PORTS; port++)
	{
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = link_page_network_short((unsigned short)port);
		address.sin_addr.s_addr = (uint32_t)local;
		if (posix_socket_bind(link_page.listening, &address, sizeof(address)) == 0)
			break;
	}
	if (port == LINK_PAGE_FIRST_PORT + LINK_PAGE_PORTS || posix_socket_listen(link_page.listening, 2) != 0 ||
		posix_socket_set_nonblocking(link_page.listening, 1) != 0)
	{
		platform_log("link page: no port to serve it on (error %d)", posix_socket_last_error());
		posix_socket_close(link_page.listening);
		link_page.listening = -1;
		return;
	}
	link_page.port = (unsigned short)port;
	snprintf(link_page.address, sizeof(link_page.address), "http://%u.%u.%u.%u:%d/", octets[0], octets[1], octets[2],
		octets[3], port);
	{
		uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(LINK_PAGE_QR_VERSION)];

		link_page.qr_ready = qrcodegen_encodeText(link_page.address, scratch, link_page.qr, qrcodegen_Ecc_MEDIUM,
			qrcodegen_VERSION_MIN, LINK_PAGE_QR_VERSION, qrcodegen_Mask_AUTO, true);
	}
	platform_log("link page: serving %s", link_page.address);
}

/* the text of a form's field, %-decoded ("link=halo%3A%2F%2Fjoin%2F..."), into value */
static int link_page_field(char const *form, char const *name, char *value, int size)
{
	size_t name_length = strlen(name);
	char const *field = form;
	int length = 0;

	while (field && *field)
	{
		if (!strncmp(field, name, name_length) && field[name_length] == '=')
			break;
		field = strchr(field, '&');
		if (field)
			field++;
	}
	if (!field || !*field)
		return 0;
	for (field += name_length + 1; *field && *field != '&' && length < size - 1; field++)
	{
		int character = (unsigned char)*field;

		if (character == '+')
			character = ' ';
		else if (character == '%' && field[1] && field[2])
		{
			char hex[3] = { field[1], field[2], 0 };

			character = (int)strtol(hex, NULL, 16);
			field += 2;
		}
		value[length++] = (char)character;
	}
	value[length] = 0;
	return length;
}

static void link_page_send(char const *status, char const *body)
{
	char response[4096];
	int length = snprintf(response, sizeof(response),
		"HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
		"Connection: close\r\n\r\n%s", status, (int)strlen(body), body);
	int sent = 0;

	while (sent < length)
	{
		int put = posix_socket_send(link_page.client, response + sent, length - sent, 0);

		if (put <= 0)
			break;
		sent += put;
	}
}

static void link_page_page(char const *message)
{
	char body[3072];

	snprintf(body, sizeof(body),
		"<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, "
		"initial-scale=1\"><title>Halo: join a game</title><style>body{font-family:system-ui,sans-serif;"
		"background:#0b1a33;color:#e8eef8;margin:0;padding:24px 16px}main{max-width:520px;margin:auto}"
		"h1{font-size:1.4em}textarea{width:100%%;box-sizing:border-box;height:6em;font-size:1em;border-radius:8px;"
		"padding:10px;border:1px solid #4a6a9a;background:#13284a;color:#fff}button{margin-top:12px;width:100%%;"
		"padding:14px;font-size:1.1em;border:0;border-radius:8px;background:#2896ff;color:#fff}p.note{color:#9fb6d8}"
		"p.message{background:#13284a;padding:10px;border-radius:8px}</style></head><body><main>"
		"<h1>Join a game on the " LINK_PAGE_CONSOLE "</h1>%s<form method=\"post\" action=\"/join\">"
		"<p>Paste the invite link (halo://join/...):</p><textarea name=\"link\" autofocus "
		"placeholder=\"halo://join/...\"></textarea><button type=\"submit\">Send to the " LINK_PAGE_CONSOLE "</button></form>"
		"<p class=\"note\">This page is the game's, on your local network, while its Direct Link page is open.</p>"
		"</main></body></html>",
		message);
	link_page_send("200 OK", body);
}

/* a request whole: the page, or a link sent (a form's POST, or GET /join?link=...) */
static void link_page_answer(void)
{
	char *request = link_page.request;
	char link[512];
	int is_join = !strncmp(request, "POST /join", 10) || !strncmp(request, "GET /join?", 10);

	if (!strncmp(request, "GET /favicon", 12))
	{
		link_page_send("404 Not Found", "");
		return;
	}
	if (!is_join)
	{
		link_page_page("");
		return;
	}
	{
		char *form = !strncmp(request, "POST", 4) ? strstr(request, "\r\n\r\n") : strchr(request, '?');
		char *end;

		if (form)
			form += form[0] == '?' ? 1 : 4;
		/* (a GET's query ends at the request line's space) */
		if (form && request[0] == 'G' && (end = strchr(form, ' ')) != NULL)
			*end = 0;
		if (!form || !link_page_field(form, "link", link, sizeof(link)))
		{
			link_page_page("<p class=\"message\">Paste a link first.</p>");
			return;
		}
	}
	{
		char *start = link;
		size_t length;

		while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
			start++;
		for (length = strlen(start); length && (start[length - 1] == ' ' || start[length - 1] == '\t' ||
			start[length - 1] == '\r' || start[length - 1] == '\n'); length--)
		{
			start[length - 1] = 0;
		}
		if (*start && p2p_join_invite(start))
		{
			platform_log("link page: an invite link came from the local network");
			link_page.took_link = 1;
			link_page_page("<p class=\"message\">Sent. The game appears on the " LINK_PAGE_CONSOLE "'s Direct Link page: "
				"pick it there to join.</p>");
		}
		else
		{
			link_page_page("<p class=\"message\">That is not an invite link this version of the game can join. "
				"It looks like halo://join/ and 64 letters and numbers.</p>");
		}
	}
}

/* the request read as it comes, then answered */
static void link_page_serve_client(void)
{
	posix_ulong available = 0;
	char *headers_end;

	if (posix_socket_bytes_available(link_page.client, &available) == 0 && available > 0)
	{
		int room = LINK_PAGE_REQUEST_SIZE - link_page.request_length;
		int got = room > 0 ? posix_socket_recv(link_page.client, link_page.request + link_page.request_length,
			room, 0) : 0;

		if (got > 0)
			link_page.request_length += got;
		link_page.request[link_page.request_length] = 0;
	}
	headers_end = strstr(link_page.request, "\r\n\r\n");
	if (headers_end)
	{
		/* (a POST's body too, as long as it says) */
		char const *length_header = strstr(link_page.request, "Content-Length:");
		long body = length_header && length_header < headers_end ? strtol(length_header + 15, NULL, 10) : 0;

		if (body < 0 || link_page.request_length >= (int)(headers_end + 4 - link_page.request) + body ||
			link_page.request_length >= LINK_PAGE_REQUEST_SIZE)
		{
			link_page_answer();
			link_page_close_client();
			return;
		}
	}
	if (link_page.request_length >= LINK_PAGE_REQUEST_SIZE ||
		system_milliseconds() - link_page.client_at > LINK_PAGE_REQUEST_MS)
	{
		link_page_close_client();
	}
}

void link_page_wanted(void)
{
	if (!config_boolean("network.link_page"))
		return;
	link_page.wanted_at = system_milliseconds();
	if (link_page.listening < 0 && system_milliseconds() - link_page.looked_at >= LINK_PAGE_LOOK_MS)
	{
		link_page.looked_at = system_milliseconds();
		link_page_open();
	}
}

void link_page_update(void)
{
	unsigned long now = system_milliseconds();

	if (link_page.listening < 0)
		return;
	if (now - link_page.wanted_at > LINK_PAGE_WANTED_MS)
	{
		link_page_close();
		return;
	}
	if (link_page.client >= 0)
	{
		link_page_serve_client();
		return;
	}
	if (now - link_page.looked_at < LINK_PAGE_LOOK_MS)
		return;
	link_page.looked_at = now;
	link_page.client = posix_socket_accept(link_page.listening, NULL, NULL);
	if (link_page.client >= 0)
	{
		posix_socket_set_nonblocking(link_page.client, 1);
		link_page.client_at = now;
		link_page.request_length = 0;
		link_page.request[0] = 0;
		link_page_serve_client();
	}
}

char const *link_page_address(void)
{
	return link_page.address;
}

int link_page_qr_size(void)
{
	return link_page.qr_ready ? qrcodegen_getSize(link_page.qr) : 0;
}

int link_page_qr_dark(int x, int y)
{
	return link_page.qr_ready && qrcodegen_getModule(link_page.qr, x, y);
}

int link_page_text_qr(char const *text)
{
	uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(LINK_PAGE_TEXT_QR_VERSION)];

	link_page.text_qr_ready = text && *text && qrcodegen_encodeText(text, scratch, link_page.text_qr,
		qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, LINK_PAGE_TEXT_QR_VERSION, qrcodegen_Mask_AUTO, true);
	return link_page.text_qr_ready ? qrcodegen_getSize(link_page.text_qr) : 0;
}

int link_page_text_qr_dark(int x, int y)
{
	return link_page.text_qr_ready && qrcodegen_getModule(link_page.text_qr, x, y);
}

int link_page_took_link(void)
{
	int took = link_page.took_link;

	link_page.took_link = 0;
	return took;
}

#else

void link_page_wanted(void)
{
}

void link_page_update(void)
{
}

char const *link_page_address(void)
{
	return "";
}

int link_page_qr_size(void)
{
	return 0;
}

int link_page_qr_dark(int x, int y)
{
	(void)x;
	(void)y;
	return 0;
}

int link_page_took_link(void)
{
	return 0;
}

int link_page_text_qr(char const *text)
{
	(void)text;
	return 0;
}

int link_page_text_qr_dark(int x, int y)
{
	(void)x;
	(void)y;
	return 0;
}

#endif
