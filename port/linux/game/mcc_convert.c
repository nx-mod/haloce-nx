/* The guest's side of an MCC map's conversion on the host's threads: the
 * host's calls (port/android/host/host_mcc_convert.c) wrapped, with the
 * loading screen and the network kept going while the game's thread waits.
 * The loaders that describe the jobs and take their results in order are
 * mcc_audio.c and mcc_bitmaps.c. */
#include "cseries.h"
#include "mcc_convert.h"
#include "mcc_loading.h"
#include <errno.h>
#include <stdint.h>
#if defined(HALO_ANDROID)
/* (port/android/guest/runtime/guest_host.h's, which the game's sources do not
see; tools/check_host_imports.py checks that list against those) */
int host_mcc_convert_workers(void);
int host_mcc_convert_submit(unsigned int request);
int host_mcc_convert_wait(int id, int milliseconds);
unsigned int host_mcc_convert_output_size(int id);
void host_mcc_convert_release(int id);
long long host_mcc_audio_append(int id, unsigned int slot);
int host_mcc_audio_read(unsigned int slot, unsigned int offset, unsigned int size, unsigned int destination);
void host_mcc_convert_reset(void);
#endif

int config_boolean(char const *name);
void memory_watch_prepare_write(void *address, unsigned long size);

/* how long a wait converts on the game's thread before the loading screen is
drawn again (mcc_loading_pulse draws at most every 50 ms) */
#define MCC_CONVERT_WAIT_MILLISECONDS 20

int mcc_convert_available(void)
{
#if defined(HALO_ANDROID)
    static int workers = -1;
    if (!config_boolean("game.mcc_host_conversion")) return 0;
    if (workers < 0) workers = host_mcc_convert_workers();
    return workers > 0;
#else
    return 0;
#endif
}

int mcc_convert_submit(struct mcc_convert_job const *job)
{
#if defined(HALO_ANDROID)
    return host_mcc_convert_submit((unsigned int)(uintptr_t)job);
#else
    (void)job;
    return -ENOSYS;
#endif
}

int mcc_convert_ready(int job)
{
#if defined(HALO_ANDROID)
    return host_mcc_convert_wait(job, 0) != 0;
#else
    (void)job;
    return 1;
#endif
}

int mcc_convert_finish(int job)
{
#if defined(HALO_ANDROID)
    int result;
    while (!(result = host_mcc_convert_wait(job, MCC_CONVERT_WAIT_MILLISECONDS))) mcc_loading_pulse();
    return result;
#else
    (void)job;
    return MCC_CONVERT_FAILED_REQUEST;
#endif
}

uint32_t mcc_convert_output_size(int job)
{
#if defined(HALO_ANDROID)
    return host_mcc_convert_output_size(job);
#else
    (void)job;
    return 0;
#endif
}

void mcc_convert_release(int job)
{
#if defined(HALO_ANDROID)
    if (job >= 0) host_mcc_convert_release(job);
#else
    (void)job;
#endif
}

long long mcc_convert_audio_append(int job, uint32_t slot)
{
#if defined(HALO_ANDROID)
    return host_mcc_audio_append(job, slot);
#else
    (void)job;
    (void)slot;
    return -ENOSYS;
#endif
}

int mcc_convert_audio_read(uint32_t slot, uint32_t offset, uint32_t size, void *out)
{
#if defined(HALO_ANDROID)
    /* (the sound cache may lie in the window, whose pages the memory watch
    protects: the host's write would otherwise fault) */
    if (size) memory_watch_prepare_write(out, size);
    return host_mcc_audio_read(slot, offset, size, (unsigned int)(uintptr_t)out);
#else
    (void)slot;
    (void)offset;
    (void)size;
    (void)out;
    return 0;
#endif
}

void mcc_convert_reset(void)
{
#if defined(HALO_ANDROID)
    host_mcc_convert_reset();
#endif
}
