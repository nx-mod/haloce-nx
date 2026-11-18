/*
HOST_MEMORY_FILES.C

The game's map cache, kept in the host's memory instead of on the device's
storage, on every host: the Switch's, Android's and the PS5's (which builds
Android's host files).

The game unpacks each map it plays from maps/ (zlib-packed, about half the
size) into one of its cache files, z:\cache000.map to z:\cache005.map, and
reads the map from there while it plays (source/cache/cache_files_windows.c).
On the storage that meant some 730 MB of slot files, a map written to it
each time a slot took a new one, and every texture and sound the game
streams in read back from it. Here the guest's files of those names
(<save root>/z/cacheNNN.map) are answered from memory: the host's own,
outside the guest's address space below 4 GB, which its allocations need.

A slot is held in pieces of a megabyte, made as the game writes into them,
so it holds about its map's size (a multiplayer map 45 MB, a campaign
level more) rather than the size the game gives the file; a piece never
written reads as zeros. The game's own rules still choose, check and
replace the slots; it truncates a slot before it writes a new map into it
(platform_file_discard, port/linux/src/xbox_files.c), which gives the old
map's pieces back. The slots start empty at each start, so a map is
unpacked again the first time it is played.

The cache files a build that kept them on the storage left there are
deleted, every cacheNNN.map of the folder, the first time a run names one
(delete_stored_cache_files): nothing reads them again.

The Switch's host_syscall.c, which translates the guest's descriptors to its
own, sends the guest's calls on these files here itself. Android's and the
PS5's guests use the host's descriptors as their own: their host_syscall.c
send a call here first (host_memory_file_guest_descriptor_call,
host_memory_file_guest_path_call), as do the posix.h helpers the guest
imports that take a descriptor or a path (host_memory_posix_*,
tools/android_imports.py). An open gives a descriptor of
HOST_MEMORY_FILE_DESCRIPTOR and up, which no host file has.
*/

#include "host.h"
#include "host_memory_files.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SLOT_COUNT 20
#define PIECE_SIZE (1024UL * 1024UL)
#define OPEN_COUNT 64

struct slot
{
	int exists;
	unsigned long long size;
	unsigned long piece_count;
	char **pieces;
	struct timespec modified;
};

struct open_file
{
	int used;
	int slot;
	unsigned long long position;
};

static struct slot slots[SLOT_COUNT];
static struct open_file opens[OPEN_COUNT];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long pieces_held;

static unsigned long megabytes_held(void)
{
	return pieces_held * (PIECE_SIZE / (1024 * 1024));
}

static int cache_file_name(const char *name);
static void delete_stored_cache_files(const char *path);

int host_memory_file_path(const char *path)
{
	size_t length = path ? strlen(path) : 0;
	const char *name;
	int index;

	/* ".../z/cacheNNN.map", or "z/cacheNNN.map" relative to the folder the
	guest works in */
	if (length < 14)
		return -1;
	name = path + length - 14;
	if ((length > 14 && name[-1] != '/') || (name[0] | 0x20) != 'z' || name[1] != '/' ||
		!cache_file_name(name + 2))
		return -1;
	index = (name[7] - '0') * 100 + (name[8] - '0') * 10 + (name[9] - '0');
	if (index >= SLOT_COUNT)
		return -1;
	delete_stored_cache_files(path);
	return index;
}

#ifdef __SWITCH__
/* the card's lock, which every other access to it takes (host_syscall.c) */
void host_sd_lock(void);
void host_sd_unlock(void);
#else
static void host_sd_lock(void) {}
static void host_sd_unlock(void) {}
#endif

/* whether name is a cache file's: cacheNNN.map, in any case */
static int cache_file_name(const char *name)
{
	return strlen(name) == 12 && strncasecmp(name, "cache", 5) == 0 && isdigit((unsigned char)name[5]) &&
		isdigit((unsigned char)name[6]) && isdigit((unsigned char)name[7]) && strcasecmp(name + 8, ".map") == 0;
}

/* The cache files a build that kept them on the storage left in the folder
of `path` (a cache file's), deleted: once a run, the first time one is
named. Only cacheNNN.map: the folder (z\) also holds the saved games. */
static void delete_stored_cache_files(const char *path)
{
	static pthread_mutex_t once_lock = PTHREAD_MUTEX_INITIALIZER;
	static int done;
	char folder[1024];
	char file[1300];
	unsigned long long bytes = 0;
	int count = 0;
	size_t length;
	DIR *directory;
	struct dirent *entry;

	pthread_mutex_lock(&once_lock);
	if (done)
	{
		pthread_mutex_unlock(&once_lock);
		return;
	}
	done = 1;
	length = strlen(path) - 12;
	if (length >= sizeof(folder))
	{
		pthread_mutex_unlock(&once_lock);
		return;
	}
	memcpy(folder, path, length);
	folder[length] = 0;
	host_sd_lock();
	directory = opendir(length ? folder : ".");
	while (directory && (entry = readdir(directory)) != NULL)
	{
		struct stat information;

		if (!cache_file_name(entry->d_name))
			continue;
		snprintf(file, sizeof(file), "%s%s", folder, entry->d_name);
		if (stat(file, &information) == 0 && S_ISREG(information.st_mode) && unlink(file) == 0)
		{
			bytes += (unsigned long long)information.st_size;
			count++;
		}
	}
	if (directory)
		closedir(directory);
	host_sd_unlock();
	pthread_mutex_unlock(&once_lock);
	if (count)
		host_logf(HOST_LOG_INFO, "map cache: deleted %d cache files (%llu MB) from %s (the map cache is kept in memory)",
			count, bytes / (1024 * 1024), length ? folder : ".");
}

/* (with the lock held) the pieces from first on given back */
static void free_pieces(struct slot *slot, unsigned long first)
{
	unsigned long index;

	for (index = first; index < slot->piece_count; index++)
	{
		if (slot->pieces[index])
		{
			free(slot->pieces[index]);
			slot->pieces[index] = NULL;
			pieces_held--;
		}
	}
}

/* (with the lock held) the slot's size changed: its pieces past the end
given back, and room in its list for the pieces up to it */
static int resize(struct slot *slot, unsigned long long size)
{
	unsigned long count = (unsigned long)((size + PIECE_SIZE - 1) / PIECE_SIZE);

	if (count < slot->piece_count)
	{
		free_pieces(slot, count);
		/* (the piece the end now falls in keeps no bytes past it) */
		if (count && slot->pieces[count - 1] && size % PIECE_SIZE)
			memset(slot->pieces[count - 1] + size % PIECE_SIZE, 0, PIECE_SIZE - size % PIECE_SIZE);
	}
	else if (count > slot->piece_count)
	{
		char **pieces = realloc(slot->pieces, count * sizeof(*pieces));

		if (!pieces)
			return -ENOMEM;
		memset(pieces + slot->piece_count, 0, (count - slot->piece_count) * sizeof(*pieces));
		slot->pieces = pieces;
		slot->piece_count = count;
	}
	slot->size = size;
	return 0;
}

static struct open_file *open_of(int descriptor)
{
	int index = descriptor - HOST_MEMORY_FILE_DESCRIPTOR;

	return index >= 0 && index < OPEN_COUNT && opens[index].used ? &opens[index] : NULL;
}

int host_memory_file_is(int descriptor)
{
	return descriptor >= HOST_MEMORY_FILE_DESCRIPTOR && descriptor < HOST_MEMORY_FILE_DESCRIPTOR + OPEN_COUNT;
}

int host_memory_file_open(int index, const char *path, int flags)
{
	struct slot *slot = &slots[index];
	int open_index;
	int result = 0;

	(void)path; /* (the slot is the path's, host_memory_file_path) */
	pthread_mutex_lock(&lock);
	if (!slot->exists && !(flags & O_CREAT))
		result = -ENOENT;
	else if (slot->exists && (flags & O_CREAT) && (flags & O_EXCL))
		result = -EEXIST;
	for (open_index = 0; result == 0 && open_index < OPEN_COUNT && opens[open_index].used; open_index++)
		;
	if (result == 0 && open_index == OPEN_COUNT)
		result = -EMFILE;
	if (result == 0)
	{
		if (!slot->exists)
			clock_gettime(CLOCK_REALTIME, &slot->modified);
		slot->exists = 1;
		if (flags & O_TRUNC)
			resize(slot, 0);
		opens[open_index].used = 1;
		opens[open_index].slot = index;
		opens[open_index].position = 0;
		result = HOST_MEMORY_FILE_DESCRIPTOR + open_index;
	}
	pthread_mutex_unlock(&lock);
	return result;
}

int host_memory_file_close(int descriptor)
{
	struct open_file *file;

	pthread_mutex_lock(&lock);
	file = open_of(descriptor);
	if (file)
		file->used = 0;
	pthread_mutex_unlock(&lock);
	return file ? 0 : -EBADF;
}

/* (with the lock held) */
static long long read_at(struct slot *slot, void *buffer, size_t count, unsigned long long offset)
{
	size_t total = 0;

	while (total < count && offset + total < slot->size)
	{
		unsigned long long at = offset + total;
		unsigned long piece = (unsigned long)(at / PIECE_SIZE);
		unsigned long within = (unsigned long)(at % PIECE_SIZE);
		size_t wanted = count - total;

		if (wanted > PIECE_SIZE - within)
			wanted = PIECE_SIZE - within;
		if (wanted > slot->size - at)
			wanted = (size_t)(slot->size - at);
		if (piece < slot->piece_count && slot->pieces[piece])
			memcpy((char *)buffer + total, slot->pieces[piece] + within, wanted);
		else
			memset((char *)buffer + total, 0, wanted);
		total += wanted;
	}
	return (long long)total;
}

/* (with the lock held) */
static long long write_at(struct slot *slot, const void *buffer, size_t count, unsigned long long offset)
{
	static int told;
	size_t total = 0;

	if (offset + count > slot->size && resize(slot, offset + count) != 0)
		return -ENOMEM;
	while (total < count)
	{
		unsigned long long at = offset + total;
		unsigned long piece = (unsigned long)(at / PIECE_SIZE);
		unsigned long within = (unsigned long)(at % PIECE_SIZE);
		size_t wanted = count - total;

		if (wanted > PIECE_SIZE - within)
			wanted = PIECE_SIZE - within;
		if (!slot->pieces[piece])
		{
			slot->pieces[piece] = calloc(1, PIECE_SIZE);
			if (!slot->pieces[piece])
			{
				if (!told)
				{
					told = 1;
					/* (logged without the lock the logger may want: the
					card's lock is not held here) */
					host_logf(HOST_LOG_ERROR, "map cache: no memory for more of a map (%lu MB held)",
						megabytes_held());
				}
				return total ? (long long)total : -ENOSPC;
			}
			pieces_held++;
		}
		memcpy(slot->pieces[piece] + within, (const char *)buffer + total, wanted);
		total += wanted;
	}
	return (long long)total;
}

long long host_memory_file_pread(int descriptor, void *buffer, size_t count, long long offset)
{
	struct open_file *file;
	long long result = -EBADF;

	if (offset < 0)
		return -EINVAL;
	pthread_mutex_lock(&lock);
	file = open_of(descriptor);
	if (file)
		result = read_at(&slots[file->slot], buffer, count, (unsigned long long)offset);
	pthread_mutex_unlock(&lock);
	return result;
}

long long host_memory_file_pwrite(int descriptor, const void *buffer, size_t count, long long offset)
{
	struct open_file *file;
	long long result = -EBADF;

	if (offset < 0)
		return -EINVAL;
	pthread_mutex_lock(&lock);
	file = open_of(descriptor);
	if (file)
		result = write_at(&slots[file->slot], buffer, count, (unsigned long long)offset);
	pthread_mutex_unlock(&lock);
	return result;
}

long long host_memory_file_read(int descriptor, void *buffer, size_t count)
{
	struct open_file *file;
	long long result = -EBADF;

	pthread_mutex_lock(&lock);
	file = open_of(descriptor);
	if (file)
	{
		result = read_at(&slots[file->slot], buffer, count, file->position);
		if (result > 0)
			file->position += (unsigned long long)result;
	}
	pthread_mutex_unlock(&lock);
	return result;
}

long long host_memory_file_write(int descriptor, const void *buffer, size_t count)
{
	struct open_file *file;
	long long result = -EBADF;

	pthread_mutex_lock(&lock);
	file = open_of(descriptor);
	if (file)
	{
		result = write_at(&slots[file->slot], buffer, count, file->position);
		if (result > 0)
			file->position += (unsigned long long)result;
	}
	pthread_mutex_unlock(&lock);
	return result;
}

long long host_memory_file_seek(int descriptor, long long offset, int whence)
{
	struct open_file *file;
	long long result = -EBADF;

	pthread_mutex_lock(&lock);
	file = open_of(descriptor);
	if (file)
	{
		long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long long)file->position :
			whence == SEEK_END ? (long long)slots[file->slot].size : -1;

		if (base < 0 || base + offset < 0)
			result = -EINVAL;
		else
		{
			file->position = (unsigned long long)(base + offset);
			result = (long long)file->position;
		}
	}
	pthread_mutex_unlock(&lock);
	return result;
}

int host_memory_file_truncate(int descriptor, long long size)
{
	struct open_file *file;
	int result = -EBADF;
	int index = -1;
	unsigned long given_back = 0;
	unsigned long held = 0;

	if (size < 0)
		return -EINVAL;
	pthread_mutex_lock(&lock);
	file = open_of(descriptor);
	if (file)
	{
		unsigned long before = pieces_held;

		index = file->slot;
		result = resize(&slots[index], (unsigned long long)size);
		given_back = before - pieces_held;
		held = megabytes_held();
	}
	pthread_mutex_unlock(&lock);
	if (given_back)
		host_logf(HOST_LOG_INFO, "map cache: slot %d gave back %lu MB for its next map; %lu MB held in all",
			index, given_back * (PIECE_SIZE / (1024 * 1024)), held);
	return result;
}

/* (with the lock held) a slot's description as stat gives a file's */
static void describe(const struct slot *slot, struct stat *out)
{
	memset(out, 0, sizeof(*out));
	out->st_mode = S_IFREG | 0644;
	out->st_nlink = 1;
	out->st_size = (off_t)slot->size;
	out->st_blksize = 512;
	out->st_blocks = (blkcnt_t)((slot->size + 511) / 512);
	out->st_atim = slot->modified;
	out->st_mtim = slot->modified;
	out->st_ctim = slot->modified;
}

int host_memory_file_fstat(int descriptor, struct stat *out)
{
	struct open_file *file;
	int result = -EBADF;

	pthread_mutex_lock(&lock);
	file = open_of(descriptor);
	if (file)
	{
		describe(&slots[file->slot], out);
		result = 0;
	}
	pthread_mutex_unlock(&lock);
	return result;
}

int host_memory_file_stat(int index, const char *path, struct stat *out)
{
	int result = -ENOENT;

	(void)path; /* (the slot is the path's, host_memory_file_path) */
	pthread_mutex_lock(&lock);
	if (slots[index].exists)
	{
		describe(&slots[index], out);
		result = 0;
	}
	pthread_mutex_unlock(&lock);
	return result;
}

int host_memory_file_unlink(int index, const char *path)
{
	struct slot *slot = &slots[index];
	int result = -ENOENT;

	(void)path; /* (the slot is the path's, host_memory_file_path) */
	pthread_mutex_lock(&lock);
	if (slot->exists)
	{
		free_pieces(slot, 0);
		free(slot->pieces);
		memset(slot, 0, sizeof(*slot));
		result = 0;
	}
	pthread_mutex_unlock(&lock);
	return result;
}

/* times as utimensat takes them: [0] access, [1] modification; NULL now */
int host_memory_file_set_times(int index, const struct timespec *times)
{
	struct slot *slot = &slots[index];
	int result = -ENOENT;

	pthread_mutex_lock(&lock);
	if (slot->exists)
	{
		if (!times || times[1].tv_nsec == UTIME_NOW)
			clock_gettime(CLOCK_REALTIME, &slot->modified);
		else if (times[1].tv_nsec != UTIME_OMIT)
			slot->modified = times[1];
		result = 0;
	}
	pthread_mutex_unlock(&lock);
	return result;
}

#ifndef __SWITCH__
/* ---------- Android's and the PS5's guests (host_memory_files.h)

Their guests' descriptors are the host's, so a memory file's descriptor
reaches host_syscall.c, and the posix.h helpers the guest imports, as it is:
each sends it here first. Answers are the guest's: its kernel stat layout,
its timespec, and Linux's errno numbers (halo_guest_syscall.h). The errno
values these functions return (ENOENT, EBADF, ENOMEM, EEXIST, EINVAL,
EMFILE, ENOSPC) have the same numbers on Linux and on the PS5's FreeBSD. */

#include "halo_guest_syscall.h"
#include "posix.h"

#include <errno.h>

#define MEMORY_GUEST_POINTER(type, value) ((type)(uintptr_t)(uint32_t)(value))
#define GUEST_EMFILE_NUMBER 24
#define GUEST_ENOTDIR_NUMBER 20
/* utimensat's nanoseconds that mean "now" and "leave it" (Linux's) */
#define GUEST_UTIME_NOW_NANOSECONDS ((1 << 30) - 1)
#define GUEST_UTIME_OMIT_NANOSECONDS ((1 << 30) - 2)

_Static_assert(ENOENT == GUEST_ENOENT && EBADF == GUEST_EBADF && ENOMEM == GUEST_ENOMEM &&
	EEXIST == GUEST_EEXIST && EINVAL == GUEST_EINVAL && ENOSPC == GUEST_ENOSPC && EMFILE == GUEST_EMFILE_NUMBER,
	"the memory files' errno numbers are not the guest's");

static void guest_kstat(const struct stat *in, uint64_t address)
{
	struct halo_guest_kstat *out = MEMORY_GUEST_POINTER(struct halo_guest_kstat *, address);

	memset(out, 0, sizeof(*out));
	out->st_mode = (uint32_t)in->st_mode;
	out->st_nlink = 1;
	out->st_size = (int64_t)in->st_size;
	out->st_blksize = (int32_t)in->st_blksize;
	out->st_blocks = (int64_t)in->st_blocks;
	out->atime_seconds = (int64_t)in->st_atim.tv_sec;
	out->atime_nanoseconds = (int64_t)in->st_atim.tv_nsec;
	out->mtime_seconds = (int64_t)in->st_mtim.tv_sec;
	out->mtime_nanoseconds = (int64_t)in->st_mtim.tv_nsec;
	out->ctime_seconds = (int64_t)in->st_ctim.tv_sec;
	out->ctime_nanoseconds = (int64_t)in->st_ctim.tv_nsec;
}

/* readv, writev and their positional forms, an iovec at a time */
static long long guest_vector(int descriptor, uint64_t vector, int count, long long offset, int positional,
	int writing)
{
	const struct halo_guest_iovec *entries = MEMORY_GUEST_POINTER(const struct halo_guest_iovec *, vector);
	long long total = 0;
	int index;

	if (count < 0 || count > GUEST_IOV_MAX)
		return -GUEST_EINVAL;
	if (count && !vector)
		return -GUEST_EFAULT;
	for (index = 0; index < count; index++)
	{
		void *base = MEMORY_GUEST_POINTER(void *, entries[index].base);
		size_t length = entries[index].length;
		long long done;

		if (!length)
			continue;
		if (writing)
			done = positional ? host_memory_file_pwrite(descriptor, base, length, offset + total) :
				host_memory_file_write(descriptor, base, length);
		else
			done = positional ? host_memory_file_pread(descriptor, base, length, offset + total) :
				host_memory_file_read(descriptor, base, length);
		if (done < 0)
			return total ? total : done;
		total += done;
		if ((size_t)done < length)
			break;
	}
	return total;
}

/* a 64-bit offset from the two longs musl passes for it, low half first */
static long long memory_offset_of_halves(long long low, long long high)
{
	return (long long)((uint64_t)(uint32_t)low | ((uint64_t)(uint32_t)high << 32));
}

long long host_memory_file_guest_descriptor_call(long long number, long long a, long long b, long long c,
	long long d, long long e, int *handled)
{
	int descriptor = (int)a;

	*handled = 0;
	/* (only the calls whose first argument is a descriptor: another's, an
	address or a count, can fall in the memory files' range) */
	switch (number)
	{
	case GUEST_SYS_close: case GUEST_SYS_read: case GUEST_SYS_write: case GUEST_SYS_pread64:
	case GUEST_SYS_pwrite64: case GUEST_SYS_readv: case GUEST_SYS_writev: case GUEST_SYS_preadv:
	case GUEST_SYS_pwritev: case GUEST_SYS__llseek: case GUEST_SYS_ftruncate: case GUEST_SYS_fstat:
	case GUEST_SYS_newfstatat: case GUEST_SYS_fsync: case GUEST_SYS_fdatasync: case GUEST_SYS_flock:
	case GUEST_SYS_fcntl: case GUEST_SYS_getdents64: case GUEST_SYS_dup: case GUEST_SYS_dup3:
	case GUEST_SYS_fchmod: case GUEST_SYS_utimensat: case GUEST_SYS_mmap:
		break;
	default:
		return 0;
	}
	/* (mmap's descriptor is its fifth argument; newfstatat and utimensat
	name a descriptor alone with no path, or an empty one) */
	if (number == GUEST_SYS_mmap)
		descriptor = (int)e;
	if (!host_memory_file_is(descriptor) ||
		((number == GUEST_SYS_newfstatat || number == GUEST_SYS_utimensat) && b &&
			MEMORY_GUEST_POINTER(const char *, b)[0]))
		return 0;
	*handled = 1;
	switch (number)
	{
	case GUEST_SYS_close:
		return host_memory_file_close(descriptor);
	case GUEST_SYS_read:
		return host_memory_file_read(descriptor, MEMORY_GUEST_POINTER(void *, b), (size_t)(uint32_t)c);
	case GUEST_SYS_write:
		return host_memory_file_write(descriptor, MEMORY_GUEST_POINTER(const void *, b), (size_t)(uint32_t)c);
	case GUEST_SYS_pread64:
		return host_memory_file_pread(descriptor, MEMORY_GUEST_POINTER(void *, b), (size_t)(uint32_t)c, d);
	case GUEST_SYS_pwrite64:
		return host_memory_file_pwrite(descriptor, MEMORY_GUEST_POINTER(const void *, b), (size_t)(uint32_t)c, d);
	case GUEST_SYS_readv:
		return guest_vector(descriptor, (uint64_t)b, (int)c, 0, 0, 0);
	case GUEST_SYS_writev:
		return guest_vector(descriptor, (uint64_t)b, (int)c, 0, 0, 1);
	case GUEST_SYS_preadv:
		return guest_vector(descriptor, (uint64_t)b, (int)c, memory_offset_of_halves(d, e), 1, 0);
	case GUEST_SYS_pwritev:
		return guest_vector(descriptor, (uint64_t)b, (int)c, memory_offset_of_halves(d, e), 1, 1);
	case GUEST_SYS__llseek:
	{
		/* _llseek(fd, offset_high, offset_low, result, whence) */
		long long position;

		if (!d)
			return -GUEST_EFAULT;
		position = host_memory_file_seek(descriptor,
			(long long)(((uint64_t)(uint32_t)b << 32) | (uint32_t)c), (int)e);
		if (position < 0)
			return position;
		memcpy(MEMORY_GUEST_POINTER(void *, d), &(int64_t){ (int64_t)position }, sizeof(int64_t));
		return 0;
	}
	case GUEST_SYS_ftruncate:
		return host_memory_file_truncate(descriptor, b);
	case GUEST_SYS_fstat:
	{
		struct stat information;
		int result;

		if (!b)
			return -GUEST_EFAULT;
		result = host_memory_file_fstat(descriptor, &information);
		if (result == 0)
			guest_kstat(&information, (uint64_t)b);
		return result;
	}
	case GUEST_SYS_newfstatat:
	{
		/* (fstat as musl may make it: the descriptor and an empty path) */
		struct stat information;
		int result;

		if (!c)
			return -GUEST_EFAULT;
		result = host_memory_file_fstat(descriptor, &information);
		if (result == 0)
			guest_kstat(&information, (uint64_t)c);
		return result;
	}
	case GUEST_SYS_fsync:
	case GUEST_SYS_fdatasync:
	case GUEST_SYS_flock:
		return 0;
	case GUEST_SYS_fcntl:
		/* (musl's open sets FD_CLOEXEC; a memory file has nothing to
		inherit) */
		switch ((int)b)
		{
		case GUEST_F_GETFD:
			return 1; /* FD_CLOEXEC */
		case GUEST_F_SETFD:
			return 0;
		case GUEST_F_GETFL:
			return GUEST_O_RDWR;
		case GUEST_F_SETFL:
			return 0;
		}
		return -GUEST_EINVAL;
	case GUEST_SYS_getdents64:
		return -GUEST_ENOTDIR_NUMBER;
	case GUEST_SYS_mmap:
		/* (a memory file has no pages to map: nothing maps a cache file) */
		host_logf(HOST_LOG_WARN, "map cache: the guest mapped a memory file");
		return -GUEST_EINVAL;
	}
	/* (dup and the rest: nothing the game does with its cache files) */
	host_logf(HOST_LOG_WARN, "map cache: guest system call %lld on a memory file is not supported", number);
	return -GUEST_EINVAL;
}

/* the host's open flags for the guest's that a memory file reads */
static int guest_open_flags(uint32_t guest)
{
	return (guest & GUEST_O_CREAT ? O_CREAT : 0) | (guest & GUEST_O_EXCL ? O_EXCL : 0) |
		(guest & GUEST_O_TRUNC ? O_TRUNC : 0);
}

static void guest_time(uint64_t address, struct timespec *out)
{
	const struct halo_guest_timespec *in = MEMORY_GUEST_POINTER(const struct halo_guest_timespec *, address);

	out->tv_sec = in->seconds;
	out->tv_nsec = in->nanoseconds == GUEST_UTIME_NOW_NANOSECONDS ? UTIME_NOW :
		in->nanoseconds == GUEST_UTIME_OMIT_NANOSECONDS ? UTIME_OMIT : in->nanoseconds;
}

long long host_memory_file_guest_path_call(long long number, const char *path, long long a, long long b,
	long long c, long long d, int *handled)
{
	int slot = host_memory_file_path(path);
	struct stat information;
	int result;

	(void)a;
	(void)b;
	*handled = 0;
	if (slot < 0)
		return 0;
	*handled = 1;
	switch (number)
	{
	case GUEST_SYS_openat:
		/* openat(directory, path, flags, mode) */
		return host_memory_file_open(slot, path, guest_open_flags((uint32_t)c));
	case GUEST_SYS_newfstatat:
		/* newfstatat(directory, path, result, flags) */
		if (!c)
			return -GUEST_EFAULT;
		result = host_memory_file_stat(slot, path, &information);
		if (result == 0)
			guest_kstat(&information, (uint64_t)c);
		return result;
	case GUEST_SYS_faccessat:
		return host_memory_file_stat(slot, path, &information);
	case GUEST_SYS_unlinkat:
		return host_memory_file_unlink(slot, path);
	case GUEST_SYS_utimensat:
	{
		/* utimensat(directory, path, times, flags) */
		struct timespec times[2];

		if (!c)
			return host_memory_file_set_times(slot, NULL);
		guest_time((uint64_t)c, &times[0]);
		guest_time((uint64_t)c + sizeof(struct halo_guest_timespec), &times[1]);
		return host_memory_file_set_times(slot, times);
	}
	}
	(void)d;
	host_logf(HOST_LOG_WARN, "map cache: guest system call %lld on a memory file's path is not supported", number);
	return -GUEST_EINVAL;
}

/* ---------- the posix.h helpers the guest imports (tools/android_imports.py)

The cache code measures, seeks, sizes, checks and stamps its files through
posix_*, not through system calls: these reach the memory files too, and
are posix_files.c's for every other file. */

static void memory_file_information(const struct stat *in, struct posix_file_information *out)
{
	memset(out, 0, sizeof(*out));
	out->size_low = (posix_ulong)((unsigned long long)in->st_size & 0xffffffffULL);
	out->size_high = (posix_ulong)((unsigned long long)in->st_size >> 32);
	out->modification_seconds = (posix_ulong)in->st_mtim.tv_sec;
	out->modification_nanoseconds = (posix_ulong)in->st_mtim.tv_nsec;
	out->access_seconds = (posix_ulong)in->st_atim.tv_sec;
	out->access_nanoseconds = (posix_ulong)in->st_atim.tv_nsec;
	out->creation_seconds = (posix_ulong)in->st_ctim.tv_sec;
	out->creation_nanoseconds = (posix_ulong)in->st_ctim.tv_nsec;
}

/* a memory file's answer, a negated errno or not, as posix_* give theirs */
static int memory_file_result(long long result)
{
	if (result < 0)
	{
		errno = (int)-result;
		return -1;
	}
	return 0;
}

int host_memory_posix_stat(const char *path, struct posix_file_information *information)
{
	int slot = host_memory_file_path(path);
	struct stat in;
	int result;

	if (slot < 0)
		return posix_stat(path, information);
	result = host_memory_file_stat(slot, path, &in);
	if (result == 0)
		memory_file_information(&in, information);
	return memory_file_result(result);
}

int host_memory_posix_fstat(int descriptor, struct posix_file_information *information)
{
	struct stat in;
	int result;

	if (!host_memory_file_is(descriptor))
		return posix_fstat(descriptor, information);
	result = host_memory_file_fstat(descriptor, &in);
	if (result == 0)
		memory_file_information(&in, information);
	return memory_file_result(result);
}

int host_memory_posix_set_file_times(const char *path, posix_ulong access_seconds,
	posix_ulong access_nanoseconds, posix_ulong modification_seconds, posix_ulong modification_nanoseconds)
{
	int slot = host_memory_file_path(path);
	struct timespec times[2];

	if (slot < 0)
		return posix_set_file_times(path, access_seconds, access_nanoseconds, modification_seconds,
			modification_nanoseconds);
	times[0].tv_sec = (time_t)access_seconds;
	times[0].tv_nsec = access_seconds ? (long)access_nanoseconds : UTIME_OMIT;
	times[1].tv_sec = (time_t)modification_seconds;
	times[1].tv_nsec = modification_seconds ? (long)modification_nanoseconds : UTIME_OMIT;
	return memory_file_result(host_memory_file_set_times(slot, times));
}

int host_memory_posix_seek(int descriptor, posix_long offset_low, posix_long offset_high, int whence,
	posix_ulong *position_low, posix_ulong *position_high)
{
	long long where;

	if (!host_memory_file_is(descriptor))
		return posix_seek(descriptor, offset_low, offset_high, whence, position_low, position_high);
	where = host_memory_file_seek(descriptor,
		(long long)(((unsigned long long)(posix_ulong)offset_high << 32) | (posix_ulong)offset_low), whence);
	if (where >= 0)
	{
		*position_low = (posix_ulong)((unsigned long long)where & 0xffffffffULL);
		*position_high = (posix_ulong)((unsigned long long)where >> 32);
	}
	return memory_file_result(where);
}

int host_memory_posix_truncate(int descriptor, posix_ulong size_low, posix_ulong size_high)
{
	if (!host_memory_file_is(descriptor))
		return posix_truncate(descriptor, size_low, size_high);
	return memory_file_result(host_memory_file_truncate(descriptor,
		(long long)(((unsigned long long)size_high << 32) | size_low)));
}
#endif
