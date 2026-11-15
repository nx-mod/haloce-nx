/* The guest's side of an MCC map's conversion on the host's threads
 * (port/android/host/host_mcc_convert.c; the jobs: mcc_convert_job.h). */
#ifndef MCC_CONVERT_H
#define MCC_CONVERT_H
#include "mcc_convert_job.h"

/* whether the host converts (game.mcc_host_conversion, and a host with
workers); where not, the loader converts on the game's thread as before */
int mcc_convert_available(void);
/* the job queued: its number, or negative where it cannot be */
int mcc_convert_submit(struct mcc_convert_job const *job);
/* whether job is done (or failed), without waiting */
int mcc_convert_ready(int job);
/* job's result once done: 1, or a negative MCC_CONVERT_FAILED_*; the loading
screen and the network are kept going meanwhile, and the game's thread
converts what is queued */
int mcc_convert_finish(int job);
/* an audio job's output's size */
uint32_t mcc_convert_output_size(int job);
/* job let go (waited for, where it is not done) */
void mcc_convert_release(int job);
/* an audio job's output added to slot's (a sound tag's) samples, kept by the
host: where in them it lies, or negative */
long long mcc_convert_audio_append(int job, uint32_t slot);
/* size bytes of slot's samples from offset copied to out: 1, or 0 */
int mcc_convert_audio_read(uint32_t slot, uint32_t offset, uint32_t size, void *out);
/* everything the host holds for the map let go */
void mcc_convert_reset(void);

#endif
