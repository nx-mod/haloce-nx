/*
HOST_TEXTURE_CACHE.C

Textures' pixels kept in the host's memory, outside the Xbox window, so that
a texture the game's texture cache gave up is read back from here and not
from the SD card. The game's cache lives in the window and is half of it (128
or 256 MB); a mod of uncompressed textures needs more than that for one map,
and each texture the cache took back was read from the card in the middle of
a frame: most of a tester's hitches. The host has far more memory free (about
1.8 GB of its heap on a Switch Lite in a 64-player match with that mod), which
the 32-bit guest cannot address, so the guest asks for a texture's bytes by a
key and the host copies them (its buffer a guest address, as 32 bits) (host_texture_cache_get, _put).

The bytes kept are a file's, as read, so that what the game does with them
after the read is the same either way: a mod's bitmaps
(port/linux/game/mod_bitmaps.c: before the channels are told and the
hardware format rebuilt), and a Custom Edition map's
(port/linux/game/custom_edition_cache.c: before they are converted).

It keeps at most 512 MB, emptied with each map's mods (more was planned,
sized from the heap's free memory, but a map with a mod of uncompressed
textures needed under 50 MB of it); and while the heap's free memory is
under 256 MB it keeps nothing more, and lets its oldest go. */

#include "host.h"

#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_BUCKETS 4096
#define CACHE_BUDGET (512ull * 1024 * 1024)
#define CACHE_FLOOR (256ull * 1024 * 1024)
/* the heap's free memory looked at again once this much more is kept */
#define CACHE_FLOOR_LOOK_BYTES (32ull * 1024 * 1024)

struct cache_entry
{
	struct cache_entry *bucket_next;
	struct cache_entry *older, *newer;
	uint64_t key;
	uint32_t size;
	unsigned char *bytes;
};

static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
static struct cache_entry *buckets[CACHE_BUCKETS];
static struct cache_entry *oldest, *newest;
static uint64_t cache_bytes, cache_budget, bytes_since_look;
static unsigned long cache_entries, hits, misses, puts, refused;

extern char *fake_heap_start;
extern char *fake_heap_end;

/* the heap's free memory: what is free in what it handed out, and what it never handed out */
static uint64_t heap_free(void)
{
	struct mallinfo heap = mallinfo();
	size_t size = (size_t)(fake_heap_end - fake_heap_start);

	return (uint64_t)heap.fordblks + (size > heap.arena ? size - heap.arena : 0);
}

static unsigned bucket_of(uint64_t key)
{
	return (unsigned)((key * 0x9E3779B97F4A7C15ull) >> 52) % CACHE_BUCKETS;
}

/* (under the lock) */
static void entry_unlink(struct cache_entry *entry)
{
	struct cache_entry **link = &buckets[bucket_of(entry->key)];

	while (*link && *link != entry)
		link = &(*link)->bucket_next;
	if (*link)
		*link = entry->bucket_next;
	if (entry->older)
		entry->older->newer = entry->newer;
	else
		oldest = entry->newer;
	if (entry->newer)
		entry->newer->older = entry->older;
	else
		newest = entry->older;
	cache_bytes -= entry->size;
	cache_entries--;
	free(entry->bytes);
	free(entry);
}

/* (under the lock) the most lately used */
static void entry_touch(struct cache_entry *entry)
{
	if (entry == newest)
		return;
	if (entry->older)
		entry->older->newer = entry->newer;
	else
		oldest = entry->newer;
	entry->newer->older = entry->older;
	entry->older = newest;
	entry->newer = NULL;
	newest->newer = entry;
	newest = entry;
}

static struct cache_entry *entry_find(uint64_t key)
{
	struct cache_entry *entry;

	for (entry = buckets[bucket_of(key)]; entry; entry = entry->bucket_next)
	{
		if (entry->key == key)
			return entry;
	}
	return NULL;
}

/* (under the lock) everything let go, and the budget for what comes now */
static void cache_empty(void)
{
	uint64_t free_bytes, budget;

	while (oldest)
		entry_unlink(oldest);
	free_bytes = heap_free();
	budget = CACHE_BUDGET;
	if (hits || misses || puts)
		host_logf(HOST_LOG_INFO, "texture cache (host): the map before had %lu textures read from here, %lu from the "
			"card", hits, misses);
	/* (the counts, the map's from now: the report's are since this) */
	hits = misses = puts = refused = 0;
	cache_budget = budget;
	bytes_since_look = 0;
	host_logf(HOST_LOG_INFO, "texture cache (host): emptied; up to %.0f MB from now (%.0f MB of the heap free)",
		(double)budget / 1048576.0, (double)free_bytes / 1048576.0);
}

void host_texture_cache_clear(void)
{
	pthread_mutex_lock(&cache_lock);
	cache_empty();
	pthread_mutex_unlock(&cache_lock);
}

int host_texture_cache_get(uint64_t key, uint32_t guest_buffer, uint32_t size)
{
	void *buffer = (void *)(uintptr_t)guest_buffer;
	struct cache_entry *entry;
	int found = 0;

	pthread_mutex_lock(&cache_lock);
	entry = entry_find(key);
	if (entry && entry->size == size)
	{
		memcpy(buffer, entry->bytes, size);
		entry_touch(entry);
		hits++;
		found = 1;
	}
	else
	{
		misses++;
	}
	pthread_mutex_unlock(&cache_lock);
	return found;
}

void host_texture_cache_put(uint64_t key, uint32_t guest_buffer, uint32_t size)
{
	const void *buffer = (const void *)(uintptr_t)guest_buffer;
	struct cache_entry *entry;

	if (!size)
		return;
	pthread_mutex_lock(&cache_lock);
	if (!cache_budget)
		cache_empty();
	entry = entry_find(key);
	if (entry)
		entry_unlink(entry);
	/* (the oldest let go to keep within the budget; and, now and then, to keep the heap's floor) */
	while (oldest && cache_bytes + size > cache_budget)
		entry_unlink(oldest);
	bytes_since_look += size;
	if (bytes_since_look >= CACHE_FLOOR_LOOK_BYTES)
	{
		bytes_since_look = 0;
		while (oldest && heap_free() < CACHE_FLOOR + size)
			entry_unlink(oldest);
		if (heap_free() < CACHE_FLOOR + size)
		{
			refused++;
			pthread_mutex_unlock(&cache_lock);
			return;
		}
	}
	if (size > cache_budget)
	{
		pthread_mutex_unlock(&cache_lock);
		return;
	}
	entry = calloc(1, sizeof(*entry));
	if (entry)
		entry->bytes = malloc(size);
	if (!entry || !entry->bytes)
	{
		free(entry);
		refused++;
		pthread_mutex_unlock(&cache_lock);
		return;
	}
	memcpy(entry->bytes, buffer, size);
	entry->key = key;
	entry->size = size;
	entry->bucket_next = buckets[bucket_of(key)];
	buckets[bucket_of(key)] = entry;
	entry->older = newest;
	if (newest)
		newest->newer = entry;
	else
		oldest = entry;
	newest = entry;
	cache_bytes += size;
	cache_entries++;
	puts++;
	pthread_mutex_unlock(&cache_lock);
}

/* (host_memory_report's) its size and how it has done since the last report */
void host_texture_cache_report(void)
{
	pthread_mutex_lock(&cache_lock);
	if (hits || misses || puts || cache_entries)
		host_logf(HOST_LOG_INFO, "texture cache (host): %.0f MB of %.0f MB in %lu textures; since the map loaded, %lu "
			"read from here, %lu from the card, %lu kept, %lu not kept for want of memory",
			(double)cache_bytes / 1048576.0, (double)cache_budget / 1048576.0, cache_entries, hits, misses, puts,
			refused);
	pthread_mutex_unlock(&cache_lock);
}
