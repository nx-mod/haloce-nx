/* (tools/test_mcc_fast.py) Winsock and the threads, over POSIX */
#ifndef MCC_FAST_SHIM_XTL_H
#define MCC_FAST_SHIM_XTL_H
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define WSAEWOULDBLOCK 10035
#define FIONBIO_SHIM 1
#undef FIONBIO
#define FIONBIO FIONBIO_SHIM
#define __stdcall
typedef struct { unsigned int fd_count; SOCKET fd_array[64]; } shim_fd_set;
#define fd_set shim_fd_set
static inline int closesocket(SOCKET s) { return close(s); }
static inline int ioctlsocket(SOCKET s, long command, unsigned long *argument)
{
	int flags = fcntl(s, F_GETFL, 0);
	(void)command;
	return fcntl(s, F_SETFL, *argument ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}
static inline int WSAGetLastError(void)
{
	return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS ? WSAEWOULDBLOCK : errno;
}
static inline int __WSAFDIsSet(SOCKET s, fd_set *set)
{
	unsigned int i;
	for (i = 0; i < set->fd_count; i++) if (set->fd_array[i] == s) return 1;
	return 0;
}
static inline int shim_select(int count, fd_set *readable, fd_set *writeable, fd_set *failed, struct timeval *timeout)
{
	struct pollfd polls[192];
	unsigned int n = 0, i, j;
	int ready, total = 0;
	(void)count;
	for (i = 0; readable && i < readable->fd_count; i++) { polls[n].fd = readable->fd_array[i]; polls[n++].events = POLLIN; }
	for (i = 0; writeable && i < writeable->fd_count; i++) { polls[n].fd = writeable->fd_array[i]; polls[n++].events = POLLOUT; }
	for (i = 0; failed && i < failed->fd_count; i++) { polls[n].fd = failed->fd_array[i]; polls[n++].events = 0; }
	ready = poll(polls, n, timeout ? (int)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000) : -1);
	if (ready < 0) return -1;
	j = 0;
	if (readable) { unsigned int k = 0; for (i = 0; i < readable->fd_count; i++, j++) if (polls[j].revents & (POLLIN | POLLHUP | POLLERR)) readable->fd_array[k++] = polls[j].fd; readable->fd_count = k; total += (int)k; }
	if (writeable) { unsigned int k = 0; for (i = 0; i < writeable->fd_count; i++, j++) if (polls[j].revents & POLLOUT) writeable->fd_array[k++] = polls[j].fd; writeable->fd_count = k; total += (int)k; }
	if (failed) { unsigned int k = 0; for (i = 0; i < failed->fd_count; i++, j++) if (polls[j].revents & (POLLERR | POLLHUP)) failed->fd_array[k++] = polls[j].fd; failed->fd_count = k; total += (int)k; }
	return total;
}
#define select shim_select
static inline void Sleep(unsigned long milliseconds) { usleep((useconds_t)milliseconds * 1000); }
static inline void *CreateThread(void *a, unsigned long b, unsigned long (*function)(void *), void *argument,
	unsigned long c, unsigned long *d)
{
	pthread_t thread;
	(void)a; (void)b; (void)c; (void)d;
	if (pthread_create(&thread, NULL, (void *(*)(void *))function, argument)) return NULL;
	pthread_detach(thread);
	return (void *)1;
}
#endif
