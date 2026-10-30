/*
PLATFORM.H

Shared internals of the Linux platform layer, which implements the Xbox SDK
(XAPI, Direct3D 8, DirectSound, XNet, ...) and MSVC runtime interfaces that
the game calls. Files including this header are compiled with the game's
ABI and see the XDK declarations, so every definition is type-checked
against the SDK prototype it replaces.
*/

#ifndef __HALO_LINUX_PLATFORM_H
#define __HALO_LINUX_PLATFORM_H

/* The host's own declarations come first. The XDK's Winsock names and
types (select, fd_set, timeval, ...) are then moved out of glibc's way while
the SDK headers are read, exactly as game code sees them. */
#include <pthread.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>

/* as cseries_windows.h does, so xtl.h also declares the debug keyboard */
#define DEBUG_KEYBOARD
#include "halo_linux_winsock_names.h"
#include <xtl.h>
#include <xbdm.h>
#include <d3d8perf.h>
#define HALO_LINUX_WINSOCK_NAMES_UNDEFINE
#include "halo_linux_winsock_names.h"
#undef HALO_LINUX_WINSOCK_NAMES_UNDEFINE

/* ---------- logging */

/* prints "halo-linux: <message>" to stderr */
void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* opens the url in the system's browser (a Discord link's page: player_link.c);
in the web build, the page offers it to be opened (a page may open a tab only
when it is clicked) */
void platform_open_url(const char *url);

/* reports an SDK entry point the Linux port does not implement, once per
name; the caller then behaves as a failing or empty call */
void platform_unimplemented(const char *name);

#define PLATFORM_UNIMPLEMENTED() platform_unimplemented(__func__)

/* ---------- errors */

/* translate errno into a Win32 error code and store it as GetLastError() */
DWORD platform_set_last_error_from_errno(int error_number);

/* ---------- handles */

enum platform_handle_type
{
	_platform_handle_file = 1,
	_platform_handle_event,
	_platform_handle_mutex,
	_platform_handle_thread,
	_platform_handle_find,
	_platform_handle_signature,
	_platform_handle_other,
};

/* Every HANDLE the layer returns points at one of these. Waitable objects
(events, mutexes, threads) share the embedded lock and condition. */
struct platform_handle
{
	unsigned long signature;
	long type;
	pthread_mutex_t lock;
	pthread_cond_t condition;
	BOOL signaled;
	BOOL manual_reset;
	/* mutexes */
	pthread_t owner;
	long recursion;
	/* type specific data */
	void *data;
	void (*destroy)(struct platform_handle *handle);
};

struct platform_handle *platform_handle_new(long type, void *data,
	void (*destroy)(struct platform_handle *handle));
/* returns NULL (and sets ERROR_INVALID_HANDLE) unless handle has this type */
struct platform_handle *platform_handle_get(HANDLE handle, long type);
/* marks a waitable handle signaled and wakes waiters */
void platform_handle_signal(struct platform_handle *handle);

/* ---------- asynchronous procedure calls (ReadFileEx completions) */

typedef void (*platform_apc_routine)(void *context0, void *context1, void *context2);
/* queue a routine for the calling thread's next alertable wait */
void platform_queue_apc(platform_apc_routine routine, void *context0, void *context1, void *context2);
/* run the calling thread's queued routines; returns how many ran */
long platform_run_apcs(void);

/* ---------- paths */

/* Translate an Xbox path (d:\maps\a10.map, t:\..., u:\..., z:\...) into a
host path below the data root. Components are matched case-insensitively
against what exists on disk, as the Xbox file system is case-insensitive. */
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
/* a file the game reads from start to end with ReadFileEx (a map unpacked into the cache:
cache_files_decompress_windows.c) read ahead by a thread of its own, so that the card is read while the
game works on what it has (xbox_files.c); its other reads, and every other file's, are as before */
void platform_file_read_ahead(HANDLE file);
const char *platform_data_root(void);
/* the Halo Custom Edition install the Xbox drive h:\ is (paths.custom_edition),
or "" when none is set or it has no maps folder */
const char *platform_custom_edition_root(void);
/* on the desktop, when the data root has no maps folder: offers to copy it
out of an Xbox disc image into destination (sdl_platform.c), and quits if
the player declines; nonzero once destination has one */
BOOL platform_offer_game_data(const char *destination);
/* what the data root's maps folder holds, which should be the Xbox maps
(sdl_platform.c tells the player when it is not, rather than the game
failing on Halo PC's ui.map) */
enum
{
	_maps_folder_xbox,
	/* its ui.map is Halo PC's (retail's, version 7, or Custom Edition's, 609) */
	_maps_folder_halo_pc,
	/* it has no ui.map (or one that is not a map) */
	_maps_folder_no_ui,
};
#define PLATFORM_MAPS_FOLDER_NAMED 4
struct platform_maps_folder
{
	int state;
	/* the data root, whole */
	char root[1024];
	long ui_version;
	/* Halo PC maps in maps/ itself, which play only from custom_maps: how
	many, and the first PLATFORM_MAPS_FOLDER_NAMED's names */
	long stray_pc_maps;
	char stray_names[256];
};
void platform_maps_folder_check(struct platform_maps_folder *maps);
/* asks the player, in a box of its own that waits for the answer (before
the window opens, or between frames), with `count` buttons (3 at most: Android's),
`default_index` Enter's and Escape's: the index of the one chosen, or -1
when it cannot ask */
int platform_ask(const char *title, const char *text, const char *const *labels, int count, int default_index);
const char *platform_save_root(void);

/* ---------- contiguous ("physical") memory

The Xbox maps physical memory at virtual 0x80000000 + P. The layer reserves
that window at start-up and hands out page-granular blocks from it, so the
physical/virtual arithmetic the game and Direct3D rely on keeps working.

On Android the process shares the low 4 GB with the Java runtime, which maps
memory of its own there, so the host looks for the window where the address
space is free and passes its address to the guest in the boot structure
(port/android). Everything that names a fixed place in the window must then
be PLATFORM_CONTIGUOUS_ADDRESS(offset) rather than a literal. The window
stays a multiple of 256 MB, which keeps the masking below correct.

The PS5's x32 guest (HALO_MOVABLE_WINDOW) is told its base the same way, as
the console loads a system module at 0x80000000; its size stays the x86-64
guest's 512 MB. */

#if defined(HALO_ANDROID) || defined(HALO_MOVABLE_WINDOW)
extern unsigned long platform_contiguous_base;
#define PLATFORM_CONTIGUOUS_BASE (platform_contiguous_base)
#else
#define PLATFORM_CONTIGUOUS_BASE 0x80000000UL
#endif
#ifdef HALO_ANDROID
/* as large as the host found room for, from 128 MB, a development kit's, to
512 MB (port/android/include/halo_android_abi.h), and passed in the boot
structure with the base; tables kept for each page of the window are as
large as the largest */
extern unsigned long platform_contiguous_size;
#define PLATFORM_CONTIGUOUS_SIZE (platform_contiguous_size)
#define PLATFORM_CONTIGUOUS_MAXIMUM 0x20000000UL
#elif defined(HALO_WEB) || (defined(HALO_GUEST) && defined(__aarch64__))
/* 128 MB, a development kit's: 64-bit ARM Linux's guest image is linked
just above the window, and the web build's WebAssembly memory ends with it
(tools/web_build.py). The guests' host reserves HALO_GUEST_WINDOW_SIZE
(port/android/include/halo_android_abi.h), which must agree; so must
halo_port_capacity.h's test of the same builds. */
#define PLATFORM_CONTIGUOUS_SIZE 0x08000000UL
#define PLATFORM_CONTIGUOUS_MAXIMUM PLATFORM_CONTIGUOUS_SIZE
#else
/* 512 MB on the desktop builds, whose caches outgrow the Xbox's (Custom
Edition maps' textures and geometry: halo_port_capacity.h), the x86-64
guest's too, whose image is linked below the window. Its pages are backed
as they are used. port/windows/src/win32_memory_watch.c has the same
size. */
#define PLATFORM_CONTIGUOUS_SIZE 0x20000000UL
#define PLATFORM_CONTIGUOUS_MAXIMUM PLATFORM_CONTIGUOUS_SIZE
#endif
#define PLATFORM_ANY_PHYSICAL_ADDRESS 0xffffffffUL

/* the address of a fixed place in the window, as the game data was built */
#define PLATFORM_CONTIGUOUS_ADDRESS(offset) (PLATFORM_CONTIGUOUS_BASE + (unsigned long)(offset))

/* returns NULL on failure; physical_address places the block exactly */
void *platform_contiguous_alloc(unsigned long size, unsigned long alignment,
	unsigned long physical_address, DWORD protect);
void platform_contiguous_free(void *address);
BOOL platform_is_contiguous(const void *address);
/* A window address and an offset into the window name the same place, and the
port carries both: a resource the game allocated holds the address it asked
for, and the port's own allocations hold an offset. On the Xbox the two could
not be told apart, and setting bit 31 converted either, which is why this used
to be an OR. That is only right while the base is 0x80000000: once the window
moves, an address still written as 0x80...... has to be moved with it and an
offset has the base added. Where the window did not move, both forms are the
same arithmetic as before.

A third form is an address already in the window where it is: the map
data's vertex and index buffers are moved there as the map is checked
(tag_validate.c). That one is kept as it is, before the other two are
tried: with the window at 0xa0000000 bit 31 is set in its addresses, which
were moved a second time, out of the window. It cannot be mistaken for an
offset, because the window starts on a multiple of its own size, so every
address in it is at least the window's size and every offset is less. */
#define PLATFORM_PHYSICAL_TO_VIRTUAL(physical) \
	((void *)((unsigned long)(physical) - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE ? \
		(unsigned long)(physical) : \
		((unsigned long)(physical) & 0x80000000UL) ? \
		((unsigned long)(physical) - 0x80000000UL + PLATFORM_CONTIGUOUS_BASE) : \
		(PLATFORM_CONTIGUOUS_BASE + (unsigned long)(physical))))
#define PLATFORM_VIRTUAL_TO_PHYSICAL(address) \
	(((unsigned long)(address) >= PLATFORM_CONTIGUOUS_BASE && \
	  (unsigned long)(address) - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE) ? \
	 (unsigned long)(address) - PLATFORM_CONTIGUOUS_BASE : (unsigned long)(address))

/* The window Halo Custom Edition tag data are linked to (0x40440000):
reserved at start-up when the game.custom_edition setting is on, else NULL
(also declared for the game in halo_linux_source_fixups.h). */
void *halo_custom_edition_tag_cache(void);
/* which textures hold their channels where Halo PC keeps them
(xbox_textures.c; also declared for the game there) */
void halo_custom_edition_texels_channels(const void *texels, unsigned char channel_order);
void halo_custom_edition_texels_forget(void);

/* ---------- guest memory write tracking (memory_watch.c)

Pages of the contiguous window that the renderer has cached (textures) are
write-protected; the first write marks them written and unprotects them.
Page generations let a cache entry tell whether any of its pages changed
since it was built. */

void memory_watch_initialize(void);
void memory_watch_protect(unsigned long address, unsigned long size);
/* newest write generation of any page in the range */
unsigned long memory_watch_generation(unsigned long address, unsigned long size);
/* changes whenever any page's generation does: while it stays the same, so
do all generations */
unsigned long memory_watch_serial(void);
/* call before the host itself (read(), the kernel) writes into the range */
void memory_watch_prepare_write(void *address, unsigned long size);
/* the length of the contiguous block starting at address, or 0 */
unsigned long platform_contiguous_block_size(const void *address);
/* the range was remapped or reprotected: treat it as written and unwatched */
void memory_watch_forget(void *address, unsigned long size);
/* a new frame starts (Present): tracking that compares page contents
(Android under ARM translation, host_watch_hash.h) hashes a page at most
once a frame, so it sees a write in the next frame, not at once */
void memory_watch_begin_frame(void);

/* ---------- time */

/* 100 ns intervals since 1601-01-01, as FILETIME uses */
void platform_unix_time_to_filetime(unsigned long seconds, unsigned long nanoseconds, FILETIME *file_time);
void platform_filetime_to_unix_time(const FILETIME *file_time, unsigned long *seconds, unsigned long *nanoseconds);

#endif
