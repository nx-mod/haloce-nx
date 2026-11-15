/*
HOST_MEMORY.C

Guest address space for the PS5 port: Android's host_memory.c
(port/android/host) with the system underneath replaced by the console's,
as the Switch rewrote its own over its services. The bookkeeping is
Android's: the pools (struct pool, pool_take, pool_release), the guest's
page map (guest_pages), the rules of host_guest_mmap, the write watch and
the crash report.

What is underneath (port/ps5/PS5.md, phase 2, and phase 0 B's findings):

- Everything the guest can address is direct memory, mapped at fixed
  addresses below 4 GB: a range is reserved with
  sceKernelReserveVirtualRange (checked to have landed where asked: the
  kernel moves a reservation it cannot place), allocated with
  sceKernelAllocateDirectMemory and mapped there read-write with
  sceKernelMapDirectMemory. Flexible memory (448 MB, libc's mmap) is not
  used: direct memory is the title's one budget of 12 GB, which RADV and the
  host's own heap share.
- The guest's three fixed ranges are claimed at start-up
  (host_memory_reserve_early), before any pool: the image's
  (HALO_GUEST_IMAGE_BASE, 64 MB), Custom Edition's tag cache, and the Xbox
  window at the first 256 MB boundary from 0x80000000 where 512 MB are free
  (0x90000000 on this console: PS5_Vulkan's libc.prx holds 0x80000000). The
  guest is built with the window movable (HALO_MOVABLE_WINDOW), so it is
  told where the window is through the boot block and from then on uses
  the window's real addresses: unlike Android's, this file never moves an
  address between the two.
- Pools of 256 MB for everything else the guest maps (its malloc, thread
  stacks), each reserved whole as direct memory, in the lowest gap of the
  map at or above 16 MB, as the console's map reads (sceKernelVirtualQuery).
- Direct memory comes back holding what it held: a page is zeroed when it
  is given out (host_low_map, a guest mapping), not when it is reserved.
  Given back, it is made inaccessible (PROT_NONE) and kept: the address
  space stays reserved, as Android's does.
- Protection is libc's mprotect, which on direct memory is
  sceKernelMprotect's (phase 0 B), in the console's 16 KB pages. madvise is
  answered without a call: the guest's musl never relies on what it does.

The loader maps the image's range itself (host_loader.c: an anonymous
MAP_FIXED mmap), which on the PS5 would put flexible memory over the direct
memory reserved for it. The link wraps mmap (--wrap=mmap, tools/ps5_build.py):
__wrap_mmap answers an anonymous MAP_FIXED mapping inside the guest's
ranges by zeroing it and setting its protection, and passes every other
mapping on (RADV's, SDL2's and glslang's, above 4 GB). The first ten calls
are logged, and the counts at the end (ps5_memory_report), so phase 4 sees
whether RADV ever maps below 4 GB.

The write tracking (port/linux/src/memory_watch.c's interface) is
Android's: pages of the window write-protected, the SIGSEGV handler marking
the first write to each; a title gets the fault with its context on its
alternate stack (phase 0 B). SIGBUS is checked for a watched page too.
Other faults are reported with the registers (host_arch.c) and passed on.
*/

#include "host.h"
#include "ps5_host.h"
#include "freebsd_abi.h"
#include "host_syscall_tables.h"
#include "host_watch_hash.h"

#include <ps5platform/heap.h>
#include <ps5platform/kernel.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

/* The guest's pages are Linux's, 4 KB: its musl maps, protects and unmaps
in them. The console's are 16 KB (sysconf(_SC_PAGESIZE)), and a protection
is the system's for a whole one. So the guest's pages are kept in 4 KB,
with a protection each (page_protection), and each system page is given
the union of its four (set_protection): a page made inaccessible next to
one in use stays readable and writable (a guard page that does not
guard), and nothing the guest uses is ever made inaccessible. A mapping
(host_low_map) starts on a system page of its own and takes the guest's
pages it needs, so that its protections (host_thread.c's guard pages) never
reach a neighbour; the rest of its last system page is never handed out
(pool_take starts a run on a system page). It takes no more pages than an
unmap of its length gives back: when it took whole system pages, each map
of a piece (256 KB and a header) left three pages taken after its unmap,
and the freed ones were too few for the next piece, so map sharing's
download used up the pools (port/ps5/PS5.md, phase 5). */
#define PAGE 0x1000ULL
#define SYSTEM_PAGE 0x4000ULL
#define PAGES_PER_SYSTEM_PAGE (SYSTEM_PAGE / PAGE)
#define LOW_LIMIT 0x100000000ULL
#define LOW_START 0x01000000ULL
#define POOL_SIZE (256ULL * 1024 * 1024)
#define POOL_PAGES (POOL_SIZE / PAGE)
#define MAXIMUM_POOLS 12
#define MEGABYTE (1024.0 * 1024.0)

_Static_assert(SYSTEM_PAGE == PS5_KERNEL_PAGE_SIZE, "the console's page");

/* sceKernelVirtualQuery, which libkernel exports and the SDK's headers do
not declare (the guest probe's declaration: the PS4's call, and its 72-byte
SceKernelVirtualQueryInfo, of which only the range is read here) */
int32_t sceKernelVirtualQuery(const void *address, int flags, void *info, size_t size);
#define VIRTUAL_QUERY_FIND_NEXT 1

struct virtual_query_info
{
	uint64_t start;
	uint64_t end;
	uint64_t offset;
	int32_t protection;
	int32_t memory_type;
	uint8_t rest[40];
};

_Static_assert(sizeof(struct virtual_query_info) == 72, "SceKernelVirtualQueryInfo");

/* the mmap the link wraps (__wrap_mmap); hidden, as the title converter
refuses a title that exports a name (the system's own modules call mmap,
and the link would otherwise export the wrap to them) */
void *__real_mmap(void *address, size_t length, int protection, int flags, int fd, off_t offset);
__attribute__((visibility("hidden"))) void *__wrap_mmap(void *address, size_t length, int protection, int flags,
	int fd, off_t offset);

struct pool
{
	uint64_t base;
	uint32_t free_pages;
	uint8_t used[POOL_PAGES]; /* 1 for each page handed out */
};

/* A pool is never freed, and its base never changes: pool_count is
published after its entry (with release ordering), so host_low_owns reads
both without the lock, from signal handlers too. */
static struct pool *pools[MAXIMUM_POOLS];
static int pool_count;
static pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t window_base, window_end, window_size;
static uint64_t image_base, image_end;
static int custom_edition_reserved;
/* the image's whole range, reserved at start-up: the image is loaded into
its first part (host_memory_initialize) */
static uint64_t image_range_end;

static uint64_t round_up(uint64_t value)
{
	return (value + PAGE - 1) & ~(PAGE - 1);
}

static int in_range(uint64_t address, uint64_t size, uint64_t base, uint64_t end)
{
	return address >= base && address + size <= end && address + size >= address;
}

/* ---------- direct memory at fixed addresses */

/* reserves size bytes exactly at address, or nothing: 0, the kernel's
result, or -1 when it placed the range elsewhere (the guest probe's
reserve_exactly) */
static int32_t reserve_exactly(uint64_t address, uint64_t size)
{
	void *at = (void *)(uintptr_t)address;
	int32_t result = sceKernelReserveVirtualRange(&at, size, 0, PS5_KERNEL_DIRECT_ALIGNMENT);

	if (result != 0)
		return result;
	if ((uint64_t)(uintptr_t)at != address)
	{
		sceKernelMunmap(at, size);
		return -1;
	}
	return 0;
}

/* claims [address, address + size) for the guest: reserved there,
allocated as direct memory and mapped read-write, not zeroed. 0, or -1
with the reason logged when what says what it is for */
static int reserve(uint64_t address, uint64_t size, const char *what)
{
	int64_t physical = 0;
	void *at = (void *)(uintptr_t)address;
	int32_t result = reserve_exactly(address, size);

	if (result != 0)
	{
		if (what)
			host_logf(HOST_LOG_INFO, "%s: %08llx-%08llx is taken (%s, 0x%08x)", what, (unsigned long long)address,
				(unsigned long long)(address + size), result == -1 ? "the reservation was placed elsewhere" :
				"the reservation was refused", (unsigned)result);
		return -1;
	}
	result = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, PS5_KERNEL_DIRECT_ALIGNMENT,
		PS5_KERNEL_DIRECT_TYPE_CPU, &physical);
	if (result != 0)
	{
		host_logf(HOST_LOG_ERROR, "%s: no %.0f MB of direct memory (0x%08x)", what ? what : "guest memory",
			size / MEGABYTE, (unsigned)result);
		sceKernelMunmap(at, size);
		return -1;
	}
	result = sceKernelMapDirectMemory(&at, size, PS5_KERNEL_PROT_CPU_READ | PS5_KERNEL_PROT_CPU_WRITE,
		PS5_KERNEL_MAP_FIXED, physical, PS5_KERNEL_DIRECT_ALIGNMENT);
	if (result != 0 || (uint64_t)(uintptr_t)at != address)
	{
		host_logf(HOST_LOG_ERROR, "%s: direct memory mapped at %p, not %08llx (0x%08x)", what ? what : "guest memory",
			at, (unsigned long long)address, (unsigned)result);
		sceKernelMunmap((void *)(uintptr_t)address, size);
		sceKernelReleaseDirectMemory(physical, size);
		return -1;
	}
	return 0;
}

/* ---------- protection, in the guest's pages */

/* each 4 KB page's protection, below 4 GB (PROT_NONE until given out) */
static uint8_t page_protection[LOW_LIMIT / PAGE];
static pthread_mutex_t protection_lock = PTHREAD_MUTEX_INITIALIZER;

/* the union of a system page's four */
static int system_protection(uint64_t system_page)
{
	uint64_t first = system_page / PAGE, index;
	int protection = 0;

	for (index = first; index < first + PAGES_PER_SYSTEM_PAGE; index++)
		protection |= page_protection[index];
	return protection;
}

/* the system pages of [first, end) get their unions, a run of equal ones
in one call; 0 or -1 with errno */
static int apply_protection(uint64_t first, uint64_t end)
{
	uint64_t run = first;
	int protection = first < end ? system_protection(first) : 0;

	while (run < end)
	{
		uint64_t at = run + SYSTEM_PAGE;

		while (at < end && system_protection(at) == protection)
			at += SYSTEM_PAGE;
		if (mprotect((void *)(uintptr_t)run, at - run, protection) != 0)
			return -1;
		run = at;
		if (run < end)
			protection = system_protection(run);
	}
	return 0;
}

/* the guest's pages [address, address + length) get the protection, and
are zeroed first when zero says (a new mapping); 0 or -1 with errno */
static int set_protection(uint64_t address, uint64_t length, int protection, int zero)
{
	uint64_t first = address & ~(SYSTEM_PAGE - 1);
	uint64_t end = (address + length + SYSTEM_PAGE - 1) & ~(SYSTEM_PAGE - 1);
	uint64_t page;
	int result = 0;

	pthread_mutex_lock(&protection_lock);
	if (zero)
	{
		/* (writable for the zeroing; the neighbours' pages lose nothing) */
		for (page = address / PAGE; page < (address + length) / PAGE; page++)
			page_protection[page] = PROT_READ | PROT_WRITE;
		result = apply_protection(first, end);
		if (result == 0)
			memset((void *)(uintptr_t)address, 0, length);
	}
	for (page = address / PAGE; page < (address + length) / PAGE; page++)
		page_protection[page] = (uint8_t)(protection & (PROT_READ | PROT_WRITE | PROT_EXEC));
	if (result == 0)
		result = apply_protection(first, end);
	pthread_mutex_unlock(&protection_lock);
	return result;
}

/* gives pages out: zeroed, with the protection asked for */
static int give_out(uint64_t address, uint64_t length, int protection)
{
	return set_protection(address, length, protection, 1);
}

/* ---------- the map */

/* the guest's fixed ranges, for the gap search */
static int crosses_fixed(uint64_t address, uint64_t size)
{
	return (address < HALO_GUEST_CUSTOM_EDITION_END && address + size > HALO_GUEST_CUSTOM_EDITION_BASE) ||
		(address < HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE && address + size > HALO_GUEST_IMAGE_BASE) ||
		(window_size && address < window_end && address + size > window_base);
}

/* the lowest gap of at least size bytes at or above minimum, on an
alignment boundary, below 4 GB and clear of the fixed ranges, from the
console's map (each region from minimum up: sceKernelVirtualQuery with the
find-next flag, as the guest probe walked it); 0 if none */
static uint64_t find_gap(uint64_t size, uint64_t minimum, uint64_t alignment)
{
	uint64_t cursor = (minimum + alignment - 1) & ~(alignment - 1);
	int regions;

	for (regions = 0; regions < 4096 && cursor + size <= LOW_LIMIT; regions++)
	{
		struct virtual_query_info info;
		uint64_t next;

		memset(&info, 0, sizeof(info));
		if (sceKernelVirtualQuery((const void *)(uintptr_t)cursor, VIRTUAL_QUERY_FIND_NEXT, &info, sizeof(info)) != 0)
			info.start = info.end = LOW_LIMIT;
		if (info.start >= cursor + size && !crosses_fixed(cursor, size))
			return cursor;
		next = info.start >= cursor + size ? cursor + alignment : info.end;
		if (crosses_fixed(cursor, size) && next <= cursor + alignment)
			next = cursor + alignment;
		cursor = (next + alignment - 1) & ~(alignment - 1);
	}
	return 0;
}

static void log_region(const struct virtual_query_info *info, FILE *file)
{
	host_logf(HOST_LOG_INFO, "low mapping: %08llx-%08llx (%.1f MB), protection 0x%x, type %d",
		(unsigned long long)info->start, (unsigned long long)info->end, (info->end - info->start) / MEGABYTE,
		(unsigned)info->protection, info->memory_type);
	if (file)
		fprintf(file, "%08llx-%08llx protection 0x%x type %d\n", (unsigned long long)info->start,
			(unsigned long long)info->end, (unsigned)info->protection, info->memory_type);
}

/* the console's regions that cross a range, so a refusal says what was in
the way */
static void log_conflicts(uint64_t address, uint64_t size)
{
	uint64_t cursor = address;
	int regions;

	for (regions = 0; regions < 64 && cursor < address + size; regions++)
	{
		struct virtual_query_info info;

		memset(&info, 0, sizeof(info));
		if (sceKernelVirtualQuery((const void *)(uintptr_t)cursor, VIRTUAL_QUERY_FIND_NEXT, &info, sizeof(info)) != 0 ||
			info.start >= address + size)
			break;
		host_logf(HOST_LOG_ERROR, "  in the way: %08llx-%08llx, protection 0x%x", (unsigned long long)info.start,
			(unsigned long long)info.end, (unsigned)info.protection);
		cursor = info.end;
	}
}

void host_memory_report_low_mappings(FILE *file)
{
	uint64_t cursor = 0;
	int regions;

	if (file)
		fprintf(file, "mappings below 4 GB:\n");
	for (regions = 0; regions < 4096 && cursor < LOW_LIMIT; regions++)
	{
		struct virtual_query_info info;

		memset(&info, 0, sizeof(info));
		if (sceKernelVirtualQuery((const void *)(uintptr_t)cursor, VIRTUAL_QUERY_FIND_NEXT, &info, sizeof(info)) != 0 ||
			info.start >= LOW_LIMIT || info.end <= cursor)
			break;
		log_region(&info, file);
		cursor = info.end;
	}
}

/* ---------- the fixed ranges and the pools */

static struct pool *pool_new(void)
{
	uint64_t minimum = LOW_START;
	struct pool *pool;
	int attempt;

	if (pool_count == MAXIMUM_POOLS)
	{
		host_logf(HOST_LOG_ERROR, "guest memory: all %d pools are in use", MAXIMUM_POOLS);
		return NULL;
	}
	for (attempt = 0; attempt < 64; attempt++)
	{
		uint64_t address = find_gap(POOL_SIZE, minimum, PS5_KERNEL_DIRECT_ALIGNMENT);
		uint64_t start = ps5_now_ns();

		if (!address)
		{
			host_logf(HOST_LOG_ERROR, "guest memory: no room below 4 GB for a pool of %.0f MB", POOL_SIZE / MEGABYTE);
			return NULL;
		}
		if (reserve(address, POOL_SIZE, NULL) == 0)
		{
			pool = calloc(1, sizeof(*pool));
			if (!pool)
			{
				host_logf(HOST_LOG_ERROR, "guest memory: out of memory for a pool's page map");
				return NULL;
			}
			/* (inaccessible until its pages are given out) */
			mprotect((void *)(uintptr_t)address, POOL_SIZE, PROT_NONE);
			pool->base = address;
			pool->free_pages = POOL_PAGES;
			pools[pool_count] = pool;
			__atomic_store_n(&pool_count, pool_count + 1, __ATOMIC_RELEASE);
			host_logf(HOST_LOG_INFO, "guest memory pool %d at %08llx, %.0f MB of direct memory, in %.1f ms",
				pool_count - 1, (unsigned long long)address, POOL_SIZE / MEGABYTE, (ps5_now_ns() - start) / 1e6);
			return pool;
		}
		/* taken meanwhile: look further up */
		minimum = address + PS5_KERNEL_DIRECT_ALIGNMENT;
	}
	return NULL;
}

uint32_t host_memory_window_base(void)
{
	return (uint32_t)window_base;
}

uint32_t host_memory_window_size(void)
{
	return (uint32_t)window_size;
}

/* (the write tracking's page hashes, below) */
static struct watch_hash page_hashes;

/* host_memory_fixed_unavailable: one of the three ranges could not be
claimed */
static int placement_failed;

static int reserve_fixed(const char *what, uint64_t address, uint64_t size)
{
	uint64_t start = ps5_now_ns();

	if (reserve(address, size, what) != 0)
		return -1;
	host_logf(HOST_LOG_INFO, "%s: %08llx-%08llx, %.0f MB of direct memory, in %.1f ms", what,
		(unsigned long long)address, (unsigned long long)(address + size), size / MEGABYTE,
		(ps5_now_ns() - start) / 1e6);
	return 0;
}

void host_memory_reserve_early(void)
{
	uint64_t candidate;

	if (reserve_fixed("guest image range", HALO_GUEST_IMAGE_BASE, HALO_GUEST_IMAGE_RESERVE) == 0)
		image_range_end = (uint64_t)HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE;
	else
	{
		log_conflicts(HALO_GUEST_IMAGE_BASE, HALO_GUEST_IMAGE_RESERVE);
		placement_failed = 1;
	}
	custom_edition_reserved = reserve_fixed("Custom Edition's tag cache", HALO_GUEST_CUSTOM_EDITION_BASE,
		HALO_GUEST_CUSTOM_EDITION_END - HALO_GUEST_CUSTOM_EDITION_BASE) == 0;
	if (!custom_edition_reserved)
	{
		log_conflicts(HALO_GUEST_CUSTOM_EDITION_BASE, HALO_GUEST_CUSTOM_EDITION_END - HALO_GUEST_CUSTOM_EDITION_BASE);
		placement_failed = 1;
	}
	/* the window: at 0x80000000 if it is free, else on the next whole 256 MB
	that is (the game turns window offsets into addresses by masking) */
	for (candidate = HALO_GUEST_WINDOW_BASE; candidate + HALO_GUEST_WINDOW_SIZE <= LOW_LIMIT;
		candidate += HALO_GUEST_WINDOW_ALIGNMENT)
	{
		if (reserve_fixed("Xbox window", candidate, HALO_GUEST_WINDOW_SIZE) == 0)
		{
			window_base = candidate;
			window_size = HALO_GUEST_WINDOW_SIZE;
			window_end = window_base + window_size;
			/* (the page hashes follow the window where it is) */
			page_hashes.base = (const uint8_t *)(uintptr_t)window_base;
			break;
		}
	}
	if (!window_size)
	{
		host_logf(HOST_LOG_ERROR, "no room below 4 GB for the Xbox window's %.0f MB",
			HALO_GUEST_WINDOW_SIZE / MEGABYTE);
		placement_failed = 1;
	}
	/* (inaccessible until the guest maps them) */
	if (custom_edition_reserved)
		mprotect((void *)(uintptr_t)HALO_GUEST_CUSTOM_EDITION_BASE,
			HALO_GUEST_CUSTOM_EDITION_END - HALO_GUEST_CUSTOM_EDITION_BASE, PROT_NONE);
	if (window_size)
		mprotect((void *)(uintptr_t)window_base, window_size, PROT_NONE);
}

int host_memory_fixed_unavailable(void)
{
	return placement_failed;
}

int host_memory_initialize(uint32_t preferred_base, uint32_t size, uint32_t *base)
{
	/* (claimed at start-up: the x32 image is never moved) */
	if (!image_range_end || preferred_base < HALO_GUEST_IMAGE_BASE ||
		(uint64_t)preferred_base + round_up(size) > image_range_end || !window_size)
	{
		host_logf(HOST_LOG_ERROR, "the guest image's range %08x-%08llx is not in what was reserved for it (%08x-%08llx)%s",
			preferred_base, (unsigned long long)preferred_base + round_up(size), HALO_GUEST_IMAGE_BASE,
			(unsigned long long)image_range_end, window_size ? "" : ", or there is no window");
		placement_failed = 1;
		return -1;
	}
	image_base = preferred_base;
	image_end = preferred_base + round_up(size);
	*base = preferred_base;
	host_logf(HOST_LOG_INFO, "Xbox memory window of %u MB at %08llx-%08llx, guest image at %08llx-%08llx",
		(unsigned int)(window_size >> 20), (unsigned long long)window_base, (unsigned long long)window_end,
		(unsigned long long)image_base, (unsigned long long)image_end);
	return 0;
}

/* ---------- page pools */

/* a run of free pages that starts on a system page (host_low_map's) */
static void *pool_take(struct pool *pool, uint64_t pages)
{
	uint64_t run = 0, page;

	if (pool->free_pages < pages)
		return NULL;
	for (page = 0; page < POOL_PAGES; page++)
	{
		if (pool->used[page] || (!run && page % PAGES_PER_SYSTEM_PAGE))
		{
			run = 0;
			continue;
		}
		if (++run == pages)
		{
			uint64_t first = page + 1 - pages;

			memset(&pool->used[first], 1, pages);
			pool->free_pages -= pages;
			return (void *)(uintptr_t)(pool->base + first * PAGE);
		}
	}
	return NULL;
}

void *host_low_map(size_t size, int protection)
{
	uint64_t pages = round_up(size) / PAGE;
	void *address = NULL;
	int index;

	if (!pages || pages > POOL_PAGES)
		return NULL;
	pthread_mutex_lock(&memory_lock);
	for (index = 0; index < pool_count && !address; index++)
		address = pool_take(pools[index], pages);
	if (!address)
	{
		struct pool *pool = pool_new();

		if (pool)
			address = pool_take(pool, pages);
	}
	pthread_mutex_unlock(&memory_lock);
	if (!address)
		return NULL;
	if (give_out((uint64_t)(uintptr_t)address, pages * PAGE, protection) != 0)
	{
		host_low_unmap(address, pages * PAGE);
		return NULL;
	}
	return address;
}

static struct pool *pool_of(uint64_t address, uint64_t size)
{
	int count = __atomic_load_n(&pool_count, __ATOMIC_ACQUIRE);
	int index;

	for (index = 0; index < count; index++)
	{
		if (in_range(address, size, pools[index]->base, pools[index]->base + POOL_SIZE))
			return pools[index];
	}
	return NULL;
}

/* gives pages of a pool back, with the memory lock held: they become
inaccessible, and the direct memory stays */
static void pool_release(struct pool *pool, uint64_t start, uint64_t length)
{
	uint64_t first = (start - pool->base) / PAGE, count = length / PAGE, page;

	set_protection(start, length, PROT_NONE, 0);
	for (page = first; page < first + count; page++)
	{
		if (pool->used[page])
		{
			pool->used[page] = 0;
			pool->free_pages++;
		}
	}
}

void host_low_unmap(void *address, size_t size)
{
	uint64_t start = (uint64_t)(uintptr_t)address & ~(PAGE - 1);
	uint64_t length = round_up((uint64_t)(uintptr_t)address + size) - start;
	struct pool *pool;

	pthread_mutex_lock(&memory_lock);
	pool = pool_of(start, length);
	if (pool)
		pool_release(pool, start, length);
	pthread_mutex_unlock(&memory_lock);
}

static int in_custom_edition(uint64_t address, uint64_t size)
{
	return custom_edition_reserved &&
		in_range(address, size, HALO_GUEST_CUSTOM_EDITION_BASE, HALO_GUEST_CUSTOM_EDITION_END);
}

int host_low_owns(uintptr_t address, size_t size)
{
	if (in_range(address, size, window_base, window_end) || in_range(address, size, image_base, image_end) ||
		in_custom_edition(address, size))
	{
		return 1;
	}
	return pool_of(address, size) != NULL;
}

/* ---------- the guest's memory system calls

As Android's: the guest maps, unmaps, reprotects and advises only memory
that is its own: the pools' pages, the Xbox window, Custom Edition's tag
cache and the image's range. What each page is in use for is kept so that
a "no replace" mapping fails where something is mapped, as the kernel's
does: a pool's used[], and guest_pages for the window and the tag cache.
Here those ranges are all direct memory the host reserved, so a mapping
anywhere else is refused (-ENOMEM), where Android's asks the kernel. All
under the memory lock. The flags are the guest's (Linux's). */

/* a bit for each page below 4 GB of the window and the tag cache that the
guest has mapped */
static uint8_t guest_pages[LOW_LIMIT / PAGE / 8];

static int guest_page(uint64_t address)
{
	uint64_t page = address / PAGE;

	return (guest_pages[page >> 3] >> (page & 7)) & 1;
}

static void set_guest_pages(uint64_t address, uint64_t length, int value)
{
	uint64_t page;

	for (page = address / PAGE; page < (address + length) / PAGE; page++)
	{
		if (value)
			guest_pages[page >> 3] |= (uint8_t)(1 << (page & 7));
		else
			guest_pages[page >> 3] &= (uint8_t)~(1 << (page & 7));
	}
}

/* what a page below 4 GB is */
enum
{
	PAGE_HOST, /* the host's, or free address space */
	PAGE_POOL,
	PAGE_WINDOW,
	PAGE_IMAGE,
	PAGE_CUSTOM_EDITION,
};

static int page_kind(uint64_t address, struct pool **pool)
{
	*pool = NULL;
	if (in_range(address, PAGE, window_base, window_end))
		return PAGE_WINDOW;
	if (in_range(address, PAGE, image_base, image_end))
		return PAGE_IMAGE;
	if (in_custom_edition(address, PAGE))
		return PAGE_CUSTOM_EDITION;
	*pool = pool_of(address, PAGE);
	if (*pool)
		return PAGE_POOL;
	return PAGE_HOST;
}

/* the end of the run of pages from address (before end) of the same kind,
and pool, as the first */
static uint64_t run_end(uint64_t address, uint64_t end, int *kind, struct pool **pool)
{
	struct pool *next_pool;

	*kind = page_kind(address, pool);
	for (address += PAGE; address < end; address += PAGE)
	{
		if (page_kind(address, &next_pool) != *kind || next_pool != *pool)
			break;
	}
	return address;
}

/* whether any page of a run holds a mapping */
static int run_in_use(uint64_t address, uint64_t end, int kind, const struct pool *pool)
{
	for (; address < end; address += PAGE)
	{
		if (kind == PAGE_IMAGE)
			return 1;
		if (kind == PAGE_POOL && pool->used[(address - pool->base) / PAGE])
			return 1;
		if ((kind == PAGE_WINDOW || kind == PAGE_CUSTOM_EDITION) && guest_page(address))
			return 1;
	}
	return 0;
}

/* whether every page of a range is the guest's (PAGE_IMAGE too, unless
image is 0), with the memory lock held */
static int guest_owns(uint64_t address, uint64_t end, int image)
{
	while (address < end)
	{
		struct pool *pool;
		int kind;

		address = run_end(address, end, &kind, &pool);
		if (kind == PAGE_HOST || (kind == PAGE_IMAGE && !image))
			return 0;
	}
	return 1;
}

/* the write tracking forgets the window's pages that were remapped or
reprotected */
static void forget_window_range(uint64_t address, uint64_t length)
{
	uint64_t start = address, end = address + length;

	if (start < window_base)
		start = window_base;
	if (end > window_end)
		end = window_end;
	if (start < end)
		host_memory_watch_forget((uint32_t)start, (uint32_t)(end - start));
}

/* the guest's mapping of [address, address + length): zeros, or a file's
bytes read into it (the Switch's way: there are no file mappings here, so
a MAP_SHARED one is a copy too), then its protection */
static long fill(uint64_t address, uint64_t length, int protection, int fd, int64_t offset)
{
	if (set_protection(address, length, fd != -1 ? PROT_READ | PROT_WRITE : protection, 1) != 0)
		return -ps5_errno_out(errno);
	if (fd != -1)
	{
		static int logged;
		ssize_t got = pread(fd, (void *)(uintptr_t)address, length, (off_t)offset);

		if (got < 0)
			return -ps5_errno_out(errno);
		if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED))
			host_logf(HOST_LOG_INFO, "guest memory: the first file mapping, %llu bytes of descriptor %d at %lld, "
				"read into memory", (unsigned long long)length, fd, (long long)offset);
	}
	if (fd != -1 && set_protection(address, length, protection, 0) != 0)
		return -ps5_errno_out(errno);
	return 0;
}

static long guest_mmap_fixed(uint64_t address, uint64_t length, int protection, int flags, int fd, int64_t offset)
{
	uint64_t end = address + length, at;
	int replace = !(flags & GUEST_MAP_FIXED_NOREPLACE);
	int host = 0, in_use = 0;
	long status;

	if (address & (PAGE - 1))
		return -GUEST_EINVAL;
	if (end > LOW_LIMIT || end < address)
		return -GUEST_ENOMEM;
	pthread_mutex_lock(&memory_lock);
	for (at = address; at < end;)
	{
		struct pool *pool;
		int kind;
		uint64_t next = run_end(at, end, &kind, &pool);

		if (kind == PAGE_HOST)
			host = 1;
		else
			in_use |= run_in_use(at, next, kind, pool);
		at = next;
	}
	if (host)
	{
		/* outside the direct memory reserved for the guest */
		status = -GUEST_ENOMEM;
		goto done;
	}
	if (!replace && in_use)
	{
		status = -GUEST_EEXIST;
		goto done;
	}
	status = fill(address, length, protection, fd, offset);
	if (status < 0)
		goto done;
	for (at = address; at < end;)
	{
		struct pool *pool;
		int kind;
		uint64_t next = run_end(at, end, &kind, &pool);
		uint64_t page;

		if (kind == PAGE_POOL)
		{
			/* (so that host_low_map does not hand them out again) */
			for (page = (at - pool->base) / PAGE; page < (next - pool->base) / PAGE; page++)
			{
				if (!pool->used[page])
				{
					pool->used[page] = 1;
					pool->free_pages--;
				}
			}
		}
		else if (kind == PAGE_WINDOW || kind == PAGE_CUSTOM_EDITION)
			set_guest_pages(at, next - at, 1);
		at = next;
	}
	forget_window_range(address, length);
	status = (long)address;
done:
	pthread_mutex_unlock(&memory_lock);
	return status;
}

long host_guest_mmap(uint64_t address, uint64_t size, int protection, int flags, int fd, int64_t offset)
{
	uint64_t length = round_up(size);
	void *result;
	long status;

	if (!length || length < size)
		return -GUEST_EINVAL;
	/* (an anonymous mapping's descriptor is ignored, as Linux does) */
	if (flags & GUEST_MAP_ANONYMOUS)
		fd = -1;
	else if (fd == -1)
		return -GUEST_EBADF;
	if (flags & (GUEST_MAP_FIXED | GUEST_MAP_FIXED_NOREPLACE))
		return guest_mmap_fixed(address, length, protection, flags, fd, offset);
	if (fd == -1)
	{
		result = host_low_map(length, protection);
		return result ? (long)(uintptr_t)result : -GUEST_ENOMEM;
	}
	result = host_low_map(length, PROT_READ | PROT_WRITE);
	if (!result)
		return -GUEST_ENOMEM;
	status = fill((uint64_t)(uintptr_t)result, length, protection, fd, offset);
	if (status < 0)
	{
		host_low_unmap(result, length);
		return status;
	}
	return (long)(uintptr_t)result;
}

/* pool pages go back to their pool, the window's and the tag cache's
become inaccessible again; nothing is done unless all of the range is one
of these (the image is never unmapped) */
long host_guest_munmap(uint64_t address, uint64_t size)
{
	uint64_t length = round_up(size), end = address + length, at;
	long status = 0;

	if ((address & (PAGE - 1)) || !length || length < size)
		return -GUEST_EINVAL;
	if (end > LOW_LIMIT || end < address)
		return -GUEST_EINVAL;
	pthread_mutex_lock(&memory_lock);
	if (!guest_owns(address, end, 0))
	{
		status = -GUEST_EINVAL;
		goto done;
	}
	for (at = address; at < end;)
	{
		struct pool *pool;
		int kind;
		uint64_t next = run_end(at, end, &kind, &pool);

		if (kind == PAGE_POOL)
			pool_release(pool, at, next - at);
		else if (kind == PAGE_WINDOW || kind == PAGE_CUSTOM_EDITION)
		{
			set_protection(at, next - at, PROT_NONE, 0);
			set_guest_pages(at, next - at, 0);
		}
		at = next;
	}
	forget_window_range(address, length);
done:
	pthread_mutex_unlock(&memory_lock);
	return status;
}

/* mprotect and madvise only on memory that is the guest's (above). The
write tracking forgets what they change in the Xbox window. */
static long guest_range(uint64_t address, uint64_t size, uint64_t *length)
{
	int owned;

	if (address & (PAGE - 1))
		return -GUEST_EINVAL;
	*length = round_up(size);
	if (*length < size)
		return -GUEST_ENOMEM;
	if (!*length)
		return 0;
	if (address + *length > LOW_LIMIT || address + *length < address)
		return -GUEST_ENOMEM;
	pthread_mutex_lock(&memory_lock);
	owned = guest_owns(address, address + *length, 1);
	pthread_mutex_unlock(&memory_lock);
	return owned ? 1 : -GUEST_ENOMEM;
}

long host_guest_mprotect(uint64_t address, uint64_t size, int protection)
{
	uint64_t length;
	long check = guest_range(address, size, &length);

	if (check <= 0)
		return check;
	if (set_protection(address, length, protection, 0) != 0)
		return -ps5_errno_out(errno);
	forget_window_range(address, length);
	return 0;
}

/* (musl's malloc advises MADV_DONTNEED or MADV_FREE on memory it no longer
uses, and never relies on what that does: on direct memory, which nothing
reclaims, nothing is done) */
static uint32_t madvise_calls;

long host_guest_madvise(uint64_t address, uint64_t size, int advice)
{
	uint64_t length;
	long check = guest_range(address, size, &length);

	(void)advice;
	if (check <= 0)
		return check;
	__atomic_fetch_add(&madvise_calls, 1, __ATOMIC_RELAXED);
	return 0;
}

/* ---------- the loader's mapping (__wrap_mmap) */

static uint32_t wrapped_answered, wrapped_passed, wrapped_passed_low;
static uint32_t wrapped_logged;

/* whether [address, address + length) lies in the direct memory the guest's
ranges hold (before the image is loaded too: its whole range) */
static int reserved_for_guest(uint64_t address, uint64_t length)
{
	return (image_range_end && in_range(address, length, HALO_GUEST_IMAGE_BASE, image_range_end)) ||
		in_range(address, length, window_base, window_end) || in_custom_edition(address, length) ||
		pool_of(address, length) != NULL;
}

void *__wrap_mmap(void *address, size_t length, int protection, int flags, int fd, off_t offset)
{
	uint64_t at = (uint64_t)(uintptr_t)address;
	int answer = (flags & FREEBSD_MAP_FIXED) && (flags & FREEBSD_MAP_ANON) && fd == -1 && !(at & (PAGE - 1)) &&
		reserved_for_guest(at, round_up(length));
	void *result;

	if (answer)
	{
		result = give_out(at, round_up(length), protection) == 0 ? address : MAP_FAILED;
		__atomic_fetch_add(&wrapped_answered, 1, __ATOMIC_RELAXED);
	}
	else
	{
		result = __real_mmap(address, length, protection, flags, fd, offset);
		__atomic_fetch_add(&wrapped_passed, 1, __ATOMIC_RELAXED);
		if (result != MAP_FAILED && (uint64_t)(uintptr_t)result < LOW_LIMIT)
			__atomic_fetch_add(&wrapped_passed_low, 1, __ATOMIC_RELAXED);
	}
	if (__atomic_fetch_add(&wrapped_logged, 1, __ATOMIC_RELAXED) < 10)
		host_logf(HOST_LOG_INFO, "mmap(%p, %zu bytes, protection %d, flags 0x%x, descriptor %d): %s, %p", address,
			length, protection, (unsigned)flags, fd, answer ? "the guest's direct memory" : "the system's", result);
	return result;
}

/* ---------- the reports */

void ps5_memory_figures(double *heap_mb, double *heap_peak_mb, double *direct_free_mb)
{
	struct ps5_heap_stats heap;
	int64_t start = 0;
	size_t available = 0;

	memset(&heap, 0, sizeof(heap));
	ps5_heap_stats(&heap);
	sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(), PS5_KERNEL_DIRECT_ALIGNMENT, &start,
		&available);
	*heap_mb = heap.mapped_bytes / MEGABYTE;
	*heap_peak_mb = heap.peak_bytes / MEGABYTE;
	*direct_free_mb = available / MEGABYTE;
}

void ps5_memory_report(void)
{
	double heap, peak, direct;
	unsigned free_pages = 0;
	int index;

	for (index = 0; index < pool_count; index++)
		free_pages += pools[index]->free_pages;
	ps5_memory_figures(&heap, &peak, &direct);
	host_logf(HOST_LOG_INFO, "mmap through the wrap: %u answered in the guest's direct memory, %u passed to the system "
		"(%u of them landed below 4 GB)", wrapped_answered, wrapped_passed, wrapped_passed_low);
	host_logf(HOST_LOG_INFO, "guest memory: %d pools, %.0f MB of them free; madvise %u calls (not made); the host's heap "
		"%.0f MB (peak %.0f MB); direct memory's largest free block %.0f MB", pool_count,
		free_pages * (double)PAGE / MEGABYTE, madvise_calls, heap, peak, direct);
}

/* ---------- write tracking (port/linux/src/memory_watch.c) */

#define WATCH_PAGE_COUNT (HALO_GUEST_WINDOW_MAXIMUM / SYSTEM_PAGE)

static uint8_t page_protected[WATCH_PAGE_COUNT];
static uint32_t page_generation[WATCH_PAGE_COUNT];
static volatile uint32_t current_generation = 1;
static int watch_active;
static int watch_hashing;
static uint8_t page_hash_watched[WATCH_PAGE_COUNT];
static uint64_t page_hash[WATCH_PAGE_COUNT];
static uint32_t page_hashed_frame[WATCH_PAGE_COUNT];
static struct watch_hash page_hashes =
{
	.base = (const uint8_t *)(uintptr_t)HALO_GUEST_WINDOW_BASE,
	.page_count = WATCH_PAGE_COUNT,
	.watched = page_hash_watched,
	.hash = page_hash,
	.hashed_frame = page_hashed_frame,
	.generation = page_generation,
	.current_generation = &current_generation,
	.frame = 1,
};

static int in_window(uint64_t address)
{
	return address >= window_base && address - window_base < window_size;
}

static uint64_t watch_page(uint64_t address)
{
	return (address - window_base) / SYSTEM_PAGE;
}

static void mark_written(uint64_t page)
{
	page_generation[page] = __sync_add_and_fetch(&current_generation, 1);
	page_protected[page] = 0;
	mprotect((void *)(uintptr_t)(window_base + page * SYSTEM_PAGE), SYSTEM_PAGE, PROT_READ | PROT_WRITE);
}

static struct sigaction previous_segv, previous_bus, previous_ill;
/* the host's code, as loaded (host_install_signal_handlers) */
static uint64_t host_code_start, host_code_end;

/* the guest's code, the stack and the frame records at a crash, through the
log's sink (which neither locks nor uses stdio); the walk copies each record
without faulting (host_arch_read_memory) */
static void report_crash(int signal_number, siginfo_t *information, void *context)
{
	char line[256];
	uint64_t pc, sp, fp, lr;
	uint64_t returns[24];
	int count, index;

	/* (the report is the last thing the title writes: every line at once) */
	ps5_log_sync(1);
	host_arch_context(context, &pc, &sp, &fp, &lr);
	snprintf(line, sizeof(line), "signal %d (code %d) at address %p: pc %016llx sp %016llx fp %016llx", signal_number,
		information->si_code, information->si_addr, (unsigned long long)pc, (unsigned long long)sp,
		(unsigned long long)fp);
	host_log(HOST_LOG_ERROR, line);
	if (pc >= host_image.base && pc < host_image.end)
	{
		snprintf(line, sizeof(line), "  in the guest image: addr2line -e build/ps5/halo_guest_vk.elf 0x%llx",
			(unsigned long long)(uint32_t)pc);
		host_log(HOST_LOG_ERROR, line);
	}
	else if (pc >= host_code_start && pc < host_code_end)
	{
		snprintf(line, sizeof(line), "  in the host: llvm-symbolizer --obj=build/ps5/host/eboot.elf 0x%llx",
			(unsigned long long)(pc - host_code_start));
		host_log(HOST_LOG_ERROR, line);
	}
	for (index = 0; host_arch_register_line(context, index, line, sizeof(line)); index++)
		host_log(HOST_LOG_ERROR, line);
	count = host_arch_walk_frames(fp, host_arch_read_memory, returns, (int)(sizeof(returns) / sizeof(returns[0])));
	for (index = 0; index < count; index++)
	{
		if (returns[index] >= host_code_start && returns[index] < host_code_end)
			snprintf(line, sizeof(line), "  frame %2d: return %016llx (the host: eboot.elf 0x%llx)", index,
				(unsigned long long)returns[index], (unsigned long long)(returns[index] - host_code_start));
		else
			snprintf(line, sizeof(line), "  frame %2d: return %016llx%s", index, (unsigned long long)returns[index],
				returns[index] >= host_image.base && returns[index] < host_image.end ? " (the guest image)" : "");
		host_log(HOST_LOG_ERROR, line);
	}
}

static void chain(struct sigaction *previous, int signal_number, siginfo_t *information, void *context)
{
	sigaction(signal_number, previous, NULL);
	if (previous->sa_flags & SA_SIGINFO)
	{
		if (previous->sa_sigaction)
			previous->sa_sigaction(signal_number, information, context);
	}
	else if (previous->sa_handler != SIG_DFL && previous->sa_handler != SIG_IGN)
	{
		previous->sa_handler(signal_number);
	}
	/* returning re-executes the faulting instruction under the previous
	(or default) handler: the system's, which ends the title and puts the
	fault in the kernel log */
}

/* a write to a watched page of the window: marked and made writable again;
FreeBSD may raise SIGSEGV or SIGBUS for it (phase 0 B saw SIGSEGV, si_code
2), so both handlers ask */
static int watched_write(siginfo_t *information)
{
	uint64_t address = (uint64_t)(uintptr_t)information->si_addr;

	if (watch_active && in_window(address))
	{
		uint64_t page = watch_page(address);

		if (page_protected[page])
		{
			mark_written(page);
			return 1;
		}
	}
	return 0;
}

/* (each handler keeps the interrupted code's errno: a write tracking fault
can come between any call and its errno check) */
static void segv_handler(int signal_number, siginfo_t *information, void *context)
{
	int saved_errno = errno;

	if (!watched_write(information))
	{
		report_crash(signal_number, information, context);
		chain(&previous_segv, signal_number, information, context);
	}
	errno = saved_errno;
}

static void bus_handler(int signal_number, siginfo_t *information, void *context)
{
	int saved_errno = errno;

	if (!watched_write(information))
	{
		report_crash(signal_number, information, context);
		chain(&previous_bus, signal_number, information, context);
	}
	errno = saved_errno;
}

static void ill_handler(int signal_number, siginfo_t *information, void *context)
{
	int saved_errno = errno;

	report_crash(signal_number, information, context);
	chain(&previous_ill, signal_number, information, context);
	errno = saved_errno;
}

void host_install_signal_handlers(void)
{
	struct sigaction action;
	void *start = NULL, *end = NULL;
	uint32_t protection = 0;

	/* where the host's code was loaded: eboot.elf is linked at 0, and its
	code is the first region (so a crash's addresses are given as
	eboot.elf's) */
	if (sceKernelQueryMemoryProtection((void *)(uintptr_t)host_install_signal_handlers, &start, &end, &protection) == 0)
	{
		host_code_start = (uint64_t)(uintptr_t)start;
		host_code_end = (uint64_t)(uintptr_t)end;
	}

	memset(&action, 0, sizeof(action));
	action.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
	sigemptyset(&action.sa_mask);
	action.sa_sigaction = segv_handler;
	sigaction(SIGSEGV, &action, &previous_segv);
	action.sa_sigaction = bus_handler;
	sigaction(SIGBUS, &action, &previous_bus);
	action.sa_sigaction = ill_handler;
	sigaction(SIGILL, &action, &previous_ill);
}

void host_memory_watch_initialize(void)
{
	watch_active = 1;
}

void host_memory_watch_use_hashes(void)
{
	watch_hashing = 1;
}

/* memory_watch_protect: write-protects the pages of a range, or keeps their
hash under host_memory_watch_use_hashes; a range outside the window is
ignored */
void host_memory_watch_protect(uint32_t address, uint32_t size)
{
	uint64_t first, last, page;

	if (!watch_active || !size || !in_window(address))
		return;
	first = watch_page(address);
	last = watch_page((uint64_t)address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	if (watch_hashing)
	{
		watch_hash_protect(&page_hashes, (uint32_t)first, (uint32_t)last);
		return;
	}
	for (page = first; page <= last; page++)
	{
		if (!page_protected[page])
		{
			page_protected[page] = 1;
			mprotect((void *)(uintptr_t)(window_base + page * SYSTEM_PAGE), SYSTEM_PAGE, PROT_READ);
		}
	}
}

uint32_t host_memory_watch_serial(void)
{
	return current_generation;
}

/* memory_watch_generation: the newest generation of the pages of a range.
Under hashing this hashes the pages again, once a frame. */
uint32_t host_memory_watch_generation(uint32_t address, uint32_t size)
{
	uint64_t first, last, page;
	uint32_t newest = 0;

	if (!size || !in_window(address))
		return 0;
	first = watch_page(address);
	last = watch_page((uint64_t)address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	if (watch_hashing)
		return watch_hash_generation(&page_hashes, (uint32_t)first, (uint32_t)last);
	for (page = first; page <= last; page++)
	{
		if (page_generation[page] > newest)
			newest = page_generation[page];
	}
	return newest;
}

/* memory_watch_prepare_write: unprotects the pages the host itself is about
to write into */
void host_memory_watch_prepare_write(uint32_t address, uint32_t size)
{
	uint64_t start = address, first, last, page;

	if (!watch_active || watch_hashing || !size)
		return;
	if (start + size <= window_base || start >= window_end)
		return;
	if (start < window_base)
		start = window_base;
	first = watch_page(start);
	last = watch_page((uint64_t)address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		if (page_protected[page])
			mark_written(page);
	}
}

/* memory_watch_forget: the pages of a range were remapped or reprotected, so
treat them as written and unwatched */
void host_memory_watch_forget(uint32_t address, uint32_t size)
{
	uint64_t first, last, page;

	if (!size || !in_window(address))
		return;
	first = watch_page(address);
	last = watch_page((uint64_t)address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	if (watch_hashing)
	{
		watch_hash_forget(&page_hashes, (uint32_t)first, (uint32_t)last);
		return;
	}
	for (page = first; page <= last; page++)
	{
		page_protected[page] = 0;
		page_generation[page] = __sync_add_and_fetch(&current_generation, 1);
	}
}

/* memory_watch_begin_frame: pages may be hashed again; nothing under page
protection */
void host_memory_watch_begin_frame(void)
{
	if (watch_hashing)
		watch_hash_begin_frame(&page_hashes);
}

/* MCC's tag window (port/linux/src/mcc_memory.c): Android's host holds it
from before the Java runtime starts (port/android/host/host_mcc_memory.c);
this guest maps it itself, as it does Custom Edition's tag cache, and never
asks */
long long host_mcc_memory_reserve(unsigned int address, unsigned int size)
{
	(void)address;
	(void)size;
	return -ENOSYS;
}

int host_mcc_memory_release(unsigned int address, unsigned int size)
{
	(void)address;
	(void)size;
	return -ENOSYS;
}
