/* An MCC map's conversion on the host's threads, for the guest's loader
 * (port/linux/game/mcc_audio.c, mcc_bitmaps.c), on the Switch, Android and the
 * PS5 alike (tools/switch_build.py and tools/ps5_build.py build this file of
 * Android's host, as they do host_mcc_import.c).
 *
 * Loading an MCC map was its conversion, one piece after another on the
 * game's thread: a campaign map's 87 s on a Switch were 62 s of its sounds
 * (thousands of permutations, each decoded, resampled and encoded again as
 * IMA ADPCM) and 18 s of its textures. Each piece needs nothing but its own
 * bytes, so the guest describes each (struct mcc_convert_job,
 * port/linux/game/mcc_convert_job.h) as it reads them, in the map's order,
 * and the workers here convert them as they come; the guest takes each result
 * in the order it asked, so what it writes into the map's tags is what one
 * thread converting them in turn wrote.
 *
 * The workers: two on the Switch (the game has a core to itself and the
 * process two more, each worker placed on one: host_thread_place_on_helper_core)
 * and three elsewhere; and the game's own thread while it waits for a result
 * (host_mcc_convert_wait runs a waiting piece itself), a third core on the
 * Switch, whose game has nothing else to do while it loads.
 *
 * The codecs are the guest's own (port/linux/game/mcc_audio_kernels.h,
 * mcc_bitmap_kernels.h, mcc_vorbis.c), with no game state, compiled here
 * too. A job reads its input where the guest has it, in the guest's memory
 * (the host's process holds the guest's, low in its own); a texture's output
 * goes there too, where the game draws it from. A sound's stays here: the
 * game only ever copies a piece of it into its sound cache, which it asks
 * for (host_mcc_audio_read), so up to the converter's 1 GB of a map's sounds
 * is the host's memory rather than the guest's heap. */
#include "host.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MCC_CONVERT_HOST 1
#include "../../linux/game/mcc_convert_job.h"
/* (the codecs, with stb_vorbis and bcdec, whose warnings are not ours: the
PS5's host is built with -Werror) */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include "../../linux/game/mcc_audio_kernels.h"
#include "../../linux/game/mcc_bitmap_kernels.h"
#include "../../linux/game/mcc_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(__SWITCH__)
/* (port/switch/host/host_thread.c's: the next of the cores the process may
use besides the game's; this file's host.h is Android's, beside it) */
void host_thread_place_on_helper_core(void);
#define MCC_CONVERT_WORKERS 2
#else
#define MCC_CONVERT_WORKERS 3
#endif
/* jobs at once, queued, running or done and not yet taken (the guest keeps
far fewer in flight: mcc_convert.c) */
#define MCC_CONVERT_JOBS 512
/* a worker's stack: the Vorbis decoder's packet (32 KB) and its own frames */
#define MCC_CONVERT_STACK (1024 * 1024)

enum { JOB_FREE, JOB_QUEUED, JOB_RUNNING, JOB_DONE };

struct convert_job {
    int state;
    /* 1 done, or a negative MCC_CONVERT_FAILED_* */
    int result;
    struct mcc_convert_job request;
    struct mcc_convert_bitmap bitmap;
    /* an audio job's output, the host's */
    unsigned char *output;
    uint32_t output_size;
};

/* a sound tag's converted permutations, one after another (the guest's
slot: the tag's index) */
struct audio_slot {
    unsigned char *bytes;
    uint32_t used, capacity;
};

static pthread_mutex_t convert_lock = PTHREAD_MUTEX_INITIALIZER;
/* a job queued (the workers'), and a job done (a waiter's) */
static pthread_cond_t convert_queued = PTHREAD_COND_INITIALIZER;
static pthread_cond_t convert_done = PTHREAD_COND_INITIALIZER;
static struct convert_job jobs[MCC_CONVERT_JOBS];
static int queue[MCC_CONVERT_JOBS], queue_first, queue_count;
static int workers_started, workers_running, jobs_running;
static struct audio_slot *audio_slots;
static uint32_t audio_slot_count;

/* ---------- the work */

static int convert_audio(struct convert_job *job)
{
    struct mcc_convert_job const *request = &job->request;
    unsigned char const *input = (unsigned char const *)(uintptr_t)request->input;
    int compression = (int)request->parameters[0], channels = (int)request->parameters[1];
    uint32_t rate = request->parameters[2], output_rate = request->parameters[3];
    uint32_t mode = request->parameters[4], frames = 0, size;
    struct mcc_pcm pcm;

    if (!input || !request->input_size || channels < 1 || channels > 2 || !rate || !output_rate)
        return MCC_CONVERT_FAILED_REQUEST;
    if (!mcc_pcm_open(input, request->input_size, compression, channels, rate, &pcm))
    {
        mcc_pcm_close(&pcm);
        return MCC_CONVERT_FAILED_DECODE;
    }
    if (mode == MCC_AUDIO_ENCODE)
    {
        size = mcc_ima_encoded_size(&pcm, channels, output_rate, &frames);
        if (!size)
        {
            mcc_pcm_close(&pcm);
            return MCC_CONVERT_FAILED_ENCODE;
        }
        job->output = malloc(size);
        if (!job->output)
        {
            mcc_pcm_close(&pcm);
            return MCC_CONVERT_FAILED_MEMORY;
        }
        mcc_ima_encode(&pcm, channels, output_rate, frames, job->output, NULL);
        job->output_size = size;
    }
    else if (mode == MCC_AUDIO_COPY)
    {
        job->output = malloc(request->input_size);
        if (!job->output)
        {
            mcc_pcm_close(&pcm);
            return MCC_CONVERT_FAILED_MEMORY;
        }
        memcpy(job->output, input, request->input_size);
        job->output_size = request->input_size;
    }
    mcc_pcm_close(&pcm);
    return 1;
}

static int convert_bitmap(struct convert_job *job)
{
    struct mcc_bitmap_work work;
    unsigned char *decoded = NULL;
    uint32_t *axes;
    int done;

    mcc_bitmap_work_from(&job->bitmap, &work);
    if (!job->request.input || !job->request.output || job->request.input_size < work.raw_size ||
        job->request.output_size < work.target_size)
        return MCC_CONVERT_FAILED_REQUEST;
    axes = malloc(MCC_BITMAP_AXES * sizeof(*axes));
    if (work.convert)
        decoded = malloc(work.decoded_size ? work.decoded_size : 1);
    if (!axes || (work.convert && !decoded))
    {
        free(axes);
        free(decoded);
        return MCC_CONVERT_FAILED_MEMORY;
    }
    done = mcc_bitmap_work_run(&work, (unsigned char *)(uintptr_t)job->request.input, decoded,
        (unsigned char *)(uintptr_t)job->request.output, axes, NULL);
    free(axes);
    free(decoded);
    return done ? 1 : MCC_CONVERT_FAILED_DECODE;
}

static int convert_run(struct convert_job *job)
{
    switch (job->request.kind)
    {
    case MCC_CONVERT_AUDIO:
        return convert_audio(job);
    case MCC_CONVERT_BITMAP:
        return convert_bitmap(job);
    default:
        return MCC_CONVERT_FAILED_REQUEST;
    }
}

/* the next queued job taken and done, the lock held on entry and on return;
0 if none was queued */
static int convert_one_locked(void)
{
    struct convert_job *job;
    int id;

    if (!queue_count)
        return 0;
    id = queue[queue_first];
    queue_first = (queue_first + 1) % MCC_CONVERT_JOBS;
    queue_count--;
    job = &jobs[id];
    job->state = JOB_RUNNING;
    jobs_running++;
    pthread_mutex_unlock(&convert_lock);
    job->result = convert_run(job);
    pthread_mutex_lock(&convert_lock);
    job->state = JOB_DONE;
    jobs_running--;
    pthread_cond_broadcast(&convert_done);
    return 1;
}

static void *convert_worker(void *context)
{
    (void)context;
#if defined(__SWITCH__)
    host_thread_place_on_helper_core();
#endif
    pthread_mutex_lock(&convert_lock);
    for (;;)
    {
        if (!convert_one_locked())
            pthread_cond_wait(&convert_queued, &convert_lock);
    }
    return NULL;
}

/* the workers, started the first time there is work; the lock held */
static void convert_start_locked(void)
{
    int index;

    if (workers_started)
        return;
    workers_started = 1;
    for (index = 0; index < MCC_CONVERT_WORKERS; index++)
    {
        pthread_attr_t attributes;
        pthread_t thread;

        pthread_attr_init(&attributes);
        pthread_attr_setstacksize(&attributes, MCC_CONVERT_STACK);
        pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&thread, &attributes, convert_worker, NULL) == 0)
            workers_running++;
        pthread_attr_destroy(&attributes);
    }
    host_logf(HOST_LOG_INFO, "mcc convert: %d of %d worker threads started", workers_running, MCC_CONVERT_WORKERS);
}

/* ---------- the guest's (port/android/guest/runtime/guest_host.h) */

/* how many threads convert besides the guest's own: 0 where there are none,
and the guest then converts on its own thread (mcc_convert.c) */
int host_mcc_convert_workers(void)
{
    int count;

    pthread_mutex_lock(&convert_lock);
    convert_start_locked();
    count = workers_running;
    pthread_mutex_unlock(&convert_lock);
    return count;
}

/* the job described at the guest's address request queued: its number, or
-EAGAIN where as many as there can be are in hand, -EINVAL for one that
makes no sense */
int host_mcc_convert_submit(unsigned int request)
{
    struct mcc_convert_job const *description = (struct mcc_convert_job const *)(uintptr_t)request;
    int id;

    if (!description || (description->kind != MCC_CONVERT_AUDIO && description->kind != MCC_CONVERT_BITMAP))
        return -EINVAL;
    if (description->kind == MCC_CONVERT_BITMAP && !description->parameters[0])
        return -EINVAL;
    pthread_mutex_lock(&convert_lock);
    convert_start_locked();
    for (id = 0; id < MCC_CONVERT_JOBS && jobs[id].state != JOB_FREE; id++)
    {
    }
    if (id == MCC_CONVERT_JOBS || queue_count == MCC_CONVERT_JOBS)
    {
        pthread_mutex_unlock(&convert_lock);
        return -EAGAIN;
    }
    memset(&jobs[id], 0, sizeof(jobs[id]));
    jobs[id].request = *description;
    if (description->kind == MCC_CONVERT_BITMAP)
        jobs[id].bitmap = *(struct mcc_convert_bitmap const *)(uintptr_t)description->parameters[0];
    jobs[id].state = JOB_QUEUED;
    queue[(queue_first + queue_count) % MCC_CONVERT_JOBS] = id;
    queue_count++;
    pthread_cond_signal(&convert_queued);
    pthread_mutex_unlock(&convert_lock);
    return id;
}

/* 1 if job id is done, a negative MCC_CONVERT_FAILED_* if it failed, 0 if it
is not done after milliseconds - which the calling thread spends converting
whatever is queued, so that the game's own thread works while it waits */
int host_mcc_convert_wait(int id, int milliseconds)
{
    struct timespec now, deadline;
    int result;

    if (id < 0 || id >= MCC_CONVERT_JOBS)
        return MCC_CONVERT_FAILED_REQUEST;
    /* (no time: only whether it is done, nothing converted) */
    if (milliseconds <= 0)
    {
        pthread_mutex_lock(&convert_lock);
        result = jobs[id].state == JOB_DONE ? jobs[id].result :
            jobs[id].state == JOB_FREE ? MCC_CONVERT_FAILED_REQUEST : 0;
        pthread_mutex_unlock(&convert_lock);
        return result;
    }
    clock_gettime(CLOCK_REALTIME, &now);
    deadline = now;
    deadline.tv_sec += milliseconds / 1000;
    deadline.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&convert_lock);
    for (;;)
    {
        if (jobs[id].state == JOB_FREE)
        {
            result = MCC_CONVERT_FAILED_REQUEST;
            break;
        }
        if (jobs[id].state == JOB_DONE)
        {
            result = jobs[id].result;
            break;
        }
        /* (the one waited for first, where it is still queued) */
        if (jobs[id].state == JOB_QUEUED && queue_count && queue[queue_first] != id)
        {
            int at, index;

            for (at = 0; at < queue_count && queue[(queue_first + at) % MCC_CONVERT_JOBS] != id; at++)
            {
            }
            for (index = at; index > 0; index--)
                queue[(queue_first + index) % MCC_CONVERT_JOBS] = queue[(queue_first + index - 1) % MCC_CONVERT_JOBS];
            queue[queue_first] = id;
        }
        if (convert_one_locked())
        {
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
            {
                result = jobs[id].state == JOB_DONE ? jobs[id].result : 0;
                break;
            }
            continue;
        }
        if (pthread_cond_timedwait(&convert_done, &convert_lock, &deadline) == ETIMEDOUT)
        {
            result = jobs[id].state == JOB_DONE ? jobs[id].result : 0;
            break;
        }
    }
    pthread_mutex_unlock(&convert_lock);
    return result;
}

/* job id's output's size: an audio job's, which the host keeps */
unsigned int host_mcc_convert_output_size(int id)
{
    unsigned int size = 0;

    if (id < 0 || id >= MCC_CONVERT_JOBS)
        return 0;
    pthread_mutex_lock(&convert_lock);
    if (jobs[id].state == JOB_DONE)
        size = jobs[id].output_size;
    pthread_mutex_unlock(&convert_lock);
    return size;
}

/* job id let go, once done (one still queued or running is waited for: the
guest only lets go of what it has taken, or of everything when a map is
refused, before it frees the memory the jobs read) */
void host_mcc_convert_release(int id)
{
    unsigned char *output;

    if (id < 0 || id >= MCC_CONVERT_JOBS)
        return;
    pthread_mutex_lock(&convert_lock);
    while (jobs[id].state == JOB_QUEUED || jobs[id].state == JOB_RUNNING)
    {
        if (!convert_one_locked())
            pthread_cond_wait(&convert_done, &convert_lock);
    }
    output = jobs[id].output;
    memset(&jobs[id], 0, sizeof(jobs[id]));
    pthread_mutex_unlock(&convert_lock);
    free(output);
}

/* audio job id's output added to the end of slot's (a sound tag's) converted
samples: where in them it lies, or -ENOMEM; the job's output is the slot's
then */
long long host_mcc_audio_append(int id, unsigned int slot)
{
    struct audio_slot *storage;
    unsigned char *output;
    uint32_t size;
    long long offset;

    if (id < 0 || id >= MCC_CONVERT_JOBS || slot > 0xFFFFu)
        return -EINVAL;
    pthread_mutex_lock(&convert_lock);
    if (jobs[id].state != JOB_DONE || jobs[id].result != 1)
    {
        pthread_mutex_unlock(&convert_lock);
        return -EINVAL;
    }
    output = jobs[id].output;
    size = jobs[id].output_size;
    pthread_mutex_unlock(&convert_lock);
    if (slot >= audio_slot_count)
    {
        struct audio_slot *larger = realloc(audio_slots, (slot + 1) * sizeof(*audio_slots));

        if (!larger)
            return -ENOMEM;
        memset(larger + audio_slot_count, 0, (slot + 1 - audio_slot_count) * sizeof(*larger));
        audio_slots = larger;
        audio_slot_count = slot + 1;
    }
    storage = &audio_slots[slot];
    if (size > UINT32_MAX - storage->used)
        return -ENOMEM;
    if (storage->used + size > storage->capacity)
    {
        uint32_t capacity = storage->capacity ? storage->capacity : 0x10000u;
        unsigned char *larger;

        while (capacity < storage->used + size)
            capacity = capacity > UINT32_MAX / 2 ? storage->used + size : capacity * 2;
        larger = realloc(storage->bytes, capacity);
        if (!larger)
            return -ENOMEM;
        storage->bytes = larger;
        storage->capacity = capacity;
    }
    offset = storage->used;
    if (size)
        memcpy(storage->bytes + storage->used, output, size);
    storage->used += size;
    return offset;
}

/* size bytes of slot's converted samples from offset copied to the guest's
destination: 1, or 0 where they are not all there */
int host_mcc_audio_read(unsigned int slot, unsigned int offset, unsigned int size, unsigned int destination)
{
    struct audio_slot const *storage;

    if (slot >= audio_slot_count || !destination)
        return 0;
    storage = &audio_slots[slot];
    if (offset > storage->used || size > storage->used - offset)
        return 0;
    if (size)
        memcpy((void *)(uintptr_t)destination, storage->bytes + offset, size);
    return 1;
}

/* everything let go: the map is unloaded (every job, done or not, and every
sound's samples) */
void host_mcc_convert_reset(void)
{
    uint32_t index;
    int id;

    pthread_mutex_lock(&convert_lock);
    for (id = 0; id < MCC_CONVERT_JOBS; id++)
    {
        while (jobs[id].state == JOB_QUEUED || jobs[id].state == JOB_RUNNING)
        {
            if (!convert_one_locked())
                pthread_cond_wait(&convert_done, &convert_lock);
        }
        free(jobs[id].output);
        memset(&jobs[id], 0, sizeof(jobs[id]));
    }
    queue_first = queue_count = 0;
    pthread_mutex_unlock(&convert_lock);
    for (index = 0; index < audio_slot_count; index++)
        free(audio_slots[index].bytes);
    free(audio_slots);
    audio_slots = NULL;
    audio_slot_count = 0;
}
