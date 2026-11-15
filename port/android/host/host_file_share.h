/* Files between two SlipstreamCE machines over a TCP connection of their own
 * (host_file_share.c). The guest's calls are host_imports.list's host_fs_*,
 * declared again in port/android/guest/runtime/guest_host.h; addresses are
 * the guest's. */
#ifndef HOST_FILE_SHARE_H
#define HOST_FILE_SHARE_H

#include <stdint.h>

enum host_fs_fetch_state {
	HOST_FS_FETCH_IDLE,
	HOST_FS_FETCH_CONNECTING,
	HOST_FS_FETCH_RECEIVING,
	HOST_FS_FETCH_DONE,
	/* failed before anything was written: the old way may still be taken */
	HOST_FS_FETCH_FAILED_EARLY,
	/* failed after some files were written */
	HOST_FS_FETCH_FAILED_LATE,
};

/* what host_fs_fetch_status writes at the guest's address */
struct host_fs_fetch_status {
	int32_t state;
	/* the files whole and right, of those given, in order */
	int32_t files_done;
	/* bytes of the file after them, so far */
	uint32_t current_bytes;
	uint32_t received_low, received_high;
	char why[160];
};

/* ---------- the host (the game's host: serving a joiner) */

/* each frame: whether the game hosts (its listener opened or closed; every
 * transfer ended when not), and the upload's cap in bytes a second (0 none);
 * 1 while it listens */
int host_fs_host_frame(int hosting, unsigned int limit);
/* a transfer offered to a joiner: its session, its token (32 bytes at the
 * guest address), and how many files; then each file (its native path and
 * size), then ready; the slot, or -1 */
int host_fs_serve_begin(unsigned int session, unsigned int token, int count);
int host_fs_serve_file(unsigned int session, int index, unsigned int path, unsigned int size);
int host_fs_serve_ready(unsigned int session);
/* writes four 32-bit words: the bytes sent (low, high), the files sent whole,
 * and the connections (low 16 bits; bit 16 when the joiner said it has all);
 * 0 for a session not served */
int host_fs_serve_progress(unsigned int session, unsigned int out);
void host_fs_serve_end(unsigned int session);

/* ---------- the joiner */

/* the host at address (network byte order) and port, the offer's session,
 * the token (32 bytes), and how many files; then each (its place in the
 * list, its number in the offer, the .part path to write, its size and
 * SHA-256); then start. stop waits for the thread, which closes what it
 * wrote first (the guest's stage deletes the files). */
int host_fs_fetch_begin(unsigned int address, int port, unsigned int session, unsigned int token, int count);
int host_fs_fetch_file(int index, int file, unsigned int path, unsigned int size, unsigned int sha256);
int host_fs_fetch_start(void);
/* writes a struct host_fs_fetch_status; returns its state */
int host_fs_fetch_status(unsigned int out);
void host_fs_fetch_stop(void);

#endif
