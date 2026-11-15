/*
NETWORK_EXTENSIONS.C

Capabilities beyond the shared OpenCE protocol, told as strings
(network_extensions.h, docs/network_extensions.md).

The message, game packet NETWORK_EXTENSIONS_PACKET_TYPE, is framed here
(create_message), outside the shared message tables, so that nothing an
OpenCE machine reads changes. Its bytes, before the type byte that ends
every game packet: the magic "SLXT", the format (1), the kind (0 the host's
announcement, 1 a client's answer), the count of entries, then each entry:
its key's length (1 to 63), the key (ASCII letters, digits, '_' and '.'),
its version (16 bits, little-endian), its flags (bit 0: the game requires
it), and its text: a length (0 to 127) and printable ASCII, what a machine
without it is to be told (the host's; empty in an answer). Keys a machine
does not know are skipped, but for their text when the game requires them;
a message that does not read whole is dropped.

A client whose host's game requires a capability it has not (a key it does
not know, or an older version) leaves the game itself, with the host's text
for it in an error box (client_cannot_play), as map sharing's failures do:
the host would remove it anyway, and its notice is not shown in the lobby
(network_distributed.c takes distributed messages in a game only).
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "network_extensions.h"
#include "bungie_net/common/message_header.h"
#include "halo_port_limits.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* the networking's (source/networking, port/linux/game) */
boolean network_distributed_server_send_to_machine_reliably(long machine_index, void *message, unsigned short size);
boolean network_distributed_client_send_reliably(void *message, unsigned short size);
boolean network_extensions_server_machine_remote(long machine_index);
boolean network_extensions_server_send_settings(long machine_index);
void network_game_server_kick_machine(long machine_index, boolean kept_out);
void distributed_send_notice_to_machine(long machine_index, char const *text);
unsigned long system_milliseconds(void);
void display_error_text_when_main_menu_loaded(wchar_t const *text);
void network_game_abort(void);
char const *config_string(char const *name);
void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	/* the transport's type of a game packet (bungie_net's message header) */
	EXTENSIONS_MESSAGE_TYPE_PACKET = 3,
	EXTENSIONS_FORMAT = 1,
	EXTENSIONS_KIND_ANNOUNCEMENT = 0,
	EXTENSIONS_KIND_ANSWER = 1,
	/* a capability's own data: the key's length and the key, then its bytes
	(network_extensions_send_data) */
	EXTENSIONS_KIND_DATA = 2,
	EXTENSIONS_FLAG_REQUIRED = 1 << 0,
	MAXIMUM_KEY_LENGTH = 63,
	MAXIMUM_TEXT_LENGTH = 127,
	MAXIMUM_MESSAGE_BYTES = 1024,
	MAXIMUM_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	/* how long the host waits for a machine's answer, when its game
	requires a capability */
	ANSWER_MILLISECONDS = 5000,
	/* how long the notice is up before the machine is removed: an OpenCE
	client shows it as a chat line, and its removal as the generic "failed
	to join", which it has no text of the host's for */
	NOTICE_MILLISECONDS = 3000,
};

#define EXTENSIONS_MAGIC "SLXT"
#define EXTENSIONS_MAGIC_LENGTH 4

/* ---------- this build's capabilities */

/* map_share.c's: a game on an Xbox multiplayer map of custom_maps */
boolean custom_xbox_multiplayer_maps_required(void);

struct capability
{
	char const *key;
	unsigned short version;
	/* what a machine without it is told, removed from a game that
	requires it */
	char const *missing_notice;
	/* whether the host's current game requires it (NULL: none does yet) */
	boolean (*required)(void);
};

/* mcc_share.c's: a capability's data from the host (machine_index NONE) or a
client */
void mcc_share_file_share_data(long machine_index, void const *payload, long size);

static struct capability const capabilities[] =
{
	{ NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1,
		"You need a client with support for custom OG Xbox multiplayer maps.", custom_xbox_multiplayer_maps_required },
	{ NETWORK_CAPABILITY_FILE_SHARE_TCP, 1, "", NULL },
};

/* each capability's data handler (EXTENSIONS_KIND_DATA), or NULL */
static void (*const data_handlers[])(long machine_index, void const *payload, long size) =
{
	NULL,
	mcc_share_file_share_data,
};
typedef char data_handlers_match[sizeof(data_handlers) / sizeof(data_handlers[0]) ==
	sizeof(capabilities) / sizeof(capabilities[0]) ? 1 : -1];

#define CAPABILITY_COUNT ((long)(sizeof(capabilities) / sizeof(capabilities[0])))

/* ---------- a machine's capabilities, as its message reads

Of this build's table alone (a key this build does not know is of no use to
it): each capability's version the machine has (0 for none), and its flags. */

struct peer_capabilities
{
	short count;
	/* (an announcement's) the first capability the game requires that this
	build has not, or has an older version of: its text, for the player */
	boolean missing_required;
	char missing_key[MAXIMUM_KEY_LENGTH + 1];
	char missing_text[MAXIMUM_TEXT_LENGTH + 1];
	unsigned short versions[sizeof(capabilities) / sizeof(capabilities[0])];
	byte flags[sizeof(capabilities) / sizeof(capabilities[0])];
};

/* ---------- globals */

static struct
{
	/* what the game required when every machine was last announced to:
	a bit for each capability of the table */
	unsigned long announced_required;
	boolean announced_once;
	struct
	{
		boolean present;
		unsigned long announced_time;
		boolean answered;
		boolean removed;
		boolean kicked;
		unsigned long removed_time;
		struct peer_capabilities capabilities;
	} machines[MAXIMUM_MACHINES];
} host;

static struct
{
	boolean announced;
	/* (the game left: client_cannot_play, once) */
	boolean left;
	struct peer_capabilities capabilities;
} client;

/* ---------- private */

/* whether the game requires the table's capability `index`: its feature's
answer, or debug.network_extensions_require (keys, separated by commas or
spaces, for testing a host that requires them) */
static boolean capability_required(
	long index)
{
	char const *forced = config_string("debug.network_extensions_require");
	size_t length = strlen(capabilities[index].key);

	if (capabilities[index].required && capabilities[index].required())
		return TRUE;
	while (forced && *forced)
	{
		size_t word = strcspn(forced, ", ");

		if (word == length && !strncmp(forced, capabilities[index].key, length))
			return TRUE;
		forced += word;
		forced += strspn(forced, ", ");
	}

	return FALSE;
}

static unsigned long required_now(
	void)
{
	unsigned long required = 0;
	long index;

	for (index = 0; index < CAPABILITY_COUNT && index < 32; index++)
	{
		if (capability_required(index))
			required |= 1UL << index;
	}

	return required;
}

static boolean key_valid(
	char const *key,
	long length)
{
	long index;

	if (length < 1 || length > MAXIMUM_KEY_LENGTH)
		return FALSE;
	for (index = 0; index < length; index++)
	{
		char c = key[index];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.'))
			return FALSE;
	}

	return TRUE;
}

/* the table's index of the capability `key` (`length` characters), or NONE */
static long capability_index(
	char const *key,
	long length)
{
	long index;

	for (index = 0; index < CAPABILITY_COUNT; index++)
	{
		if ((long)strlen(capabilities[index].key) == length && !strncmp(capabilities[index].key, key, (size_t)length))
			return index;
	}

	return NONE;
}

/* a message of `kind` with this build's capabilities, those of `required`
flagged, sent to `machine_index` (the host) or the host (NONE) */
static boolean extensions_send(
	byte kind,
	unsigned long required,
	long machine_index)
{
	byte data[MAXIMUM_MESSAGE_BYTES];
	byte buffer[MAXIMUM_MESSAGE_BYTES + 16];
	long size = 0;
	long index;
	void *message;
	unsigned short message_size;

	memcpy(data, EXTENSIONS_MAGIC, EXTENSIONS_MAGIC_LENGTH);
	size = EXTENSIONS_MAGIC_LENGTH;
	data[size++] = EXTENSIONS_FORMAT;
	data[size++] = kind;
	data[size++] = (byte)CAPABILITY_COUNT;
	for (index = 0; index < CAPABILITY_COUNT; index++)
	{
		long length = (long)strlen(capabilities[index].key);
		/* (the host's text for a machine without it; none in an answer) */
		char const *text = kind == EXTENSIONS_KIND_ANNOUNCEMENT ? capabilities[index].missing_notice : "";
		long text_length = (long)strlen(text);

		if (text_length > MAXIMUM_TEXT_LENGTH)
			text_length = MAXIMUM_TEXT_LENGTH;
		if (size + 1 + length + 3 + 1 + text_length + 1 > (long)sizeof(data))
			return FALSE;
		data[size++] = (byte)length;
		memcpy(data + size, capabilities[index].key, (size_t)length);
		size += length;
		data[size++] = (byte)(capabilities[index].version & 0xFF);
		data[size++] = (byte)(capabilities[index].version >> 8);
		data[size++] = (byte)((required >> index) & 1 ? EXTENSIONS_FLAG_REQUIRED : 0);
		data[size++] = (byte)text_length;
		memcpy(data + size, text, (size_t)text_length);
		size += text_length;
	}
	data[size++] = NETWORK_EXTENSIONS_PACKET_TYPE;
	message = create_message(EXTENSIONS_MESSAGE_TYPE_PACKET, data, (unsigned long)size, buffer, sizeof(buffer));
	if (!message)
		return FALSE;
	message_size = (unsigned short)GET_MESSAGE_SIZE(*(unsigned short *)message);

	return machine_index == NONE ?
		network_distributed_client_send_reliably(message, message_size) :
		network_distributed_server_send_to_machine_reliably(machine_index, message, message_size);
}

/* a message's capabilities, of `kind`; FALSE if it is not one of ours, or
does not read whole */
static boolean extensions_read(
	void const *data,
	long size,
	byte kind,
	struct peer_capabilities *result)
{
	byte const *bytes = data;
	long at, count, index;

	/* (the type byte that ends the packet is not the message's) */
	size--;
	if (size < EXTENSIONS_MAGIC_LENGTH + 3 || memcmp(bytes, EXTENSIONS_MAGIC, EXTENSIONS_MAGIC_LENGTH) ||
		bytes[EXTENSIONS_MAGIC_LENGTH] != EXTENSIONS_FORMAT || bytes[EXTENSIONS_MAGIC_LENGTH + 1] != kind)
	{
		return FALSE;
	}
	count = bytes[EXTENSIONS_MAGIC_LENGTH + 2];
	at = EXTENSIONS_MAGIC_LENGTH + 3;
	memset(result, 0, sizeof(*result));
	for (index = 0; index < count; index++)
	{
		long length;

		if (at >= size)
			return FALSE;
		long text_length;
		long known;
		unsigned short version;
		byte flags;

		length = bytes[at++];
		/* (the key, version, flags and the text's length) */
		if (at + length + 4 > size || !key_valid((char const *)bytes + at, length))
			return FALSE;
		text_length = bytes[at + length + 3];
		if (text_length > MAXIMUM_TEXT_LENGTH || at + length + 4 + text_length > size)
			return FALSE;
		known = capability_index((char const *)bytes + at, length);
		version = (unsigned short)(bytes[at + length] | (bytes[at + length + 1] << 8));
		flags = bytes[at + length + 2];
		if (known != NONE)
		{
			result->versions[known] = version;
			result->flags[known] = flags;
		}
		/* (a capability the game requires that this build has not: kept with
		its text, printable) */
		if ((flags & EXTENSIONS_FLAG_REQUIRED) && !result->missing_required &&
			(known == NONE || capabilities[known].version < version))
		{
			long character;

			result->missing_required = TRUE;
			memcpy(result->missing_key, bytes + at, (size_t)length);
			result->missing_key[length] = 0;
			for (character = 0; character < text_length; character++)
			{
				byte c = bytes[at + length + 4 + character];

				result->missing_text[character] = c >= 32 && c <= 126 ? (char)c : '?';
			}
			result->missing_text[text_length] = 0;
		}
		result->count++;
		at += length + 4 + text_length;
	}

	return at == size;
}


/* the host: the table's first capability the game requires that the
machine has not answered with (of its version or later), or NONE */
static long machine_missing(
	long machine_index,
	unsigned long required)
{
	long index;

	for (index = 0; index < CAPABILITY_COUNT; index++)
	{
		if (((required >> index) & 1) &&
			host.machines[machine_index].capabilities.versions[index] < capabilities[index].version)
		{
			return index;
		}
	}

	return NONE;
}

static void machine_remove(
	long machine_index,
	long capability_index)
{
	host.machines[machine_index].removed = TRUE;
	host.machines[machine_index].removed_time = system_milliseconds();
	error(_error_silent, "network extensions: machine #%ld %s %s: removed", machine_index,
		host.machines[machine_index].answered ? "has not got" : "did not answer for", capabilities[capability_index].key);
	platform_log("network extensions: machine #%ld %s %s: removed", machine_index,
		host.machines[machine_index].answered ? "has not got" : "did not answer for", capabilities[capability_index].key);
	distributed_send_notice_to_machine(machine_index, capabilities[capability_index].missing_notice);
}

/* ---------- public, the host */

void network_extensions_host_machine_accepted(
	long machine_index)
{
	if (machine_index < 0 || machine_index >= MAXIMUM_MACHINES)
		return;
	memset(&host.machines[machine_index], 0, sizeof(host.machines[machine_index]));
	host.machines[machine_index].present = TRUE;
	host.machines[machine_index].announced_time = system_milliseconds();
	if (!host.announced_once)
	{
		host.announced_required = required_now();
		host.announced_once = TRUE;
	}
	extensions_send(EXTENSIONS_KIND_ANNOUNCEMENT, host.announced_required, machine_index);
}

void network_extensions_host_update(
	void)
{
	unsigned long required = required_now();
	unsigned long now = system_milliseconds();
	long machine_index;

	for (machine_index = 0; machine_index < MAXIMUM_MACHINES; machine_index++)
	{
		if (host.machines[machine_index].present && !network_extensions_server_machine_remote(machine_index))
			memset(&host.machines[machine_index], 0, sizeof(host.machines[machine_index]));
	}
	/* (what the game requires changed: every machine told again; their
	answers stand, an answer listing all the machine has) */
	if (host.announced_once && required != host.announced_required)
	{
		host.announced_required = required;
		for (machine_index = 0; machine_index < MAXIMUM_MACHINES; machine_index++)
		{
			if (host.machines[machine_index].present && !host.machines[machine_index].removed)
				extensions_send(EXTENSIONS_KIND_ANNOUNCEMENT, required, machine_index);
		}
	}
	/* (the machines told, removed once the notice has been up a while) */
	for (machine_index = 0; machine_index < MAXIMUM_MACHINES; machine_index++)
	{
		if (host.machines[machine_index].present && host.machines[machine_index].removed &&
			!host.machines[machine_index].kicked &&
			now - host.machines[machine_index].removed_time >= NOTICE_MILLISECONDS)
		{
			host.machines[machine_index].kicked = TRUE;
			network_game_server_kick_machine(machine_index, FALSE);
		}
	}
	if (!required)
		return;
	for (machine_index = 0; machine_index < MAXIMUM_MACHINES; machine_index++)
	{
		long missing;

		if (!host.machines[machine_index].present || host.machines[machine_index].removed)
			continue;
		missing = machine_missing(machine_index, required);
		if (missing == NONE)
			continue;
		if (host.machines[machine_index].answered ||
			now - host.machines[machine_index].announced_time >= ANSWER_MILLISECONDS)
		{
			machine_remove(machine_index, missing);
		}
	}
}

boolean network_extensions_host_machine_held(
	long machine_index)
{
	unsigned long required;

	if (machine_index < 0 || machine_index >= MAXIMUM_MACHINES || !host.machines[machine_index].present)
		return FALSE;
	required = required_now();

	return required && (!host.machines[machine_index].answered || machine_missing(machine_index, required) != NONE);
}

/* a capability's data (EXTENSIONS_KIND_DATA), from the host (machine_index
NONE) or a client: handed to its handler where the capability is agreed;
FALSE if the message is not of that kind */
static boolean data_handle(
	long machine_index,
	void const *data,
	long size)
{
	byte const *bytes = data;
	long key_length, index;
	boolean agreed;

	/* (the type byte that ends the packet is not the message's) */
	size--;
	if (size < EXTENSIONS_MAGIC_LENGTH + 3 || memcmp(bytes, EXTENSIONS_MAGIC, EXTENSIONS_MAGIC_LENGTH) ||
		bytes[EXTENSIONS_MAGIC_LENGTH] != EXTENSIONS_FORMAT || bytes[EXTENSIONS_MAGIC_LENGTH + 1] != EXTENSIONS_KIND_DATA)
	{
		return FALSE;
	}
	key_length = bytes[EXTENSIONS_MAGIC_LENGTH + 2];
	if (key_length < 1 || key_length > MAXIMUM_KEY_LENGTH || EXTENSIONS_MAGIC_LENGTH + 3 + key_length > size)
		return TRUE;
	index = capability_index((char const *)bytes + EXTENSIONS_MAGIC_LENGTH + 3, key_length);
	if (index == NONE || !data_handlers[index])
		return TRUE;
	agreed = machine_index == NONE ?
		network_extension_host_has(capabilities[index].key, 1) :
		network_extension_agreed(machine_index, capabilities[index].key, 1);
	if (agreed)
		data_handlers[index](machine_index, bytes + EXTENSIONS_MAGIC_LENGTH + 3 + key_length,
			size - (EXTENSIONS_MAGIC_LENGTH + 3 + key_length));
	return TRUE;
}

boolean network_extensions_send_data(
	long machine_index,
	char const *key,
	void const *payload,
	long size)
{
	byte data[MAXIMUM_MESSAGE_BYTES];
	byte buffer[MAXIMUM_MESSAGE_BYTES + 16];
	long key_length = key ? (long)strlen(key) : 0;
	long index = capability_index(key, key_length);
	long at;
	void *message;
	unsigned short message_size;

	if (index == NONE || size < 0 || EXTENSIONS_MAGIC_LENGTH + 3 + key_length + size + 1 > (long)sizeof(data) ||
		(machine_index == NONE ? !network_extension_host_has(key, 1) : !network_extension_agreed(machine_index, key, 1)))
	{
		return FALSE;
	}
	memcpy(data, EXTENSIONS_MAGIC, EXTENSIONS_MAGIC_LENGTH);
	at = EXTENSIONS_MAGIC_LENGTH;
	data[at++] = EXTENSIONS_FORMAT;
	data[at++] = EXTENSIONS_KIND_DATA;
	data[at++] = (byte)key_length;
	memcpy(data + at, key, (size_t)key_length);
	at += key_length;
	memcpy(data + at, payload, (size_t)size);
	at += size;
	data[at++] = NETWORK_EXTENSIONS_PACKET_TYPE;
	message = create_message(EXTENSIONS_MESSAGE_TYPE_PACKET, data, (unsigned long)at, buffer, sizeof(buffer));
	if (!message)
		return FALSE;
	message_size = (unsigned short)GET_MESSAGE_SIZE(*(unsigned short *)message);
	return machine_index == NONE ?
		network_distributed_client_send_reliably(message, message_size) :
		network_distributed_server_send_to_machine_reliably(machine_index, message, message_size);
}

void network_extensions_host_handle(
	long machine_index,
	void const *data,
	long size)
{
	struct peer_capabilities read;
	boolean was_held = network_extensions_host_machine_held(machine_index);

	if (machine_index >= 0 && machine_index < MAXIMUM_MACHINES && host.machines[machine_index].present &&
		data_handle(machine_index, data, size))
	{
		return;
	}

	if (machine_index < 0 || machine_index >= MAXIMUM_MACHINES || !host.machines[machine_index].present ||
		!extensions_read(data, size, EXTENSIONS_KIND_ANSWER, &read))
	{
		return;
	}
	host.machines[machine_index].capabilities = read;
	host.machines[machine_index].answered = TRUE;
	error(_error_silent, "network extensions: machine #%ld answered with %d capabilities", machine_index, read.count);
	/* (the game's settings, held from it until now; one without a required
	capability is removed instead, network_extensions_host_update) */
	if (was_held && !network_extensions_host_machine_held(machine_index))
		network_extensions_server_send_settings(machine_index);
}

boolean network_extension_agreed(
	long machine_index,
	char const *key,
	unsigned short version)
{
	long index = capability_index(key, key ? (long)strlen(key) : 0);

	if (machine_index < 0 || machine_index >= MAXIMUM_MACHINES || !host.machines[machine_index].answered ||
		index == NONE)
	{
		return FALSE;
	}

	return host.machines[machine_index].capabilities.versions[index] >= version &&
		host.machines[machine_index].capabilities.versions[index] != 0;
}

/* ---------- the client */

/* the host's game requires a capability this build has not: the player told
why (the host's text for it, else its key), and the game left, as map
sharing leaves one it cannot download (map_share.c's download_fail) */
static void client_cannot_play(
	struct peer_capabilities const *read)
{
	char message[MAXIMUM_TEXT_LENGTH + MAXIMUM_KEY_LENGTH + 96];
	wchar_t text[sizeof(message)];
	long index;

	if (client.left)
		return;
	client.left = TRUE;
	if (read->missing_text[0])
		snprintf(message, sizeof(message), "%s", read->missing_text);
	else
		snprintf(message, sizeof(message), "This game needs a capability your game does not have (%s).",
			read->missing_key);
	error(_error_silent, "network extensions: the host's game requires %s, which this build has not: left",
		read->missing_key);
	platform_log("network extensions: the host's game requires %s, which this build has not: left", read->missing_key);
	for (index = 0; message[index] && index < (long)(sizeof(text) / sizeof(text[0])) - 1; index++)
		text[index] = (wchar_t)(unsigned char)message[index];
	text[index] = 0;
	display_error_text_when_main_menu_loaded(text);
	network_game_abort();
}

/* ---------- public, the client */

void network_extensions_client_reset(
	void)
{
	memset(&client, 0, sizeof(client));
}

void network_extensions_client_handle(
	void const *data,
	long size)
{
	struct peer_capabilities read;

	if (data_handle(NONE, data, size))
		return;
	if (!extensions_read(data, size, EXTENSIONS_KIND_ANNOUNCEMENT, &read))
		return;
	client.capabilities = read;
	client.announced = TRUE;
	error(_error_silent, "network extensions: the host announced %d capabilities", read.count);
	extensions_send(EXTENSIONS_KIND_ANSWER, 0, NONE);
	if (read.missing_required)
		client_cannot_play(&read);
}

boolean network_extension_host_has(
	char const *key,
	unsigned short version)
{
	long index = capability_index(key, key ? (long)strlen(key) : 0);

	if (!client.announced || index == NONE)
		return FALSE;

	return client.capabilities.versions[index] >= version && client.capabilities.versions[index] != 0;
}

boolean network_extension_host_requires(
	char const *key)
{
	long index = capability_index(key, key ? (long)strlen(key) : 0);

	if (!client.announced || index == NONE)
		return FALSE;

	return client.capabilities.versions[index] != 0 && (client.capabilities.flags[index] & EXTENSIONS_FLAG_REQUIRED);
}
