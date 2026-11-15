/* MCC packages between two SlipstreamCE machines over a connection of their
 * own (mcc_fast.c): see its header. OpenCE machines never see it. */
#ifndef MCC_FAST_H
#define MCC_FAST_H
#include <stdint.h>

struct mcc_share_host_transfer;

/* ---------- the host (the game's thread) */

/* each frame: whether this machine hosts an MCC game that shares (the
 * listener), and the upload's cap (bytes a second) */
void mcc_fast_host_frame(int hosting, long limit);
/* each frame, for every transfer offered whole: published for the joiner's
 * connection (its machine's address, host byte order); then end_frame drops
 * those not published again (and their connections) */
void mcc_fast_host_publish(struct mcc_share_host_transfer const *transfer, unsigned long address);
void mcc_fast_host_end_frame(void);
/* what a transfer's fast connection has done: 1 when it has one (bytes sent,
 * the files sent whole, when it last sent) */
int mcc_fast_host_progress(long machine, uint32_t session, uint64_t *sent, int *files_done,
    unsigned long *advanced_at);

/* ---------- the joiner (the game's thread) */

enum mcc_fast_client_state {
    MCC_FAST_IDLE,
    MCC_FAST_CONNECTING,     /* connecting, or waiting for the host's hello */
    MCC_FAST_RECEIVING,
    MCC_FAST_DONE,           /* every file received (taken or still to take) */
    MCC_FAST_FAILED_EARLY,   /* nothing received: the host's own way instead */
    MCC_FAST_FAILED_LATE     /* some received, then lost: the download fails */
};

/* the host's address (host byte order), the transfer's session and level, and
 * its files' sizes in its order: 1 begun */
int mcc_fast_client_begin(unsigned long address, uint32_t session, char const *level, int count,
    uint32_t const *sizes);
enum mcc_fast_client_state mcc_fast_client_state(void);
/* the bytes received, in order across the files, at most max: how many */
uint32_t mcc_fast_client_take(void *buffer, uint32_t max);
char const *mcc_fast_client_why(void);
void mcc_fast_client_stop(void);

#endif
