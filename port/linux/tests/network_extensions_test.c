/*
NETWORK_EXTENSIONS_TEST.C

Tests of the capabilities' handshake (port/linux/game/network_extensions.c,
docs/network_extensions.md): the message's bytes, what a machine reads of
another's (keys it does not know skipped, a message that does not read
whole dropped), the host's removal of a machine that does not answer in
time or answers without a required capability (told, then removed), and
nothing removed when the game requires nothing. Built with the stub headers
beside it and run by tools/test_network_extensions.py; exits nonzero on a
failure.
*/

#include "../game/network_extensions.c"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* ---------- the networking, as the test has it */

static unsigned long now = 1000;
static boolean custom_map_game;
static char const *required_setting = "";
static boolean machine_remote[HALO_PORT_MAXIMUM_NETWORK_MACHINES];

/* the last message each way: to a machine (its index), to the host */
static byte sent_to_machine[2048];
static long sent_to_machine_size;
static long sent_to_machine_index = NONE;
static byte sent_to_host[2048];
static long sent_to_host_size;
static long kicked = NONE;
static char notice[256];
static char error_box[512];
static int aborted;

void *create_message(long type, void const *data, unsigned long data_size, void *buffer, unsigned short buffer_size)
{
	word header = (word)(((data_size + sizeof(word)) << 4) | ((unsigned long)type << 2));

	if (data_size + sizeof(word) > buffer_size)
		return NULL;
	memcpy(buffer, &header, sizeof(header));
	memcpy((byte *)buffer + sizeof(header), data, data_size);
	return buffer;
}

boolean network_distributed_server_send_to_machine_reliably(long machine_index, void *message, unsigned short size)
{
	memcpy(sent_to_machine, message, size);
	sent_to_machine_size = size;
	sent_to_machine_index = machine_index;
	return TRUE;
}

boolean network_distributed_client_send_reliably(void *message, unsigned short size)
{
	memcpy(sent_to_host, message, size);
	sent_to_host_size = size;
	return TRUE;
}

static long settings_sent_to = NONE;

boolean network_extensions_server_send_settings(long machine_index)
{
	settings_sent_to = machine_index;
	return TRUE;
}

boolean network_extensions_server_machine_remote(long machine_index)
{
	return machine_index >= 0 && machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES && machine_remote[machine_index];
}

void network_game_server_kick_machine(long machine_index, boolean kept_out)
{
	(void)kept_out;
	kicked = machine_index;
}

void distributed_send_notice_to_machine(long machine_index, char const *text)
{
	(void)machine_index;
	snprintf(notice, sizeof(notice), "%s", text);
}

boolean custom_xbox_multiplayer_maps_required(void)
{
	return custom_map_game;
}

void display_error_text_when_main_menu_loaded(wchar_t const *text)
{
	long index;

	for (index = 0; text[index] && index < (long)sizeof(error_box) - 1; index++)
		error_box[index] = (char)text[index];
	error_box[index] = 0;
}

void network_game_abort(void)
{
	aborted++;
}

unsigned long system_milliseconds(void)
{
	return now;
}

char const *config_string(char const *name)
{
	return !strcmp(name, "debug.network_extensions_require") ? required_setting : "";
}

void platform_log(char const *format, ...)
{
	(void)format;
}

void error(int priority, char const *format, ...)
{
	(void)priority;
	(void)format;
}

/* ---------- helpers */

/* mcc_share.c's handler of file_share_tcp's data, as the test has it: what
it was last handed */
static long data_from = -2;
static long data_size = -1;
static byte data_bytes[64];

void mcc_share_file_share_data(long machine_index, void const *payload, long size)
{
	data_from = machine_index;
	data_size = size;
	memcpy(data_bytes, payload, (size_t)(size < 64 ? size : 64));
}

static void host_reset(void)
{
	memset(&host, 0, sizeof(host));
	memset(machine_remote, 0, sizeof(machine_remote));
	kicked = NONE;
	notice[0] = 0;
	sent_to_machine_size = 0;
	sent_to_host_size = 0;
}

/* a message's bytes after its header (as the packet switches hand them on) */
static void const *message_data(byte const *message)
{
	return message + sizeof(word);
}

static long message_data_size(long message_size)
{
	return message_size - (long)sizeof(word);
}

/* a message of `kind` of the given entries, framed as the build frames one:
its bytes after the header, the type byte last */
static long message_build_text(byte *out, byte kind, char const *const *keys, unsigned short const *versions,
	byte const *flags, char const *const *texts, long count)
{
	long size = 0, index;

	memcpy(out, "SLXT", 4);
	size = 4;
	out[size++] = 1;
	out[size++] = kind;
	out[size++] = (byte)count;
	for (index = 0; index < count; index++)
	{
		long length = (long)strlen(keys[index]);

		out[size++] = (byte)length;
		memcpy(out + size, keys[index], (size_t)length);
		size += length;
		out[size++] = (byte)(versions[index] & 0xFF);
		out[size++] = (byte)(versions[index] >> 8);
		out[size++] = flags[index];
		{
			long text_length = texts ? (long)strlen(texts[index]) : 0;

			out[size++] = (byte)text_length;
			memcpy(out + size, texts ? texts[index] : "", (size_t)text_length);
			size += text_length;
		}
	}
	out[size++] = NETWORK_EXTENSIONS_PACKET_TYPE;
	return size;
}

static long message_build(byte *out, byte kind, char const *const *keys, unsigned short const *versions,
	byte const *flags, long count)
{
	return message_build_text(out, kind, keys, versions, flags, NULL, count);
}

/* ---------- tests */

static void test_announcement_bytes(void)
{
	byte const *data;
	long size;

	host_reset();
	required_setting = NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS;
	machine_remote[3] = TRUE;
	network_extensions_host_machine_accepted(3);
	CHECK(sent_to_machine_index == 3);
	CHECK(GET_MESSAGE_TYPE(*(word *)sent_to_machine) == 3);
	CHECK((long)GET_MESSAGE_SIZE(*(word *)sent_to_machine) == sent_to_machine_size);
	data = message_data(sent_to_machine);
	size = message_data_size(sent_to_machine_size);
	CHECK(!memcmp(data, "SLXT", 4));
	CHECK(data[4] == 1 && data[5] == 0 && data[6] == 2);
	CHECK(data[7] == strlen(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS));
	CHECK(!memcmp(data + 8, NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, data[7]));
	CHECK(data[8 + data[7]] == 1 && data[9 + data[7]] == 0);
	/* (required) */
	CHECK(data[10 + data[7]] == 1);
	/* (its text, for a machine without it) */
	CHECK(data[11 + data[7]] == strlen(capabilities[0].missing_notice));
	CHECK(!memcmp(data + 12 + data[7], capabilities[0].missing_notice, data[11 + data[7]]));
	CHECK(data[size - 1] == NETWORK_EXTENSIONS_PACKET_TYPE);
	/* the second, file_share_tcp: version 1, not required, no text */
	{
		long at = 12 + data[7] + data[11 + data[7]];

		CHECK(data[at] == strlen(NETWORK_CAPABILITY_FILE_SHARE_TCP));
		CHECK(!memcmp(data + at + 1, NETWORK_CAPABILITY_FILE_SHARE_TCP, data[at]));
		CHECK(data[at + 1 + data[at]] == 1 && data[at + 2 + data[at]] == 0);
		CHECK(data[at + 3 + data[at]] == 0 && data[at + 4 + data[at]] == 0);
		CHECK(size == at + 5 + data[at] + 1);
	}
	required_setting = "";
}

static void test_client_reads_and_answers(void)
{
	byte message[512];
	char const *keys[] = { "some_future_capability", NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS };
	unsigned short versions[] = { 7, 1 };
	/* (one it does not know, not required: skipped) */
	byte flags[] = { 0, 1 };
	long size = message_build(message, 0, keys, versions, flags, 2);
	byte const *data;

	host_reset();
	network_extensions_client_reset();
	CHECK(!network_extension_host_has(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1));
	network_extensions_client_handle(message, size);
	CHECK(network_extension_host_has(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1));
	CHECK(!network_extension_host_has(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 2));
	CHECK(network_extension_host_requires(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS));
	CHECK(!network_extension_host_has("some_future_capability", 1));
	/* answered, with this build's capabilities */
	CHECK(sent_to_host_size > 0);
	data = message_data(sent_to_host);
	CHECK(!memcmp(data, "SLXT", 4) && data[5] == 1);
	/* (it can play the game: stays) */
	CHECK(!aborted);
	/* a new game: nothing known of its host (an OpenCE host announces nothing) */
	network_extensions_client_reset();
	CHECK(!network_extension_host_has(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1));
	CHECK(!network_extension_host_requires(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS));
}

static void test_malformed_dropped(void)
{
	byte message[512];
	char const *keys[] = { NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS };
	unsigned short versions[] = { 1 };
	byte flags[] = { 0 };
	long size = message_build(message, 0, keys, versions, flags, 1);
	byte broken[512];

	network_extensions_client_reset();
	/* another magic: a packet of type 255 that is not ours */
	memcpy(broken, message, (size_t)size);
	broken[0] = 'X';
	network_extensions_client_handle(broken, size);
	CHECK(!network_extension_host_has(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1));
	/* cut short */
	network_extensions_client_handle(message, size - 3);
	CHECK(!network_extension_host_has(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1));
	/* a key of a character keys never have */
	memcpy(broken, message, (size_t)size);
	broken[9] = '/';
	network_extensions_client_handle(broken, size);
	CHECK(!network_extension_host_has(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1));
	/* an answer is not an announcement */
	memcpy(broken, message, (size_t)size);
	broken[5] = 1;
	network_extensions_client_handle(broken, size);
	CHECK(!network_extension_host_has(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1));
	/* whole, it reads */
	network_extensions_client_handle(message, size);
	CHECK(network_extension_host_has(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1));
}

/* an OpenCE client: no answer; removed once the wait is over, told why */
static void test_no_answer_removed(void)
{
	host_reset();
	required_setting = NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS;
	machine_remote[2] = TRUE;
	network_extensions_host_machine_accepted(2);
	now += ANSWER_MILLISECONDS - 1;
	network_extensions_host_update();
	CHECK(kicked == NONE);
	now += 1;
	network_extensions_host_update();
	/* (told at once, removed once the notice has been up a while) */
	CHECK(strstr(notice, "custom OG Xbox multiplayer maps") != NULL);
	CHECK(kicked == NONE);
	now += NOTICE_MILLISECONDS;
	network_extensions_host_update();
	CHECK(kicked == 2);
	/* (once) */
	kicked = NONE;
	network_extensions_host_update();
	CHECK(kicked == NONE);
	required_setting = "";
}

/* a client that answers without the capability: removed at once */
static void test_answer_without_removed(void)
{
	byte message[512];
	char const *keys[] = { "another_capability" };
	unsigned short versions[] = { 1 };
	byte flags[] = { 0 };
	long size = message_build(message, 1, keys, versions, flags, 1);

	host_reset();
	required_setting = NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS;
	machine_remote[4] = TRUE;
	network_extensions_host_machine_accepted(4);
	settings_sent_to = NONE;
	network_extensions_host_handle(4, message, size);
	/* (still held: never sent the settings, removed) */
	CHECK(network_extensions_host_machine_held(4));
	CHECK(settings_sent_to == NONE);
	network_extensions_host_update();
	CHECK(notice[0]);
	now += NOTICE_MILLISECONDS;
	network_extensions_host_update();
	CHECK(kicked == 4);
	required_setting = "";
}

/* a client that answers with it: kept, and agreed */
static void test_answer_with_kept(void)
{
	byte message[512];
	char const *keys[] = { "another_capability", NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS };
	unsigned short versions[] = { 1, 1 };
	byte flags[] = { 0, 0 };
	long size = message_build(message, 1, keys, versions, flags, 2);

	host_reset();
	required_setting = NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS;
	machine_remote[5] = TRUE;
	network_extensions_host_machine_accepted(5);
	/* (held from the game's settings until it answers, then sent them) */
	CHECK(network_extensions_host_machine_held(5));
	settings_sent_to = NONE;
	network_extensions_host_handle(5, message, size);
	CHECK(!network_extensions_host_machine_held(5));
	CHECK(settings_sent_to == 5);
	now += ANSWER_MILLISECONDS * 2;
	network_extensions_host_update();
	CHECK(kicked == NONE);
	CHECK(network_extension_agreed(5, NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1));
	CHECK(!network_extension_agreed(5, NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 2));
	CHECK(!network_extension_agreed(6, NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS, 1));
	required_setting = "";
}

/* a game that requires nothing: no one removed (OpenCE clients play it) */
static void test_nothing_required_nobody_removed(void)
{
	host_reset();
	required_setting = "";
	machine_remote[1] = TRUE;
	network_extensions_host_machine_accepted(1);
	/* (nothing required: nothing held) */
	CHECK(!network_extensions_host_machine_held(1));
	now += ANSWER_MILLISECONDS * 10;
	network_extensions_host_update();
	CHECK(kicked == NONE);
}

/* the game comes to require it (another map): announced again; a machine
that never answered is removed once its wait is over */
static void test_requirement_change_announced(void)
{
	host_reset();
	required_setting = "";
	machine_remote[7] = TRUE;
	network_extensions_host_machine_accepted(7);
	sent_to_machine_size = 0;
	now += ANSWER_MILLISECONDS * 2;
	required_setting = NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS;
	network_extensions_host_update();
	CHECK(sent_to_machine_size > 0 && sent_to_machine_index == 7);
	/* (its wait was over long ago: told, then removed) */
	CHECK(notice[0]);
	now += NOTICE_MILLISECONDS;
	network_extensions_host_update();
	CHECK(kicked == 7);
	required_setting = "";
}

/* a machine gone is forgotten */
static void test_machine_gone_forgotten(void)
{
	host_reset();
	required_setting = NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS;
	machine_remote[8] = TRUE;
	network_extensions_host_machine_accepted(8);
	machine_remote[8] = FALSE;
	now += ANSWER_MILLISECONDS * 2;
	network_extensions_host_update();
	CHECK(kicked == NONE);
	CHECK(!host.machines[8].present);
	required_setting = "";
}

/* a game on an Xbox map of custom_maps requires it (map_share.c) */
static void test_custom_map_game_requires(void)
{
	host_reset();
	required_setting = "";
	custom_map_game = TRUE;
	machine_remote[9] = TRUE;
	network_extensions_host_machine_accepted(9);
	CHECK(((byte const *)message_data(sent_to_machine))[10 + strlen(NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS)] == 1);
	now += ANSWER_MILLISECONDS;
	network_extensions_host_update();
	now += NOTICE_MILLISECONDS;
	network_extensions_host_update();
	CHECK(kicked == 9);
	custom_map_game = FALSE;
}

/* the host's game requires a capability this build has not: the client
leaves itself, with the host's text */
static void test_client_leaves_without_required(void)
{
	byte message[512];
	char const *keys[] = { "some_future_capability" };
	unsigned short versions[] = { 1 };
	byte flags[] = { 1 };
	char const *texts[] = { "You need a client with support for future things." };
	long size = message_build_text(message, 0, keys, versions, flags, texts, 1);

	network_extensions_client_reset();
	aborted = 0;
	error_box[0] = 0;
	network_extensions_client_handle(message, size);
	CHECK(aborted == 1);
	CHECK(!strcmp(error_box, "You need a client with support for future things."));
	/* (once) */
	network_extensions_client_handle(message, size);
	CHECK(aborted == 1);

	/* a newer version than this build's of one it knows: the same */
	{
		char const *known[] = { NETWORK_CAPABILITY_CUSTOM_XBOX_MULTIPLAYER_MAPS };
		unsigned short newer[] = { 2 };

		size = message_build_text(message, 0, known, newer, flags, texts, 1);
		network_extensions_client_reset();
		aborted = 0;
		network_extensions_client_handle(message, size);
		CHECK(aborted == 1);
	}
	/* no text: its key named */
	size = message_build_text(message, 0, keys, versions, flags, NULL, 1);
	network_extensions_client_reset();
	aborted = 0;
	network_extensions_client_handle(message, size);
	CHECK(aborted == 1);
	CHECK(strstr(error_box, "some_future_capability") != NULL);
	/* a text past its message's end: dropped, nothing done */
	size = message_build_text(message, 0, keys, versions, flags, texts, 1);
	network_extensions_client_reset();
	aborted = 0;
	network_extensions_client_handle(message, size - 10);
	CHECK(aborted == 0);
	aborted = 0;
}

/* a capability's data (kind 2): sent only where it is agreed, and handed to
its handler only where it is (the token of the host's file connection) */
static void test_data_where_agreed(void)
{
	byte answer[512], announcement[512], payload[36];
	char const *keys[] = { NETWORK_CAPABILITY_FILE_SHARE_TCP };
	unsigned short versions[] = { 1 };
	byte flags[] = { 0 };
	long answer_size = message_build(answer, 1, keys, versions, flags, 1);
	long announcement_size = message_build(announcement, 0, keys, versions, flags, 1);
	byte data_message[512];
	long data_message_size;

	memset(payload, 0xA5, sizeof(payload));
	payload[0] = 7;
	host_reset();
	network_extensions_client_reset();
	machine_remote[3] = TRUE;
	network_extensions_host_machine_accepted(3);
	/* (a machine that has not answered with it: nothing sent) */
	CHECK(!network_extensions_send_data(3, NETWORK_CAPABILITY_FILE_SHARE_TCP, payload, sizeof(payload)));
	network_extensions_host_handle(3, answer, answer_size);
	CHECK(network_extension_agreed(3, NETWORK_CAPABILITY_FILE_SHARE_TCP, 1));
	sent_to_machine_size = 0;
	CHECK(network_extensions_send_data(3, NETWORK_CAPABILITY_FILE_SHARE_TCP, payload, sizeof(payload)));
	CHECK(sent_to_machine_size > 0 && sent_to_machine_index == 3);
	CHECK(!network_extensions_send_data(4, NETWORK_CAPABILITY_FILE_SHARE_TCP, payload, sizeof(payload)));
	CHECK(!network_extensions_send_data(3, "no_such_capability", payload, sizeof(payload)));
	data_message_size = message_data_size(sent_to_machine_size);
	memcpy(data_message, message_data(sent_to_machine), (size_t)data_message_size);
	/* the client: dropped before the host announced it */
	data_size = -1;
	network_extensions_client_handle(data_message, data_message_size);
	CHECK(data_size == -1);
	network_extensions_client_handle(announcement, announcement_size);
	CHECK(network_extension_host_has(NETWORK_CAPABILITY_FILE_SHARE_TCP, 1));
	sent_to_host_size = 0;
	network_extensions_client_handle(data_message, data_message_size);
	CHECK(data_from == NONE && data_size == (long)sizeof(payload) && !memcmp(data_bytes, payload, sizeof(payload)));
	/* (data is not answered, as an announcement is) */
	CHECK(sent_to_host_size == 0);
	/* the host: a client's data, handed on with the machine */
	CHECK(network_extensions_send_data(NONE, NETWORK_CAPABILITY_FILE_SHARE_TCP, payload, 5));
	data_size = -1;
	network_extensions_host_handle(3, message_data(sent_to_host), message_data_size(sent_to_host_size));
	CHECK(data_from == 3 && data_size == 5);
	/* (from a machine that never answered with it: dropped) */
	machine_remote[6] = TRUE;
	network_extensions_host_machine_accepted(6);
	data_size = -1;
	network_extensions_host_handle(6, message_data(sent_to_host), message_data_size(sent_to_host_size));
	CHECK(data_size == -1);
	network_extensions_client_reset();
}

int main(void)
{
	test_announcement_bytes();
	test_client_reads_and_answers();
	test_malformed_dropped();
	test_no_answer_removed();
	test_answer_without_removed();
	test_answer_with_kept();
	test_nothing_required_nobody_removed();
	test_requirement_change_announced();
	test_machine_gone_forgotten();
	test_custom_map_game_requires();
	test_client_leaves_without_required();
	test_data_where_agreed();
	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
