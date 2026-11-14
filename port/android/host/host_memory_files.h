/*
HOST_MEMORY_FILES.H

The game's map cache files kept in the host's memory (host_memory_files.c),
on every host: Android's, the PS5's (which builds Android's file) and the
Switch's.

host_memory_file_path gives a path's slot, or -1; an open gives a
descriptor of HOST_MEMORY_FILE_DESCRIPTOR and up; the rest take one and
answer as the system calls do, a negated errno on failure.
*/

#ifndef __HALO_HOST_MEMORY_FILES_H
#define __HALO_HOST_MEMORY_FILES_H

#include <stddef.h>
#include <stdint.h>

#define HOST_MEMORY_FILE_DESCRIPTOR 0x40000000

struct stat;
struct timespec;

int host_memory_file_path(const char *path);
int host_memory_file_is(int descriptor);
int host_memory_file_open(int slot, const char *path, int flags);
int host_memory_file_close(int descriptor);
long long host_memory_file_read(int descriptor, void *buffer, size_t count);
long long host_memory_file_write(int descriptor, const void *buffer, size_t count);
long long host_memory_file_pread(int descriptor, void *buffer, size_t count, long long offset);
long long host_memory_file_pwrite(int descriptor, const void *buffer, size_t count, long long offset);
long long host_memory_file_seek(int descriptor, long long offset, int whence);
int host_memory_file_truncate(int descriptor, long long size);
int host_memory_file_fstat(int descriptor, struct stat *out);
int host_memory_file_stat(int slot, const char *path, struct stat *out);
int host_memory_file_unlink(int slot, const char *path);
int host_memory_file_set_times(int slot, const struct timespec *times);

#ifndef __SWITCH__
/* Android's and the PS5's guests use the host's descriptors as their own
(the Switch's host translates them, host_syscall.c, and calls the functions
above itself). Each host's host_syscall sends a guest call here first:

- host_memory_file_guest_descriptor_call: a call on a descriptor, when the
  descriptor (its first argument) is a memory file's;
- host_memory_file_guest_path_call: a call on a path (openat, newfstatat,
  faccessat, unlinkat, utimensat), with the path as the host resolved it.

Each sets *handled when the call was a memory file's, and then returns the
guest's answer: a result, or a negated errno of the guest's
(halo_guest_syscall.h). Otherwise the host makes the call as before. */
long long host_memory_file_guest_descriptor_call(long long number, long long a, long long b, long long c,
	long long d, long long e, int *handled);
long long host_memory_file_guest_path_call(long long number, const char *path, long long a, long long b,
	long long c, long long d, int *handled);
#endif

#endif
