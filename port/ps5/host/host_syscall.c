/*
HOST_SYSCALL.C

System calls on behalf of the guest's musl runtime, on the PS5 (its
syscall_arch.h sends every call here, through the host_syscall import).

The guest's interface is Linux's (port/android/include/halo_guest_syscall.h:
its numbers, its layouts, its flags), and the console's kernel is FreeBSD's,
reached through the C library the title links: libkernel's calls, and the
platform layer's where a title cannot use libkernel's (the RADV link
recipe binds openat, fstatat, mkdirat, unlinkat, renameat, fchmodat,
access, utimensat, statvfs, clock_nanosleep and arc4random_buf to it).
This file is Android's host_syscall.c in its structure (one switch on the
guest's number, each call translated and made with the C library, the
result the guest's 32-bit long with Linux's negative errno), with the
Switch's inventory of what a non-Linux system lacks. The translations that
need no system are host_syscall_tables.c's, tested on the PC; their FreeBSD
numbers are freebsd_abi.h's, checked here against the SDK's headers.

Per family:

- files: the flags of open, the *at calls and fcntl translated; struct
  stat and FreeBSD's directory records (getdents) repacked into the guest's
  kernel stat and linux_dirent64; relative paths resolved against the
  guest's working folder, which is the host's own (getcwd crashes a title,
  phase 0 B), /data/halo; /proc/self/exe is /data/halo/halo, the folder the
  desktop game looks for its data in;
- the standard output and error go to the log, a line at a time, prefixed
  "game: ";
- memory is host_memory.c's, futexes host_futex.c's;
- the game's map cache files (z/cacheNNN.map) are kept in the host's
  memory (host_memory_files.c, Android's): a call on one of their
  descriptors or paths goes there first;
- clocks translated (Linux's MONOTONIC is FreeBSD's 4, BOOTTIME its
  UPTIME), timespec and timeval in the guest's 32-bit layouts;
- identity and limits answered here: a Linux 5.15 on x86_64, user 1000,
  the direct memory as the RAM;
- signals are the host's: the guest's handlers are accepted and never run;
- ppoll over poll (musl's poll); the network's own calls (sendmsg,
  recvmsg, epoll_pwait, pselect6) are not the game's way to its sockets,
  which are the host's (posix_net.c), and answer -ENOSYS;
- what the guest cannot have here (processes, timers, signals to send)
  answers -ENOSYS. Every -ENOSYS is counted by
  number, and the counts are logged when the game ends
  (ps5_syscall_report), so the write-up says what the game asked for and
  did not get.
*/

#include "host.h"
#include "ps5_host.h"
#include "freebsd_abi.h"
#include "host_syscall_tables.h"
#include "host_memory_files.h"

#include <ps5platform/kernel.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

/* ---------- freebsd_abi.h against the SDK's headers */

#define FREEBSD(name) _Static_assert(FREEBSD_##name == name, "freebsd_abi.h's " #name " is not the SDK's")
FREEBSD(O_ACCMODE);
FREEBSD(O_NONBLOCK);
FREEBSD(O_APPEND);
FREEBSD(O_ASYNC);
FREEBSD(O_SYNC);
FREEBSD(O_NOFOLLOW);
FREEBSD(O_CREAT);
FREEBSD(O_TRUNC);
FREEBSD(O_EXCL);
FREEBSD(O_NOCTTY);
FREEBSD(O_DIRECT);
FREEBSD(O_DIRECTORY);
FREEBSD(O_CLOEXEC);
FREEBSD(POLLIN);
FREEBSD(POLLPRI);
FREEBSD(POLLOUT);
FREEBSD(POLLERR);
FREEBSD(POLLHUP);
FREEBSD(POLLNVAL);
FREEBSD(POLLRDNORM);
FREEBSD(POLLRDBAND);
FREEBSD(POLLWRNORM);
FREEBSD(POLLWRBAND);
FREEBSD(AT_FDCWD);
FREEBSD(AT_SYMLINK_NOFOLLOW);
FREEBSD(AT_REMOVEDIR);
FREEBSD(F_DUPFD);
FREEBSD(F_GETFD);
FREEBSD(F_SETFD);
FREEBSD(F_GETFL);
FREEBSD(F_SETFL);
FREEBSD(F_DUPFD_CLOEXEC);
FREEBSD(FD_CLOEXEC);
FREEBSD(EPERM);
FREEBSD(ENOENT);
FREEBSD(ESRCH);
FREEBSD(EINTR);
FREEBSD(EIO);
FREEBSD(ENXIO);
FREEBSD(E2BIG);
FREEBSD(ENOEXEC);
FREEBSD(EBADF);
FREEBSD(ECHILD);
FREEBSD(EDEADLK);
FREEBSD(ENOMEM);
FREEBSD(EACCES);
FREEBSD(EFAULT);
FREEBSD(ENOTBLK);
FREEBSD(EBUSY);
FREEBSD(EEXIST);
FREEBSD(EXDEV);
FREEBSD(ENODEV);
FREEBSD(ENOTDIR);
FREEBSD(EISDIR);
FREEBSD(EINVAL);
FREEBSD(ENFILE);
FREEBSD(EMFILE);
FREEBSD(ENOTTY);
FREEBSD(ETXTBSY);
FREEBSD(EFBIG);
FREEBSD(ENOSPC);
FREEBSD(ESPIPE);
FREEBSD(EROFS);
FREEBSD(EMLINK);
FREEBSD(EPIPE);
FREEBSD(EDOM);
FREEBSD(ERANGE);
FREEBSD(EAGAIN);
FREEBSD(EINPROGRESS);
FREEBSD(EALREADY);
FREEBSD(ENOTSOCK);
FREEBSD(EDESTADDRREQ);
FREEBSD(EMSGSIZE);
FREEBSD(EPROTOTYPE);
FREEBSD(ENOPROTOOPT);
FREEBSD(EPROTONOSUPPORT);
FREEBSD(ESOCKTNOSUPPORT);
FREEBSD(EOPNOTSUPP);
FREEBSD(EPFNOSUPPORT);
FREEBSD(EAFNOSUPPORT);
FREEBSD(EADDRINUSE);
FREEBSD(EADDRNOTAVAIL);
FREEBSD(ENETDOWN);
FREEBSD(ENETUNREACH);
FREEBSD(ENETRESET);
FREEBSD(ECONNABORTED);
FREEBSD(ECONNRESET);
FREEBSD(ENOBUFS);
FREEBSD(EISCONN);
FREEBSD(ENOTCONN);
FREEBSD(ESHUTDOWN);
FREEBSD(ETOOMANYREFS);
FREEBSD(ETIMEDOUT);
FREEBSD(ECONNREFUSED);
FREEBSD(ELOOP);
FREEBSD(ENAMETOOLONG);
FREEBSD(EHOSTDOWN);
FREEBSD(EHOSTUNREACH);
FREEBSD(ENOTEMPTY);
FREEBSD(EUSERS);
FREEBSD(EDQUOT);
FREEBSD(ESTALE);
FREEBSD(EREMOTE);
FREEBSD(ENOLCK);
FREEBSD(ENOSYS);
FREEBSD(EIDRM);
FREEBSD(ENOMSG);
FREEBSD(EOVERFLOW);
FREEBSD(ECANCELED);
FREEBSD(EILSEQ);
FREEBSD(ENOATTR);
FREEBSD(EBADMSG);
FREEBSD(EMULTIHOP);
FREEBSD(ENOLINK);
FREEBSD(EPROTO);
FREEBSD(ENOTRECOVERABLE);
FREEBSD(EOWNERDEAD);
FREEBSD(CLOCK_REALTIME);
FREEBSD(CLOCK_MONOTONIC);
FREEBSD(CLOCK_UPTIME);
FREEBSD(CLOCK_REALTIME_FAST);
FREEBSD(CLOCK_MONOTONIC_FAST);
FREEBSD(CLOCK_THREAD_CPUTIME_ID);
FREEBSD(CLOCK_PROCESS_CPUTIME_ID);
FREEBSD(TIMER_ABSTIME);
FREEBSD(UTIME_NOW);
FREEBSD(UTIME_OMIT);
FREEBSD(DT_UNKNOWN);
FREEBSD(DT_FIFO);
FREEBSD(DT_CHR);
FREEBSD(DT_DIR);
FREEBSD(DT_BLK);
FREEBSD(DT_REG);
FREEBSD(DT_LNK);
FREEBSD(DT_SOCK);
FREEBSD(MAP_PRIVATE);
FREEBSD(MAP_FIXED);
FREEBSD(MAP_ANON);
#undef FREEBSD
_Static_assert(offsetof(struct dirent, d_fileno) == FREEBSD_DIRENT_FILENO && sizeof(((struct dirent *)0)->d_fileno) == 4,
	"struct dirent's d_fileno");
_Static_assert(offsetof(struct dirent, d_reclen) == FREEBSD_DIRENT_RECLEN, "struct dirent's d_reclen");
_Static_assert(offsetof(struct dirent, d_type) == FREEBSD_DIRENT_TYPE, "struct dirent's d_type");
_Static_assert(offsetof(struct dirent, d_namlen) == FREEBSD_DIRENT_NAMLEN && sizeof(((struct dirent *)0)->d_namlen) == 1,
	"struct dirent's d_namlen");
_Static_assert(offsetof(struct dirent, d_name) == FREEBSD_DIRENT_NAME, "struct dirent's d_name");

/* the values the guest's interface shares with FreeBSD, passed as they are */
#define SAME(name) _Static_assert(name == GUEST_##name, "FreeBSD's " #name " is not the guest's")
SAME(SEEK_SET);
SAME(SEEK_CUR);
SAME(SEEK_END);
SAME(PROT_READ);
SAME(PROT_WRITE);
SAME(PROT_EXEC);
SAME(LOCK_SH);
SAME(LOCK_EX);
SAME(LOCK_NB);
SAME(LOCK_UN);
SAME(O_RDONLY);
SAME(O_WRONLY);
SAME(O_RDWR);
#undef SAME

#define GUEST(type, value) ((type)(uintptr_t)(uint32_t)(value))

/* (Linux's ENOTTY, which the guest's header does not name) */
#define GUEST_ENOTTY 25

static long result_of(long value)
{
	return value == -1 ? -ps5_errno_out(errno) : value;
}

/* ---------- -ENOSYS, counted */

#define CALL_NUMBERS 512
/* the failed calls the log names (host_syscall) */
#define FAILURES_LOGGED 300

static uint32_t enosys_counts[CALL_NUMBERS];

#define NAME(call) [GUEST_SYS_##call] = #call
static const char *const call_names[CALL_NUMBERS] =
{
	NAME(getcwd), NAME(epoll_pwait), NAME(dup), NAME(dup3), NAME(fcntl), NAME(ioctl), NAME(flock), NAME(mkdirat),
	NAME(unlinkat), NAME(renameat), NAME(statfs), NAME(fstatfs), NAME(ftruncate), NAME(faccessat), NAME(chdir),
	NAME(fchmod), NAME(fchmodat), NAME(openat), NAME(close), NAME(pipe2), NAME(getdents64), NAME(_llseek),
	NAME(read), NAME(write), NAME(readv), NAME(writev), NAME(pread64), NAME(pwrite64), NAME(preadv), NAME(pwritev),
	NAME(pselect6), NAME(ppoll), NAME(readlinkat), NAME(newfstatat), NAME(fstat), NAME(fsync), NAME(fdatasync),
	NAME(utimensat), NAME(exit), NAME(exit_group), NAME(waitid), NAME(set_tid_address), NAME(futex),
	NAME(nanosleep), NAME(getitimer), NAME(setitimer), NAME(timer_create), NAME(timer_gettime),
	NAME(timer_settime), NAME(clock_gettime), NAME(clock_getres), NAME(clock_nanosleep), NAME(sched_yield),
	NAME(kill), NAME(tkill), NAME(tgkill), NAME(sigaltstack), NAME(rt_sigaction), NAME(rt_sigprocmask),
	NAME(rt_sigtimedwait), NAME(times), NAME(uname), NAME(getrlimit), NAME(getrusage), NAME(umask),
	NAME(gettimeofday), NAME(getpid), NAME(getppid), NAME(getuid), NAME(geteuid), NAME(getgid), NAME(getegid),
	NAME(gettid), NAME(sysinfo), NAME(sendmsg), NAME(recvmsg), NAME(brk), NAME(munmap), NAME(mremap), NAME(clone),
	NAME(execve), NAME(mmap), NAME(mprotect), NAME(madvise), NAME(wait4), NAME(prlimit64), NAME(renameat2),
	NAME(getrandom), NAME(membarrier), NAME(statx), NAME(clone3),
};
#undef NAME

void ps5_syscall_report(void)
{
	char line[1024];
	size_t used;
	int number, any = 0;

	used = (size_t)snprintf(line, sizeof(line), "system calls answered -ENOSYS:");
	for (number = 0; number < CALL_NUMBERS; number++)
	{
		uint32_t count = __atomic_load_n(&enosys_counts[number], __ATOMIC_RELAXED);

		if (!count || used >= sizeof(line))
			continue;
		used += (size_t)snprintf(line + used, sizeof(line) - used, " %s (%d) %u", call_names[number] ?
			call_names[number] : "?", number, (unsigned)count);
		any = 1;
	}
	if (!any)
		snprintf(line + used, sizeof(line) - used, " none");
	ps5_log_line(line);
}

/* once per call that is answered without the system, the first time */
static void log_once(int *done, const char *text)
{
	if (!__atomic_exchange_n(done, 1, __ATOMIC_RELAXED))
		ps5_log("%s", text);
}

/* ---------- paths

The guest's working folder is the host's answer: the data folder, which
chdir keeps (getcwd's import crashes a title, phase 0 B). A relative path
on the working folder is made absolute here, so the console's own working
folder (the title's /app0, where nothing is written) never matters. */

#define PATH_SIZE 1024

static const char guest_cwd[] = PS5_DATA_FOLDER;

/* the path to give the system for the guest's (directory, path): an
absolute one, a relative one on a descriptor, or one on the working folder
made absolute in buffer; NULL with *error set */
static const char *path_of(int directory, uint64_t path, char *buffer, long *error)
{
	const char *text = GUEST(const char *, path);

	if (!path)
	{
		*error = -GUEST_EFAULT;
		return NULL;
	}
	if (text[0] == '/' || directory != GUEST_AT_FDCWD)
		return text;
	if (!text[0])
	{
		*error = -GUEST_ENOENT;
		return NULL;
	}
	if ((size_t)snprintf(buffer, PATH_SIZE, "%s/%s", guest_cwd, text) >= PATH_SIZE)
	{
		*error = -GUEST_ENAMETOOLONG;
		return NULL;
	}
	return buffer;
}

/* the desktop game finds its data beside its executable (xbox_files.c):
the data folder */
#define GUEST_EXECUTABLE PS5_DATA_FOLDER "/halo"

/* ---------- what the log shows of the game's files */

/* the first start's extraction (sdl_platform.c's data_extract, xiso.c)
writes the maps under maps.partial/ and renames the folder maps; the
game's maps are opened from maps/. Each named once, with the time. */
#define NOTED_FILES 64

static char noted_files[NOTED_FILES][96];
static int noted_count;
static pthread_mutex_t noted_lock = PTHREAD_MUTEX_INITIALIZER;

static void note_file(const char *path, long flags)
{
	const char *partial = strstr(path, "maps.partial/");
	const char *map = strstr(path, "/maps/");
	size_t length = strlen(path);
	const char *what = NULL;
	int index;

	if (partial && (flags & FREEBSD_O_CREAT))
		what = "extraction: writing";
	else if (map && length > 4 && !strcmp(path + length - 4, ".map"))
		what = "map: opening";
	if (!what)
		return;
	pthread_mutex_lock(&noted_lock);
	for (index = 0; index < noted_count; index++)
	{
		if (!strncmp(noted_files[index], path, sizeof(noted_files[index]) - 1))
			break;
	}
	if (index == noted_count && noted_count < NOTED_FILES)
	{
		snprintf(noted_files[noted_count++], sizeof(noted_files[0]), "%s", path);
		pthread_mutex_unlock(&noted_lock);
		ps5_log("%s %s at %.1f s", what, path, ps5_seconds());
		return;
	}
	pthread_mutex_unlock(&noted_lock);
}

/* ---------- structures */

static int timespec_in(uint64_t address, struct timespec *result)
{
	const struct halo_guest_timespec *value = GUEST(const struct halo_guest_timespec *, address);

	if (!address)
		return 0;
	result->tv_sec = value->seconds;
	result->tv_nsec = value->nanoseconds;
	return 1;
}

static void timespec_out(uint64_t address, const struct timespec *value)
{
	struct halo_guest_timespec *result = GUEST(struct halo_guest_timespec *, address);

	if (!address)
		return;
	result->seconds = (int32_t)value->tv_sec;
	result->nanoseconds = (int32_t)value->tv_nsec;
}

/* ppoll: the guest's pollfd is the host's layout (a 32-bit descriptor, two
16-bit masks), its events translated each way (host_syscall_tables.c);
the timeout in milliseconds, rounded up (none, -1: no limit); the signal
mask ignored, as no signal of the guest's is ever delivered */
#define PPOLL_ON_STACK 64

static long guest_ppoll(uint64_t descriptors, uint32_t count, uint64_t timeout_address)
{
	struct halo_guest_pollfd
	{
		int32_t fd;
		int16_t events;
		int16_t revents;
	} *guest = GUEST(struct halo_guest_pollfd *, descriptors);
	struct pollfd stack[PPOLL_ON_STACK];
	struct pollfd *host = stack;
	struct timespec timeout;
	long long milliseconds = -1;
	uint32_t index;
	int result;

	_Static_assert(sizeof(struct pollfd) == 8 && sizeof(struct halo_guest_pollfd) == 8, "pollfd's layout");
	if (count && !descriptors)
		return -GUEST_EFAULT;
	if (count > PPOLL_ON_STACK)
	{
		host = malloc(sizeof(*host) * count);
		if (!host)
			return -GUEST_ENOMEM;
	}
	for (index = 0; index < count; index++)
	{
		host[index].fd = guest[index].fd;
		host[index].events = (short)ps5_poll_events_in((uint16_t)guest[index].events);
		host[index].revents = 0;
	}
	if (timespec_in(timeout_address, &timeout))
	{
		if (timeout.tv_sec < 0 || timeout.tv_nsec < 0 || timeout.tv_nsec >= 1000000000L)
		{
			if (host != stack)
				free(host);
			return -GUEST_EINVAL;
		}
		milliseconds = (long long)timeout.tv_sec * 1000 + (timeout.tv_nsec + 999999) / 1000000;
		if (milliseconds > INT32_MAX)
			milliseconds = INT32_MAX;
	}
	result = poll(host, (nfds_t)count, (int)milliseconds);
	for (index = 0; result >= 0 && index < count; index++)
		guest[index].revents = (int16_t)ps5_poll_events_out((uint16_t)host[index].revents,
			(uint16_t)guest[index].events);
	if (host != stack)
		free(host);
	return result_of(result);
}

static void kstat_out(const struct stat *value, uint64_t address)
{
	struct ps5_stat_fields fields;

	fields.device = value->st_dev;
	fields.inode = value->st_ino;
	fields.mode = value->st_mode;
	fields.links = value->st_nlink;
	fields.uid = value->st_uid;
	fields.gid = value->st_gid;
	fields.rdev = value->st_rdev;
	fields.size = value->st_size;
	fields.block_size = value->st_blksize;
	fields.blocks = value->st_blocks;
	fields.atime_seconds = value->st_atim.tv_sec;
	fields.atime_nanoseconds = value->st_atim.tv_nsec;
	fields.mtime_seconds = value->st_mtim.tv_sec;
	fields.mtime_nanoseconds = value->st_mtim.tv_nsec;
	fields.ctime_seconds = value->st_ctim.tv_sec;
	fields.ctime_nanoseconds = value->st_ctim.tv_nsec;
	ps5_kstat_fill(GUEST(struct halo_guest_kstat *, address), &fields);
}

static long guest_fstatat(int directory, uint64_t path, uint64_t result, uint32_t guest_flags)
{
	char buffer[PATH_SIZE];
	const char *host_path;
	struct stat value;
	long error = 0, flags;
	int empty_path;

	if (!result)
		return -GUEST_EFAULT;
	flags = ps5_at_flags_in(guest_flags, &empty_path);
	if (flags < 0)
		return flags;
	/* (musl's fstat: the descriptor itself, AT_EMPTY_PATH with "") */
	if (empty_path && path && !GUEST(const char *, path)[0])
	{
		if (fstat(directory, &value) != 0)
			return -ps5_errno_out(errno);
		kstat_out(&value, result);
		return 0;
	}
	host_path = path_of(directory, path, buffer, &error);
	if (!host_path)
		return error;
	if (fstatat(directory, host_path, &value, (int)flags) != 0)
		return -ps5_errno_out(errno);
	kstat_out(&value, result);
	return 0;
}

/* ---------- standard output and error: the log, a line at a time */

struct log_stream
{
	char line[960];
	size_t length;
};

static struct log_stream log_streams[2];
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_bytes(int fd, const char *bytes, size_t size)
{
	struct log_stream *stream = &log_streams[fd == 2];
	size_t index;

	pthread_mutex_lock(&log_lock);
	for (index = 0; index < size; index++)
	{
		char c = bytes[index];

		if (c == '\n' || stream->length == sizeof(stream->line) - 1)
		{
			stream->line[stream->length] = 0;
			ps5_log("game: %s", stream->line);
			stream->length = 0;
			if (c == '\n')
				continue;
		}
		stream->line[stream->length++] = c;
	}
	pthread_mutex_unlock(&log_lock);
}

/* ---------- vectored reads and writes (Android's) */

#define IOVEC_STACK_COUNT 64

static long iovec_in(uint64_t vector, int count, struct iovec *buffer, struct iovec **result)
{
	const struct halo_guest_iovec *guest_vector = GUEST(const struct halo_guest_iovec *, vector);
	struct iovec *host_vector = buffer;
	int index;

	if (count < 0 || count > GUEST_IOV_MAX)
		return -GUEST_EINVAL;
	if (count && !vector)
		return -GUEST_EFAULT;
	if (count > IOVEC_STACK_COUNT)
	{
		host_vector = malloc((size_t)count * sizeof(*host_vector));
		if (!host_vector)
			return -GUEST_ENOMEM;
	}
	for (index = 0; index < count; index++)
	{
		host_vector[index].iov_base = GUEST(void *, guest_vector[index].base);
		host_vector[index].iov_len = guest_vector[index].length;
	}
	*result = host_vector;
	return 0;
}

static void iovec_free(struct iovec *vector, struct iovec *buffer)
{
	if (vector != buffer)
		free(vector);
}

static long guest_writev(int fd, uint64_t vector, int count, int64_t offset, int positional)
{
	struct iovec buffer[IOVEC_STACK_COUNT];
	struct iovec *host_vector;
	long result = iovec_in(vector, count, buffer, &host_vector);

	if (result)
		return result;
	if (fd == 1 || fd == 2)
	{
		int index;

		for (index = 0; index < count; index++)
		{
			log_bytes(fd, host_vector[index].iov_base, host_vector[index].iov_len);
			result += (long)host_vector[index].iov_len;
		}
		iovec_free(host_vector, buffer);
		return result;
	}
	if (positional)
		result = result_of(pwritev(fd, host_vector, count, offset));
	else
		result = result_of(writev(fd, host_vector, count));
	iovec_free(host_vector, buffer);
	return result;
}

static long guest_readv(int fd, uint64_t vector, int count, int64_t offset, int positional)
{
	struct iovec buffer[IOVEC_STACK_COUNT];
	struct iovec *host_vector;
	long result = iovec_in(vector, count, buffer, &host_vector);

	if (result)
		return result;
	if (positional)
		result = result_of(preadv(fd, host_vector, count, offset));
	else
		result = result_of(readv(fd, host_vector, count));
	iovec_free(host_vector, buffer);
	return result;
}

/* a 64-bit offset from the two longs musl passes for it, low half first */
static int64_t offset_of_halves(long long low, long long high)
{
	return (int64_t)((uint64_t)(uint32_t)low | ((uint64_t)(uint32_t)high << 32));
}

/* ---------- directories

FreeBSD's getdents fills a buffer with its own records, which are repacked
as Linux's (host_syscall_tables.c). They grow in the repacking, so what a
guest's buffer cannot take is kept for its next call: each open
directory's records are read into a buffer of its own and handed out from
there, as FreeBSD's readdir does with its own. A seek or a close of the
descriptor drops them. */

#define DIRECTORIES 16
#define DIRECTORY_BUFFER 65536

struct directory
{
	int fd; /* -1: free */
	uint8_t *buffer;
	size_t size, used;
	int64_t offset;
};

static struct directory directories[DIRECTORIES];
static pthread_mutex_t directory_lock = PTHREAD_MUTEX_INITIALIZER;
static int directories_ready;

/* with the lock held */
static struct directory *directory_of(int fd, int create)
{
	struct directory *free_entry = NULL;
	int index;

	if (!directories_ready)
	{
		for (index = 0; index < DIRECTORIES; index++)
			directories[index].fd = -1;
		directories_ready = 1;
	}
	for (index = 0; index < DIRECTORIES; index++)
	{
		if (directories[index].fd == fd)
			return &directories[index];
		if (directories[index].fd < 0 && !free_entry)
			free_entry = &directories[index];
	}
	if (!create || !free_entry)
		return NULL;
	if (!free_entry->buffer)
		free_entry->buffer = malloc(DIRECTORY_BUFFER);
	if (!free_entry->buffer)
		return NULL;
	free_entry->fd = fd;
	free_entry->size = free_entry->used = 0;
	free_entry->offset = 0;
	return free_entry;
}

static void directory_forget(int fd)
{
	struct directory *entry;

	pthread_mutex_lock(&directory_lock);
	entry = directories_ready ? directory_of(fd, 0) : NULL;
	if (entry)
		entry->fd = -1;
	pthread_mutex_unlock(&directory_lock);
}

static long guest_getdents64(int fd, uint64_t address, uint32_t count)
{
	uint8_t *out = GUEST(uint8_t *, address);
	struct directory *entry;
	long written = 0;

	if (!address)
		return -GUEST_EFAULT;
	pthread_mutex_lock(&directory_lock);
	entry = directory_of(fd, 1);
	if (!entry)
	{
		pthread_mutex_unlock(&directory_lock);
		return -GUEST_ENOMEM;
	}
	for (;;)
	{
		size_t consumed = 0;
		long packed;

		if (entry->used == entry->size)
		{
			int got = getdents(fd, (char *)entry->buffer, DIRECTORY_BUFFER);

			if (got < 0)
			{
				written = written ? written : -ps5_errno_out(errno);
				break;
			}
			if (got == 0)
				break;
			entry->size = (size_t)got;
			entry->used = 0;
		}
		packed = ps5_dirents_repack(entry->buffer + entry->used, entry->size - entry->used, out + written,
			count - (size_t)written, &consumed, &entry->offset);
		if (packed < 0)
		{
			/* (the next record does not fit: the guest's next call takes it) */
			if (!written)
				written = packed;
			break;
		}
		written += packed;
		entry->used += consumed;
		if (entry->used < entry->size)
			break;
	}
	pthread_mutex_unlock(&directory_lock);
	return written;
}

/* ---------- descriptors */

/* (O_CLOEXEC asks for nothing here: no process is started, and the
console refuses F_SETFD) */
static long set_descriptor_flags(int fd, long open_flags)
{
	if ((open_flags & FREEBSD_O_NONBLOCK) && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) != 0)
		return -ps5_errno_out(errno);
	return 0;
}

static long guest_fcntl(int fd, int command, uint64_t argument)
{
	switch (command)
	{
	case GUEST_F_DUPFD:
		return result_of(fcntl(fd, F_DUPFD, (int)argument));
	case GUEST_F_DUPFD_CLOEXEC:
		return result_of(fcntl(fd, F_DUPFD_CLOEXEC, (int)argument));
	case GUEST_F_GETFD:
	case GUEST_F_SETFD:
		/* (the console refuses F_SETFD with EINVAL, measured in phase 2: and
		close-on-exec means nothing where no process is started. A
		descriptor that is open answers, as if it were set) */
		if (fcntl(fd, F_GETFL) == -1)
			return -ps5_errno_out(errno);
		return command == GUEST_F_GETFD ? GUEST_FD_CLOEXEC : 0;
	case GUEST_F_GETFL:
	{
		long result = result_of(fcntl(fd, F_GETFL));

		return result < 0 ? result : (long)ps5_open_flags_out(result);
	}
	case GUEST_F_SETFL:
	{
		long flags = ps5_open_flags_in((uint32_t)argument);

		if (flags < 0)
			return flags;
		return result_of(fcntl(fd, F_SETFL, (int)flags));
	}
	case GUEST_F_GETLK:
	case GUEST_F_SETLK:
	case GUEST_F_SETLKW:
	case GUEST_F_OFD_GETLK:
	case GUEST_F_OFD_SETLK:
	case GUEST_F_OFD_SETLKW:
		/* (record locks: nothing in the game takes one) */
		return -GUEST_ENOSYS;
	}
	return -GUEST_EINVAL;
}

/* ---------- identity and limits */

/* sysinfo's sizes in the guest's 32-bit fields: counted in larger units
(mem_unit) until they fit, as a 64-bit kernel does for a 32-bit program */
static void sysinfo_out(struct halo_guest_sysinfo *result, uint64_t uptime, uint64_t total, uint64_t free_memory)
{
	uint32_t unit = 1;

	memset(result, 0, sizeof(*result));
	while ((total > UINT32_MAX || free_memory > UINT32_MAX) && unit < 0x80000000u)
	{
		unit <<= 1;
		total >>= 1;
		free_memory >>= 1;
	}
	result->uptime = uptime > UINT32_MAX ? UINT32_MAX : (uint32_t)uptime;
	result->totalram = (uint32_t)total;
	result->freeram = (uint32_t)free_memory;
	result->procs = 1;
	result->mem_unit = unit;
}

/* struct rlimit for the guest: two 64-bit values, the current and the most */
static void rlimit_out(int resource, uint64_t address)
{
	uint64_t *limits = GUEST(uint64_t *, address);
	uint64_t value = UINT64_MAX;

	if (!address)
		return;
	if (resource == GUEST_RLIMIT_STACK)
		value = 8 * 1024 * 1024;
	else if (resource == GUEST_RLIMIT_NOFILE)
		value = 1024;
	limits[0] = value;
	limits[1] = value;
}

static long guest_uname(uint64_t address)
{
	/* Linux's struct utsname: six fields of 65 characters */
	static const char *const fields[6] = { "Linux", "ps5", "5.15.0", "#1 PS5", "x86_64", "(none)" };
	char *out = GUEST(char *, address);
	int index;

	if (!address)
		return -GUEST_EFAULT;
	memset(out, 0, 6 * 65);
	for (index = 0; index < 6; index++)
		snprintf(out + index * 65, 65, "%s", fields[index]);
	return 0;
}

/* ---------- dispatch */

/* the guest's umask: the console's kernel keeps none a title can set
(ps5platform/libc.h), so the guest's is only remembered; what the guest
makes in the data folder is the player's (below), 0 as the mask, as the
plan has it */
static uint32_t guest_umask = 0;

/* A file or folder the guest makes in the data folder is made open to the
console's FTP server, which sees only what everyone may: a file 0666, a
folder 0777, whatever mode the game asked for and the kernel's mask left
(host_main.c's open_player_folder does the same at the start for what is
there). */
static int in_data_folder(const char *path)
{
	return !strncmp(path, PS5_DATA_FOLDER "/", sizeof(PS5_DATA_FOLDER)) || !strcmp(path, PS5_DATA_FOLDER);
}

static int times_logged, rusage_logged, statfs_logged;

static long long guest_call(long long number, long long a, long long b, long long c, long long d, long long e,
	long long f)
{
	char buffer[PATH_SIZE], second_buffer[PATH_SIZE];
	const char *path, *second_path;
	long error = 0;
	int handled;
	long long memory_result;

	/* the game's map cache files, kept in memory (host_memory_files.c, as
	Android's host): a call on one of their descriptors or paths */
	memory_result = host_memory_file_guest_descriptor_call(number, a, b, c, d, e, &handled);
	if (handled)
		return memory_result;
	if ((number == GUEST_SYS_openat || number == GUEST_SYS_newfstatat || number == GUEST_SYS_faccessat ||
		number == GUEST_SYS_unlinkat || number == GUEST_SYS_utimensat) && b)
	{
		path = path_of((int)a, (uint64_t)b, buffer, &error);
		if (path)
		{
			memory_result = host_memory_file_guest_path_call(number, path, a, b, c, d, &handled);
			if (handled)
				return memory_result;
		}
		error = 0;
	}

	switch (number)
	{
	/* ---------- files */

	case GUEST_SYS_openat:
	{
		long flags = ps5_open_flags_in((uint32_t)c);

		if (flags < 0)
			return flags;
		path = path_of((int)a, (uint64_t)b, buffer, &error);
		if (!path)
			return error;
		note_file(path, flags);
		{
			int fd = openat((int)a, path, (int)flags, (mode_t)(uint32_t)d);

			if (fd >= 0 && (flags & O_CREAT) && (path[0] != '/' || in_data_folder(path)))
				fchmod(fd, 0666);
			return result_of(fd);
		}
	}
	case GUEST_SYS_close:
		directory_forget((int)a);
		return result_of(close((int)a));
	case GUEST_SYS_read:
		return result_of(read((int)a, GUEST(void *, b), (size_t)(uint32_t)c));
	case GUEST_SYS_write:
		if (a == 1 || a == 2)
		{
			log_bytes((int)a, GUEST(const char *, b), (size_t)(uint32_t)c);
			return (uint32_t)c;
		}
		return result_of(write((int)a, GUEST(const void *, b), (size_t)(uint32_t)c));
	case GUEST_SYS_readv:
		return guest_readv((int)a, (uint64_t)b, (int)c, 0, 0);
	case GUEST_SYS_writev:
		return guest_writev((int)a, (uint64_t)b, (int)c, 0, 0);
	case GUEST_SYS_preadv:
		return guest_readv((int)a, (uint64_t)b, (int)c, offset_of_halves(d, e), 1);
	case GUEST_SYS_pwritev:
		return guest_writev((int)a, (uint64_t)b, (int)c, offset_of_halves(d, e), 1);
	case GUEST_SYS_pread64:
		return result_of(pread((int)a, GUEST(void *, b), (size_t)(uint32_t)c, (off_t)d));
	case GUEST_SYS_pwrite64:
		return result_of(pwrite((int)a, GUEST(const void *, b), (size_t)(uint32_t)c, (off_t)d));
	case GUEST_SYS__llseek:
	{
		/* _llseek(fd, offset_high, offset_low, result, whence): the new
		position through a pointer, the guest's result being 32 bits */
		int64_t offset = (int64_t)(((uint64_t)(uint32_t)b << 32) | (uint32_t)c);
		off_t position;

		if (!d)
			return -GUEST_EFAULT;
		position = lseek((int)a, (off_t)offset, (int)e);
		if (position == (off_t)-1)
			return -ps5_errno_out(errno);
		directory_forget((int)a);
		memcpy(GUEST(void *, d), &(int64_t){ (int64_t)position }, sizeof(int64_t));
		return 0;
	}
	case GUEST_SYS_fsync:
	case GUEST_SYS_fdatasync:
		return result_of(fsync((int)a));
	case GUEST_SYS_ftruncate:
		return result_of(ftruncate((int)a, (off_t)b));
	case GUEST_SYS_fstat:
	{
		struct stat value;

		if (!b)
			return -GUEST_EFAULT;
		if (fstat((int)a, &value) != 0)
			return -ps5_errno_out(errno);
		kstat_out(&value, (uint64_t)b);
		return 0;
	}
	case GUEST_SYS_newfstatat:
		return guest_fstatat((int)a, (uint64_t)b, (uint64_t)c, (uint32_t)d);
	case GUEST_SYS_statx:
		/* musl falls back to newfstatat */
		return -GUEST_ENOSYS;
	case GUEST_SYS_faccessat:
	{
		struct stat value;

		path = path_of((int)a, (uint64_t)b, buffer, &error);
		if (!path)
			return error;
		/* the platform layer's access (phase 0 B: it answers where
		libkernel's refuses every path); on a descriptor, whether the file
		is there */
		if (path[0] == '/')
			return result_of(access(path, (int)c));
		return result_of(fstatat((int)a, path, &value, 0));
	}
	case GUEST_SYS_readlinkat:
	{
		struct stat value;
		size_t length;

		path = path_of((int)a, (uint64_t)b, buffer, &error);
		if (!path)
			return error;
		if (!strcmp(GUEST(const char *, b), "/proc/self/exe"))
		{
			length = strlen(GUEST_EXECUTABLE);
			if (length > (size_t)(uint32_t)d)
				length = (size_t)(uint32_t)d;
			memcpy(GUEST(char *, c), GUEST_EXECUTABLE, length);
			return (long)length;
		}
		/* (no symbolic links here, and no readlink a title can call) */
		if (fstatat((int)a, path, &value, AT_SYMLINK_NOFOLLOW) != 0)
			return -ps5_errno_out(errno);
		return -GUEST_EINVAL;
	}
	case GUEST_SYS_mkdirat:
		path = path_of((int)a, (uint64_t)b, buffer, &error);
		if (!path)
			return error;
		if (mkdirat((int)a, path, (mode_t)(uint32_t)c) != 0)
			return -ps5_errno_out(errno);
		if (path[0] != '/' || in_data_folder(path))
			fchmodat((int)a, path, 0777, 0);
		return 0;
	case GUEST_SYS_unlinkat:
	{
		int empty_path;
		long flags = ps5_at_flags_in((uint32_t)c, &empty_path);

		if (flags < 0)
			return flags;
		path = path_of((int)a, (uint64_t)b, buffer, &error);
		if (!path)
			return error;
		return result_of(unlinkat((int)a, path, (int)flags));
	}
	case GUEST_SYS_renameat2:
		if ((uint32_t)e)
			return -GUEST_EINVAL;
		/* FALLTHROUGH */
	case GUEST_SYS_renameat:
		path = path_of((int)a, (uint64_t)b, buffer, &error);
		second_path = path ? path_of((int)c, (uint64_t)d, second_buffer, &error) : NULL;
		if (!path || !second_path)
			return error;
		return result_of(renameat((int)a, path, (int)c, second_path));
	case GUEST_SYS_fchmod:
		return result_of(fchmod((int)a, (mode_t)(uint32_t)b));
	case GUEST_SYS_fchmodat:
		path = path_of((int)a, (uint64_t)b, buffer, &error);
		if (!path)
			return error;
		return result_of(fchmodat((int)a, path, (mode_t)(uint32_t)c, 0));
	case GUEST_SYS_utimensat:
	{
		struct timespec times[2];
		int empty_path, index;
		long flags = ps5_at_flags_in((uint32_t)d, &empty_path);

		if (flags < 0)
			return flags;
		for (index = 0; c && index < 2; index++)
		{
			timespec_in((uint64_t)c + (uint64_t)index * sizeof(struct halo_guest_timespec), &times[index]);
			times[index].tv_nsec = ps5_utime_nanoseconds_in((int32_t)times[index].tv_nsec);
		}
		/* (musl's futimens: the descriptor itself, with no path) */
		if (!b)
			return result_of(futimens((int)a, c ? times : NULL));
		path = path_of((int)a, (uint64_t)b, buffer, &error);
		if (!path)
			return error;
		return result_of(utimensat((int)a, path, c ? times : NULL, (int)flags));
	}
	case GUEST_SYS_getdents64:
		return guest_getdents64((int)a, (uint64_t)b, (uint32_t)c);
	case GUEST_SYS_getcwd:
	{
		size_t length = strlen(guest_cwd) + 1;

		if (!a)
			return -GUEST_EFAULT;
		if ((size_t)(uint32_t)b < length)
			return -GUEST_ERANGE;
		memcpy(GUEST(char *, a), guest_cwd, length);
		return (long)length;
	}
	case GUEST_SYS_chdir:
	{
		const char *text = GUEST(const char *, a);

		/* the working folder stays the data folder */
		if (!a)
			return -GUEST_EFAULT;
		if (!strcmp(text, guest_cwd) || !strcmp(text, PS5_DATA_FOLDER "/") || !strcmp(text, "."))
			return 0;
		ps5_log("the game asked to change its working folder to %s: refused", text);
		return -GUEST_EACCES;
	}
	case GUEST_SYS_statfs:
	case GUEST_SYS_fstatfs:
		/* (counted: the game's free space is the host's posix_disk_space) */
		log_once(&statfs_logged, "statfs: not answered (-ENOSYS)");
		return -GUEST_ENOSYS;
	case GUEST_SYS_dup:
		return result_of(dup((int)a));
	case GUEST_SYS_dup3:
	{
		long flags = ps5_open_flags_in((uint32_t)c), status;

		if (flags < 0 || (flags & ~FREEBSD_O_CLOEXEC) || a == b)
			return -GUEST_EINVAL;
		status = result_of(dup2((int)a, (int)b));
		if (status >= 0 && set_descriptor_flags((int)b, flags) != 0)
			status = -ps5_errno_out(errno);
		return status;
	}
	case GUEST_SYS_pipe2:
	{
		int descriptors[2];
		long flags = ps5_open_flags_in((uint32_t)b), status;

		if (flags < 0 || (flags & ~(FREEBSD_O_CLOEXEC | FREEBSD_O_NONBLOCK)))
			return -GUEST_EINVAL;
		if (!a)
			return -GUEST_EFAULT;
		if (pipe(descriptors) != 0)
			return -ps5_errno_out(errno);
		status = set_descriptor_flags(descriptors[0], flags);
		if (!status)
			status = set_descriptor_flags(descriptors[1], flags);
		if (status)
		{
			close(descriptors[0]);
			close(descriptors[1]);
			return status;
		}
		memcpy(GUEST(int *, a), descriptors, sizeof(descriptors));
		return 0;
	}
	case GUEST_SYS_fcntl:
		return guest_fcntl((int)a, (int)b, (uint64_t)c);
	case GUEST_SYS_ioctl:
		return -GUEST_ENOTTY;
	case GUEST_SYS_flock:
		return result_of(flock((int)a, (int)b));
	case GUEST_SYS_umask:
	{
		uint32_t previous = guest_umask;

		guest_umask = (uint32_t)a & 0777;
		return previous;
	}

	/* ---------- memory (host_memory.c) */

	case GUEST_SYS_mmap:
		return host_guest_mmap((uint64_t)a, (uint64_t)b, (int)c, (int)d, (int)e, f);
	case GUEST_SYS_munmap:
		return host_guest_munmap((uint64_t)a, (uint64_t)b);
	case GUEST_SYS_mprotect:
		return host_guest_mprotect((uint64_t)a, (uint64_t)b, (int)c);
	case GUEST_SYS_madvise:
		return host_guest_madvise((uint64_t)a, (uint64_t)b, (int)c);
	case GUEST_SYS_mremap:
		/* musl's realloc falls back to a new mapping and a copy */
		return -GUEST_ENOSYS;
	case GUEST_SYS_brk:
		/* musl then takes its memory from mmap (Android's answer) */
		return -GUEST_ENOMEM;

	/* ---------- threads and time */

	case GUEST_SYS_futex:
		return host_futex((uint32_t)a, (int)b, (uint32_t)c, (uint64_t)d, (uint64_t)e, (uint32_t)f);
	case GUEST_SYS_set_tid_address:
	case GUEST_SYS_gettid:
		return ps5_thread_id();
	case GUEST_SYS_getpid:
		return getpid();
	case GUEST_SYS_getppid:
		return getppid();
	case GUEST_SYS_sched_yield:
		return result_of(sched_yield());
	case GUEST_SYS_nanosleep:
	{
		struct timespec request, remaining;
		long result;

		if (!timespec_in((uint64_t)a, &request))
			return -GUEST_EFAULT;
		result = result_of(nanosleep(&request, &remaining));
		if (result == -GUEST_EINTR)
			timespec_out((uint64_t)b, &remaining);
		return result;
	}
	case GUEST_SYS_clock_nanosleep:
	{
		struct timespec request, remaining;
		long clock = ps5_clock_in((int32_t)a);
		int absolute = ((int)b & GUEST_TIMER_ABSTIME) != 0;
		int result;

		if (clock < 0)
			return clock;
		if (!timespec_in((uint64_t)c, &request))
			return -GUEST_EFAULT;
		result = clock_nanosleep((clockid_t)clock, absolute ? TIMER_ABSTIME : 0, &request, &remaining);
		if (result == EINTR && !absolute)
			timespec_out((uint64_t)d, &remaining);
		return result ? -ps5_errno_out(result) : 0;
	}
	case GUEST_SYS_clock_gettime:
	case GUEST_SYS_clock_getres:
	{
		struct timespec value;
		long clock = ps5_clock_in((int32_t)a);
		int result;

		if (clock < 0)
			return clock;
		result = number == GUEST_SYS_clock_gettime ? clock_gettime((clockid_t)clock, &value) :
			clock_getres((clockid_t)clock, &value);
		if (result == 0)
			timespec_out((uint64_t)b, &value);
		return result_of(result);
	}
	case GUEST_SYS_gettimeofday:
	{
		struct timespec value;
		struct halo_guest_timespec *result = GUEST(struct halo_guest_timespec *, a);

		clock_gettime(CLOCK_REALTIME, &value);
		if (result)
		{
			result->seconds = (int32_t)value.tv_sec;
			result->nanoseconds = (int32_t)(value.tv_nsec / 1000);
		}
		return 0;
	}
	case GUEST_SYS_times:
		/* struct tms, four 32-bit clock_t: zeros (no process accounting) */
		log_once(&times_logged, "times: answered with zeros");
		if (a)
			memset(GUEST(void *, a), 0, 16);
		return 0;
	case GUEST_SYS_getrusage:
		/* struct rusage with 32-bit longs: two timevals and fourteen longs */
		log_once(&rusage_logged, "getrusage: answered with zeros");
		if (b)
			memset(GUEST(void *, b), 0, 72);
		return 0;
	case GUEST_SYS_getitimer:
	case GUEST_SYS_setitimer:
	case GUEST_SYS_timer_create:
	case GUEST_SYS_timer_gettime:
	case GUEST_SYS_timer_settime:
		/* (their signals would be the host's) */
		return -GUEST_ENOSYS;
	case GUEST_SYS_clone:
	case GUEST_SYS_clone3:
		/* the guest's threads are host_thread_create's; musl falls back
		from clone3 to clone, and never reaches either */
		return -GUEST_ENOSYS;
	case GUEST_SYS_execve:
	case GUEST_SYS_wait4:
	case GUEST_SYS_waitid:
		/* a title starts no processes */
		return -GUEST_ENOSYS;

	/* ---------- signals: the host's */

	case GUEST_SYS_rt_sigaction:
	case GUEST_SYS_sigaltstack:
		return 0;
	case GUEST_SYS_rt_sigprocmask:
		/* (no signal of the guest's is ever delivered: the old mask is empty) */
		if (c)
			memset(GUEST(void *, c), 0, 8);
		return 0;
	case GUEST_SYS_rt_sigtimedwait:
	case GUEST_SYS_kill:
	case GUEST_SYS_tkill:
	case GUEST_SYS_tgkill:
		/* musl's abort raises SIGABRT, then crashes on purpose: the host's
		handlers report that */
		return -GUEST_ENOSYS;

	/* ---------- identity and limits */

	case GUEST_SYS_getuid:
	case GUEST_SYS_geteuid:
	case GUEST_SYS_getgid:
	case GUEST_SYS_getegid:
		return 1000;
	case GUEST_SYS_uname:
		return guest_uname((uint64_t)a);
	case GUEST_SYS_sysinfo:
	{
		int64_t start = 0;
		size_t available = 0;

		if (!a)
			return -GUEST_EFAULT;
		sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(), PS5_KERNEL_DIRECT_ALIGNMENT, &start,
			&available);
		sysinfo_out(GUEST(struct halo_guest_sysinfo *, a), ps5_now_ns() / 1000000000ull,
			(uint64_t)sceKernelGetDirectMemorySize(), available);
		return 0;
	}
	case GUEST_SYS_getrlimit:
		rlimit_out((int)a, (uint64_t)b);
		return 0;
	case GUEST_SYS_prlimit64:
		/* (new limits are accepted and change nothing) */
		rlimit_out((int)b, (uint64_t)d);
		return 0;

	/* ---------- waiting on descriptors, and the network (port/ps5/PS5.md,
	phase 5): the game's sockets are the host's (posix_net.c's helpers), so
	none of these is asked for by the game itself; musl's poll is ppoll */

	case GUEST_SYS_ppoll:
		return guest_ppoll((uint64_t)a, (uint32_t)b, (uint64_t)c);
	case GUEST_SYS_sendmsg:
	case GUEST_SYS_recvmsg:
	case GUEST_SYS_epoll_pwait:
	case GUEST_SYS_pselect6:
		return -GUEST_ENOSYS;

	/* ---------- the rest */

	case GUEST_SYS_getrandom:
		if ((uint32_t)b && !a)
			return -GUEST_EFAULT;
		arc4random_buf(GUEST(void *, a), (size_t)(uint32_t)b);
		return (uint32_t)b;
	case GUEST_SYS_membarrier:
		return -GUEST_ENOSYS;
	case GUEST_SYS_exit:
	case GUEST_SYS_exit_group:
		/* (the guest's threads end by returning to the host, so a thread's
		exit is the game's, as Android's host takes it) */
		host_exit((int)a);
	}
	host_logf(HOST_LOG_WARN, "guest system call %lld is not supported", number);
	return -GUEST_ENOSYS;
}

long long host_syscall(long long number, long long a, long long b, long long c, long long d, long long e,
	long long f)
{
	static uint32_t failures_logged;
	long long result = guest_call(number, a, b, c, d, e, f);

	if (result == -GUEST_ENOSYS && number >= 0 && number < CALL_NUMBERS)
		__atomic_fetch_add(&enosys_counts[number], 1, __ATOMIC_RELAXED);
	/* a failure, the first FAILURES_LOGGED times, with the path of a call
	that names a file (not a file that is not there, which the game asks
	about all the time; not a futex wait that found the word changed or
	timed out, which is how futexes work; an -ENOSYS is counted instead) */
	else if (result < 0 && result > -4096 && result != -GUEST_ENOENT && !(number == GUEST_SYS_futex &&
		(result == -GUEST_EAGAIN || result == -GUEST_ETIMEDOUT)) &&
		__atomic_fetch_add(&failures_logged, 1, __ATOMIC_RELAXED) < FAILURES_LOGGED)
	{
		const char *name = number >= 0 && number < CALL_NUMBERS && call_names[number] ? call_names[number] : "?";
		int named = number == GUEST_SYS_openat || number == GUEST_SYS_newfstatat || number == GUEST_SYS_faccessat ||
			number == GUEST_SYS_mkdirat || number == GUEST_SYS_unlinkat || number == GUEST_SYS_renameat ||
			number == GUEST_SYS_renameat2 || number == GUEST_SYS_readlinkat || number == GUEST_SYS_fchmodat ||
			number == GUEST_SYS_utimensat;

		if (named && b)
			host_logf(HOST_LOG_INFO, "guest %s(%lld, \"%.160s\", 0x%llx) = %lld", name, a, GUEST(const char *, b),
				(unsigned long long)c, result);
		else
			host_logf(HOST_LOG_INFO, "guest %s(0x%llx, 0x%llx, 0x%llx, 0x%llx) = %lld", name, (unsigned long long)a,
				(unsigned long long)b, (unsigned long long)c, (unsigned long long)d, result);
	}
	return result;
}
