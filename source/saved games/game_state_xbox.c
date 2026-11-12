/*
GAME_STATE_XBOX.C

symbols in this file:
001AFBB0 0160:
	_game_state_allocate_buffer (0000)
001AFD10 0040:
	_game_state_free_buffer (0000)
001AFD50 00d0:
	_game_state_create_or_open_file (0000)
001AFE20 0040:
	_game_state_close_file (0000)
001AFE60 00e0:
	_game_state_write_to_file (0000)
001AFF40 0120:
	_game_state_read_from_file (0000)
001B0060 0090:
	_game_state_write_core (0000)
001B00F0 0080:
	_game_state_read_core_header (0000)
001B0170 00a0:
	_game_state_read_core (0000)
001B0210 0010:
	_game_state_get_persistent_storage_filename (0000)
001B0220 0020:
	_game_state_get_persistent_storage_path (0000)
001B0240 0030:
	_delete_persistent_storage (0000)
001B0270 0190:
	_game_state_open_persistent_storage (0000)
001B0400 01b0:
	_game_state_read_header_from_persistent_storage (0000)
001B05B0 0160:
	_game_state_write_to_persistent_storage (0000)
001B0710 00b0:
	_game_state_read_from_persistent_storage (0000)
001B07C0 0020:
	_game_state_create_persistent_storage (0000)
002A8084 001f:
	??_C@_0BP@IDAHEHI@?$CIunsigned?5long?$CJresult?$DN?$DNaddress?$AA@ (0000)
002A80A4 001e:
	??_C@_0BO@BBKFFHIP@?$CB?$CIgpu_size?$CG?$CICPU_PAGE_SIZE?91?$CJ?$CJ?$AA@ (0000)
002A80C4 001e:
	??_C@_0BO@BHHCKDPL@?$CB?$CIcpu_size?$CG?$CICPU_PAGE_SIZE?91?$CJ?$CJ?$AA@ (0000)
002A80E4 000b:
	??_C@_0L@JBLOLKNC@gpu_size?$DO0?$AA@ (0000)
002A80F0 000b:
	??_C@_0L@CBGHMJFA@cpu_size?$DO0?$AA@ (0000)
002A80FC 002a:
	??_C@_0CK@FIOMLFFF@?$CBxbox_game_state_globals?4buffer_@ (0000)
002A8128 002d:
	??_C@_0CN@KDCMADJG@c?3?2halo?2SOURCE?2saved?5games?2game_@ (0000)
002A8158 0029:
	??_C@_0CJ@FFFLELDG@xbox_game_state_globals?4buffer_a@ (0000)
002A8184 002e:
	??_C@_0CO@BEKHBMJJ@couldn?8t?5open?5or?5create?5saved?5ga@ (0000)
002A81B4 0010:
	??_C@_0BA@MOOKLLKO@z?3?2savegame?4bin?$AA@ (0000)
002A81C4 0023:
	??_C@_0CD@DDMIEKAL@?$CBxbox_game_state_globals?4file_op@ (0000)
002A81E8 0022:
	??_C@_0CC@CLCLOOEI@xbox_game_state_globals?4file_ope@ (0000)
002A820C 0025:
	??_C@_0CF@BLHGLGEN@couldn?8t?5write?5saved?5game?5file?5?$CI@ (0000)
002A8234 0024:
	??_C@_0CE@CPCCDNFP@couldn?8t?5read?5saved?5game?5file?5?$CI?$CD@ (0000)
002A8258 0048:
	??_C@_0EI@JODPNGEB@xbox_game_state_globals?4file_val@ (0000)
002A82A0 000b:
	??_C@_0L@BACPNKLN@d?3?2core?2?$CFs?$AA@ (0000)
002A82AC 0008:
	??_C@_07ELALHFFD@d?3?2core?$AA@ (0000)
002A82B4 0036:
	??_C@_0DG@PGBLNDAA@game?5state?5has?5been?5corrupted?5?$CIt@ (0000)
002A82EC 000d:
	??_C@_0N@KAKGDJDL@savegame?4bin?$AA@ (0000)
002A82FC 0030:
	??_C@_0DA@GAKEGFCK@couldn?8t?5open?5or?5create?5persiste@ (0000)
002A832C 0028:
	??_C@_0CI@LDKKAEDE@couldn?8t?5resize?5persistent?5stora@ (0000)
002A8354 0033:
	??_C@_0DD@EGEEEGAD@couldn?8t?5read?5header?5from?5persis@ (0000)
002A8388 0026:
	??_C@_0CG@JDCOBIBA@checksum?5failed?5on?5persistent?5st@ (0000)
002A83B0 002c:
	??_C@_0CM@LDKGDBEH@failed?5to?5write?5to?5persistent?5st@ (0000)
002A83DC 0021:
	??_C@_0CB@GFGKONGF@header_size?$DMsizeof?$CIsaved_header?$CJ@ (0000)
002A8400 002d:
	??_C@_0CN@DOEIAHAD@failed?5to?5read?5from?5persistent?5s@ (0000)
004D27D0 0014:
	_xbox_game_state_globals (0000)
*/

/* ---------- headers */

#include "cache/physical_memory_map.h"
#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h" /* port: the check ahead */
#include "memory/crc.h"
#include "saved games/game_state.h"
#include "bungie_net/common/thread.h" /* port: the check ahead */
#include "sound/sound_manager.h"
#include <xtl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	CPU_PAGE_SIZE = 0x1000,
	/* the native builds' larger game state (halo_port_capacity.h); the saved
	game files only need to hold it */
	GAME_STATE_SIZE = HALO_PORT_GAME_STATE_SIZE,
	GAME_STATE_FILE_SIZE = HALO_PORT_GAME_STATE_SIZE
};

/* ---------- structures */

struct xbox_game_state_globals_prefix
{
	boolean buffer_allocated;
	byte reserved001[3];
	void *buffer;
	long buffer_size;
	boolean file_open;
	boolean file_valid_for_read;
	byte reserved00E[2];
	HANDLE handle;
};

typedef char verify_xbox_game_state_buffer_offset[
	offsetof(struct xbox_game_state_globals_prefix, buffer) == 0x4 ? 1 : -1];
typedef char verify_xbox_game_state_file_open_offset[
	offsetof(struct xbox_game_state_globals_prefix, file_open) == 0xC ? 1 : -1];
typedef char verify_xbox_game_state_handle_offset[
	offsetof(struct xbox_game_state_globals_prefix, handle) == 0x10 ? 1 : -1];
typedef char verify_xbox_game_state_globals_prefix_size[
	sizeof(struct xbox_game_state_globals_prefix) == 0x14 ? 1 : -1];

/* ---------- prototypes */

static void game_state_header_terminate(
	void *header,
	long header_size);
static HANDLE game_state_open_persistent_storage(
	const char *directory);
static boolean game_state_snapshot_ready(
	void);

/* ---------- globals */

static struct xbox_game_state_globals_prefix xbox_game_state_globals = { 0 };

/* The checkpoint kept in memory (not the Xbox's: the native builds' game
state is 20 MB, written to the card or the disk with the game stopped). A
checkpoint copies the game state into the snapshot and returns at once, and a
revert copies it back. The game state cannot be kept where it is: the game
changes it from the next tick.

Nothing is written to savegame.bin while the snapshot exists. The file was
written behind the game's back by a thread of its own, and nothing read it:
a revert takes the snapshot, a new map clears saved_game_valid, and a saved
game the player keeps is the profile's (game_state_write_to_persistent_storage).
On the Switch those 20 MB a checkpoint held the card for seconds, and the
game's texture reads, debug.txt and halo.log waited behind them (a 7.5 s
frame in a co-op game of 343 Guilty Spark). Only recover_saved_games_hack,
a developer's script global, read the file; it is written as the Xbox wrote
it only when the snapshot cannot be made. */
static struct
{
	/* the snapshot (allocation is what malloc gave), and whether it holds
	this file's latest checkpoint */
	void *allocation;
	void *snapshot;
	boolean snapshot_valid;
} game_state_snapshot = { 0 };

/* ---------- public code */

void *game_state_allocate_buffer(
	unsigned long address,
	unsigned long cpu_size,
	unsigned long gpu_size)
{
	void *result;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		46,
		!xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		48,
		address);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		49,
		cpu_size>0);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		50,
		gpu_size>0);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		53,
		!(cpu_size&(CPU_PAGE_SIZE-1)));
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		54,
		!(gpu_size&(CPU_PAGE_SIZE-1)));

	result = physical_memory_get_game_state_base_address();
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		58,
		result);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		59,
		(unsigned long)result==address);

	XPhysicalProtect(
		(void *)(address+cpu_size),
		gpu_size,
		PAGE_READWRITE|PAGE_WRITECOMBINE);

	xbox_game_state_globals.buffer_allocated = TRUE;
	xbox_game_state_globals.buffer = (void *)address;
	xbox_game_state_globals.buffer_size = cpu_size+gpu_size;

	return result;
}

void game_state_free_buffer(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		75,
		xbox_game_state_globals.buffer_allocated);
	XPhysicalFree(xbox_game_state_globals.buffer);
	xbox_game_state_globals.buffer_allocated = FALSE;

	return;
}

void game_state_create_or_open_file(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		86,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		87,
		!xbox_game_state_globals.file_open);

	xbox_game_state_globals.handle = CreateFileA("z:\\savegame.bin",
		GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
		FILE_FLAG_NO_BUFFERING|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (xbox_game_state_globals.handle != INVALID_HANDLE_VALUE &&
		SetFilePointer(xbox_game_state_globals.handle, GAME_STATE_FILE_SIZE, NULL,
			FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
		SetEndOfFile(xbox_game_state_globals.handle))
	{
		xbox_game_state_globals.file_open = TRUE;
		/* the 20 MB snapshot made now, while the map loads: made at the first
		checkpoint, it cost that frame 0.87 s on the Switch (Assault on the
		Control Room's first checkpoint) */
		game_state_snapshot_ready();
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			97,
			FALSE,
			csprintf(temporary, "couldn't open or create saved game file (#%d)",
				GetLastError()));
	}

	return;
}

void game_state_close_file(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		106,
		xbox_game_state_globals.file_open);
	game_state_snapshot.snapshot_valid = FALSE;
	CloseHandle(xbox_game_state_globals.handle);
	xbox_game_state_globals.file_open = FALSE;

	return;
}

/* the snapshot made; FALSE if it cannot be, and the checkpoint is written
to the file as the Xbox wrote it */
static boolean game_state_snapshot_ready(
	void)
{
	if (!game_state_snapshot.allocation)
	{
		game_state_snapshot.allocation = malloc(xbox_game_state_globals.buffer_size + CPU_PAGE_SIZE);
		if (!game_state_snapshot.allocation)
			return FALSE;
		game_state_snapshot.snapshot = (void *)(((uintptr_t)game_state_snapshot.allocation + CPU_PAGE_SIZE - 1) &
			~(uintptr_t)(CPU_PAGE_SIZE - 1));
	}
	return TRUE;
}

boolean game_state_write_to_file(
	void)
{
	unsigned long bytes_written;
	boolean result = FALSE;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		120,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		121,
		xbox_game_state_globals.file_open);

	/* in memory: the snapshot taken, done (nothing written: see
	game_state_snapshot) */
	if (game_state_snapshot_ready())
	{
		memcpy(game_state_snapshot.snapshot, xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size);
		game_state_snapshot.snapshot_valid = TRUE;
		xbox_game_state_globals.file_valid_for_read = TRUE;
		return TRUE;
	}

	if (SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		WriteFile(xbox_game_state_globals.handle, xbox_game_state_globals.buffer,
			xbox_game_state_globals.buffer_size, &bytes_written, NULL) &&
		bytes_written == xbox_game_state_globals.buffer_size)
	{
		xbox_game_state_globals.file_valid_for_read = TRUE;
		result = TRUE;
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			132,
			FALSE,
			csprintf(temporary, "couldn't write saved game file (#%d)", GetLastError()));
	}

	return result;
}

boolean game_state_read_from_file(
	void)
{
	unsigned long bytes_read;
	boolean result = FALSE;
	void *image;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		144,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		145,
		xbox_game_state_globals.file_open);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		146,
		xbox_game_state_globals.file_valid_for_read || recover_saved_games_hack);

	/* this file's checkpoint is in memory (game_state_snapshot). It is this
	process's own game state, taken as it was saved, so it is not checked as
	an image read from a file is */
	if (game_state_snapshot.snapshot_valid)
	{
		memcpy(xbox_game_state_globals.buffer, game_state_snapshot.snapshot, xbox_game_state_globals.buffer_size);
		return TRUE;
	}

	/* port: read beside the game state, and taken once checked */
	image = malloc(xbox_game_state_globals.buffer_size);
	if (image &&
		SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		ReadFile(xbox_game_state_globals.handle, image,
			xbox_game_state_globals.buffer_size, &bytes_read, NULL) &&
		bytes_read == xbox_game_state_globals.buffer_size &&
		game_state_image_accept(image, xbox_game_state_globals.buffer_size))
	{
		result = TRUE;
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			156,
			FALSE,
			csprintf(temporary, "couldn't read saved game file (#%d)", GetLastError()));
	}
	free(image);

	return result;
}

boolean game_state_write_core(
	const char *name,
	void *buffer,
	long buffer_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_written;
	boolean result = FALSE;

	CreateDirectoryA("d:\\core", NULL);
	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		if (WriteFile(file, buffer, buffer_size, &bytes_written, NULL) &&
			bytes_written == buffer_size)
		{
			result = TRUE;
		}
	}
	CloseHandle(file);

	return result;
}

boolean game_state_read_core_header(
	const char *name,
	void *header,
	long header_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_read;
	boolean result = FALSE;

	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		if (ReadFile(file, header, header_size, &bytes_read, NULL) &&
			bytes_read == header_size)
		{
			game_state_header_terminate(header, header_size);
			result = TRUE;
		}
	}
	CloseHandle(file);

	return result;
}

void game_state_read_core(
	const char *name,
	void *buffer,
	long buffer_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_read;

	void *image = malloc(buffer_size);

	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		NULL);
	/* port: read beside the game state, and taken once checked */
	match_vassert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		226,
		image && file != INVALID_HANDLE_VALUE &&
			ReadFile(file, image, buffer_size, &bytes_read, NULL) &&
			bytes_read == buffer_size &&
			game_state_image_accept(image, buffer_size),
		"game state has been corrupted (thank you, come again)");
	(void)buffer;
	CloseHandle(file);
	free(image);

	return;
}

const char *game_state_get_persistent_storage_filename(
	void)
{
	return "savegame.bin";
}

static __inline boolean game_state_get_persistent_storage_path(
	char *path)
{
	boolean result;

	result = player_ui_get_path_to_local_player_profile_directory(0, path) != FALSE;

	return result;
}

static void persistent_header_cache_forget(void);

static void delete_persistent_storage(
	void)
{
	char path[256];

	persistent_header_cache_forget();
	if (game_state_get_persistent_storage_path(path))
		DeleteFileA(path);

	return;
}

static HANDLE game_state_open_persistent_storage(
	const char *directory)
{
	char path[256];
	byte zeroes[16*1024];
	HANDLE file;
	unsigned long bytes_written;
	boolean success = FALSE;

	if (directory || game_state_get_persistent_storage_path(path))
	{
		if (directory)
		{
			strcpy(path, directory);
		}
		else
		{
			game_state_get_persistent_storage_path(path);
		}
		strcat(path, "savegame.bin");

		file = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0,
			NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			success = TRUE;
			if (GetFileSize(file, NULL) != GAME_STATE_FILE_SIZE)
			{
				persistent_header_cache_forget(); /* port */
				memset(zeroes, 0, sizeof(zeroes));
				if (!WriteFile(file, zeroes, sizeof(zeroes), &bytes_written, NULL) ||
					bytes_written != sizeof(zeroes) ||
					SetFilePointer(file, GAME_STATE_FILE_SIZE, NULL, FILE_BEGIN) ==
						INVALID_SET_FILE_POINTER ||
					!SetEndOfFile(file))
				{
					match_vassert(
						"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
						491,
						FALSE,
						csprintf(temporary,
							"couldn't resize persistent storage \"%s\"", path));
					delete_persistent_storage();
					CloseHandle(file);
					success = FALSE;
				}
			}
		}
		else
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				498,
				FALSE,
				csprintf(temporary, "couldn't open or create persistent storage \"%s\"",
					path));
		}
	}

	return success ? file : INVALID_HANDLE_VALUE;
}

/* port: the strings of a header read from a file end in it (the file is
anyone's) */
static void game_state_header_terminate(
	void *header,
	long header_size)
{
	if (header_size == sizeof(struct game_state_header))
	{
		struct game_state_header *game_state_header = header;

		game_state_header->map_name[NUMBEROF(game_state_header->map_name) - 1] = 0;
		game_state_header->build_number[NUMBEROF(game_state_header->build_number) - 1] = 0;
	}
}

/* port: what the campaign menus ask of the saved game, each time they open
(game_state_test_persistent_storage), kept: the answer is the whole 20 MB
file read and checksummed, half a second and more on the Switch's card for
each menu. It is the same answer while the file is: its path, size and time
written the same, and nothing here written, resized or deleted it since (the
file is always the same size, and a FAT card's times are two seconds apart,
so those alone would not tell a save made just now). */
static struct
{
	boolean valid;
	char path[256];
	DWORD size_low, size_high;
	FILETIME written;
	long header_size, buffer_size;
	byte header[2048];
	boolean result, corrupted;
} persistent_header_cache;

static unsigned long persistent_header_generation;

static void persistent_header_cache_forget(
	void)
{
	persistent_header_cache.valid = FALSE;
	persistent_header_generation++;
}

/* the saved game's path and how it is now; FALSE if there is none */
static boolean persistent_header_stamp(
	char *path,
	WIN32_FILE_ATTRIBUTE_DATA *data)
{
	if (!game_state_get_persistent_storage_path(path))
		return FALSE;
	strcat(path, "savegame.bin");
	return GetFileAttributesExA(path, GetFileExInfoStandard, data) != FALSE;
}

static boolean game_state_read_header_uncached(
	void *header,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size,
	boolean *corrupted,
	char const *directory);
static void persistent_header_ahead_adopt(
	boolean wait);

boolean game_state_read_header_from_persistent_storage(
	void *header,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size,
	boolean *corrupted)
{
	char path[256];
	WIN32_FILE_ATTRIBUTE_DATA data;
	boolean result;
	boolean corrupted_read = FALSE;

	/* (a check made ahead, on its thread, taken: waited for if it is still
	going, which is sooner than reading the file again here) */
	persistent_header_ahead_adopt(TRUE);
	if (header_size <= (long)sizeof(persistent_header_cache.header) &&
		persistent_header_cache.valid &&
		persistent_header_cache.header_size == header_size &&
		persistent_header_cache.buffer_size == buffer_size &&
		persistent_header_stamp(path, &data) &&
		!strcmp(path, persistent_header_cache.path) &&
		data.nFileSizeLow == persistent_header_cache.size_low &&
		data.nFileSizeHigh == persistent_header_cache.size_high &&
		!memcmp(&data.ftLastWriteTime, &persistent_header_cache.written, sizeof(data.ftLastWriteTime)))
	{
		/* (the header as the read left it: its checksum, which
		header_checksum points into, cleared) */
		memcpy(header, persistent_header_cache.header, header_size);
		if (corrupted)
			*corrupted = persistent_header_cache.corrupted;
		return persistent_header_cache.result;
	}
	persistent_header_cache_forget();
	result = game_state_read_header_uncached(header, header_checksum, header_size, buffer_size, &corrupted_read,
		NULL);
	if (corrupted)
		*corrupted = corrupted_read;
	/* (stamped after: the read may have made the file, or sized it) */
	if (header_size <= (long)sizeof(persistent_header_cache.header) && persistent_header_stamp(path, &data))
	{
		strcpy(persistent_header_cache.path, path);
		persistent_header_cache.size_low = data.nFileSizeLow;
		persistent_header_cache.size_high = data.nFileSizeHigh;
		persistent_header_cache.written = data.ftLastWriteTime;
		persistent_header_cache.header_size = header_size;
		persistent_header_cache.buffer_size = buffer_size;
		memcpy(persistent_header_cache.header, header, header_size);
		persistent_header_cache.result = result;
		persistent_header_cache.corrupted = corrupted_read;
		persistent_header_cache.valid = TRUE;
	}
	return result;
}

/* (port: with a directory, the profile's folder given, on the check ahead's
thread: no sound to keep fed, nothing deleted, and its buffer not its
stack's) */
static boolean game_state_read_header_uncached(
	void *header,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size,
	boolean *corrupted,
	char const *directory)
{
	byte stack_buffer[128*1024];
	byte *buffer = stack_buffer;
	HANDLE file;
	unsigned long checksum;
	unsigned long stored_checksum;
	unsigned long bytes_read;
	long remaining_size;
	long read_size;
	boolean result;

	result = FALSE;
	if (corrupted)
		*corrupted = FALSE;
	if (directory && (buffer = malloc(sizeof(stack_buffer))) == NULL)
		return FALSE;
	file = game_state_open_persistent_storage(directory);

	if (file != INVALID_HANDLE_VALUE)
	{
		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!ReadFile(file, header, header_size, &bytes_read, NULL) ||
			bytes_read != header_size)
		{
			game_state_header_terminate(header, header_size);
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				298,
				FALSE,
				csprintf(temporary,
					"couldn't read header from persistent storage (#%d)",
					GetLastError()));
			if (!directory)
				delete_persistent_storage();
		}
		else
		{
			game_state_header_terminate(header, header_size);
			stored_checksum = *header_checksum;

			crc_new(&checksum);
			*header_checksum = 0;
			crc_checksum_buffer(&checksum, header, header_size);

			remaining_size = buffer_size-header_size;
			while (remaining_size>0)
			{
				read_size = MIN(sizeof(stack_buffer), remaining_size);
				if (ReadFile(file, buffer, read_size, &bytes_read, NULL) &&
					bytes_read == read_size)
				{
					crc_checksum_buffer(&checksum, buffer, read_size);
				}
				if (!directory)
					sound_idle();
				remaining_size -= read_size;
			}

			if (checksum == stored_checksum)
			{
				result = TRUE;
			}
			else
			{
				if (corrupted && stored_checksum)
					*corrupted = TRUE;
				error(_error_silent, "checksum failed on persistent storage");
			}
		}
		CloseHandle(file);
	}
	if (directory)
		free(buffer);

	return result;
}

/* port: the menus' check of the saved game, made ahead while the main menu
is up (game_state_persistent_storage_check_ahead), on a thread of its own:
the first one of a run read the whole 20 MB file as the campaign menu
opened, more than half a second on the Switch's card. Its answer is the
kept one's (persistent_header_cache), taken on the game's thread
(persistent_header_ahead_adopt) unless something here has written, sized or
deleted the file since it began. */
static struct
{
	struct thread_reference *thread;
	unsigned long generation;
	char directory[256];
	char path[256];
	struct game_state_header header;
	boolean result, corrupted, stamped;
	WIN32_FILE_ATTRIBUTE_DATA data;
} persistent_header_ahead;
static long persistent_header_ahead_profile = NONE;
static unsigned long persistent_header_ahead_started_generation = (unsigned long)-1;

static unsigned long __stdcall persistent_header_ahead_proc(
	void *input)
{
	persistent_header_ahead.result = game_state_read_header_uncached(&persistent_header_ahead.header,
		&persistent_header_ahead.header.checksum, sizeof(persistent_header_ahead.header), GAME_STATE_SIZE,
		&persistent_header_ahead.corrupted, persistent_header_ahead.directory);
	persistent_header_ahead.stamped = GetFileAttributesExA(persistent_header_ahead.path, GetFileExInfoStandard,
		&persistent_header_ahead.data) != FALSE;
	(void)input;

	return 0;
}

static void persistent_header_ahead_adopt(
	boolean wait)
{
	if (!persistent_header_ahead.thread)
		return;
	if (!thread_has_exited(persistent_header_ahead.thread))
	{
		if (!wait)
			return;
		while (!thread_has_exited(persistent_header_ahead.thread))
			Sleep(1);
	}
	dispose_thread(persistent_header_ahead.thread);
	persistent_header_ahead.thread = NULL;
	if (persistent_header_ahead.stamped && persistent_header_ahead.generation == persistent_header_generation)
	{
		strcpy(persistent_header_cache.path, persistent_header_ahead.path);
		persistent_header_cache.size_low = persistent_header_ahead.data.nFileSizeLow;
		persistent_header_cache.size_high = persistent_header_ahead.data.nFileSizeHigh;
		persistent_header_cache.written = persistent_header_ahead.data.ftLastWriteTime;
		persistent_header_cache.header_size = sizeof(persistent_header_ahead.header);
		persistent_header_cache.buffer_size = GAME_STATE_SIZE;
		memcpy(persistent_header_cache.header, &persistent_header_ahead.header, sizeof(persistent_header_ahead.header));
		persistent_header_cache.result = persistent_header_ahead.result;
		persistent_header_cache.corrupted = persistent_header_ahead.corrupted;
		persistent_header_cache.valid = TRUE;
	}
}

/* (each tick of the main menu: nothing asked of the card unless player 1's
profile has changed, or the saved game has, since the last check) */
void game_state_persistent_storage_check_ahead(
	void)
{
	long profile_index;
	WIN32_FILE_ATTRIBUTE_DATA data;

	if (persistent_header_ahead.thread)
	{
		persistent_header_ahead_adopt(FALSE);
		return;
	}
	/* (on the main menu no profile is active yet: the campaign menu makes
	player 1's last one active as it opens, so that one is checked) */
	profile_index = player_ui_get_active_player_profile_index(0);
	if (profile_index == NONE)
		profile_index = player_ui_get_player1_last_used_profile_index();
	if (profile_index == NONE ||
		(profile_index == persistent_header_ahead_profile &&
		persistent_header_generation == persistent_header_ahead_started_generation))
	{
		return;
	}
	persistent_header_ahead_profile = profile_index;
	persistent_header_ahead_started_generation = persistent_header_generation;
	if (!player_profile_get_enclosing_directory_path(profile_index, persistent_header_ahead.directory))
		return;
	snprintf(persistent_header_ahead.path, sizeof(persistent_header_ahead.path), "%ssavegame.bin",
		persistent_header_ahead.directory);
	/* (kept already; or no saved game, which the check would make) */
	if (persistent_header_cache.valid && !strcmp(persistent_header_cache.path, persistent_header_ahead.path))
		return;
	if (!GetFileAttributesExA(persistent_header_ahead.path, GetFileExInfoStandard, &data))
		return;
	persistent_header_ahead.generation = persistent_header_generation;
	if (!create_thread(0, persistent_header_ahead_proc, NULL, &persistent_header_ahead.thread))
		persistent_header_ahead.thread = NULL;
}

void game_state_write_to_persistent_storage(
	void *buffer,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size)
{
	byte saved_header[2048];
	HANDLE file;
	unsigned long checksum;
	unsigned long bytes_written;

	persistent_header_cache_forget(); /* port */
	file = game_state_open_persistent_storage(NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		*header_checksum = 0;

		crc_new(&checksum);
		crc_checksum_buffer(&checksum, buffer, GAME_STATE_SIZE);
		*header_checksum = checksum;

		match_assert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			333,
			header_size<sizeof(saved_header));

		memcpy(saved_header, buffer, header_size);
		memset(buffer, 0, header_size);

		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!WriteFile(file, buffer, buffer_size, &bytes_written, NULL) ||
			bytes_written != buffer_size ||
			SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!WriteFile(file, saved_header, header_size, &bytes_written, NULL) ||
			bytes_written != header_size)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				351,
				FALSE,
				csprintf(temporary, "failed to write to persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}

		memcpy(buffer, saved_header, header_size);
		CloseHandle(file);
	}

	return;
}

void game_state_read_from_persistent_storage(
	void *buffer,
	long buffer_size)
{
	HANDLE file;
	unsigned long bytes_read;

	file = game_state_open_persistent_storage(NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		/* port: read beside the game state, and taken once checked */
		void *image = malloc(buffer_size);

		if (!image ||
			SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!ReadFile(file, image, buffer_size, &bytes_read, NULL) ||
			bytes_read != buffer_size ||
			!game_state_image_accept(image, buffer_size))
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				383,
				FALSE,
				csprintf(temporary, "failed to read from persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}
		(void)buffer;
		free(image);
		CloseHandle(file);
	}

	return;
}

void game_state_create_persistent_storage(
	const char *path)
{
	HANDLE handle;

	handle = game_state_open_persistent_storage(path);
	if (handle != INVALID_HANDLE_VALUE)
		CloseHandle(handle);

	return;
}

/* ---------- private code */
