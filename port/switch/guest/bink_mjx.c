/*
BINK_MJX.C

The Bink video SDK entry points bink_playback.c uses, answered by the ".mjx"
reader in mjx.c. Replaces bink_null.c, which reports every movie as missing.

The game asks for d:\bink\<name>.bik. There is no Bink decoder to answer -
the RAD SDK is proprietary and the reconstructed libs/binkxbox is missing the
decode math itself - so tools/mjx_pack.py converts each movie once, on a
desktop, into d:\bink\<name>.mjx: the same picture and the same soundtrack, but
as whole baseline JPEG frames and plain PCM. BinkOpen looks for that name, and
anything still missing is skipped exactly as a missing movie is.

The frame rate, the frame count and the dimensions come from the container
rather than from the SDK's own bookkeeping, so the pacing here is the only
thing standing between a movie and playing as fast as the CPU allows. BinkWait
is what enforces it, and the game may call it in a tight spin
(bink_playback.c:1025, `while (BinkWait(bink)) { }`), so it has to return 0
within a frame time no matter what. It returns 0 once the picture's deadline
has passed and 1 before that, which is the whole contract.

The prototypes match the declarations in bink_playback.c; the RAD SDK's
RADEXPLINK is __stdcall. bink_playback.c does not include a Bink header - the
vendored bink.h is not in the tree - so it declares what it uses itself, and
so does this file. Any change to one has to be made to the other.
*/

#include "platform.h"

#include "mjx.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void *(__stdcall *rad_memory_allocate_proc)(unsigned long size);
typedef void (__stdcall *rad_memory_free_proc)(void *memory);
typedef void *(__stdcall *bink_sound_system_open_proc)(unsigned long param);
typedef struct BINK *HBINK;

/* bink_playback.c:541 passes its IDirectSound * through BinkSetSoundSystem as
   an unsigned long; only ever used as an opaque token here. */
struct IDirectSound;

/* bink_playback.c's view of the handle. These five members are read by name
   and by offset (bink_playback.c:207), so they stay exactly here; everything
   after them is ours. */
typedef struct BINK
{
	unsigned long Width;
	unsigned long Height;
	unsigned long Frames;
	unsigned long FrameNum;
	unsigned long LastFrameNum;

	/* everything below is private to this file */
	struct mjx_movie movie;
	const char *failure;        /* why the last frame would not decode */
	unsigned long long due;     /* monotonic nanoseconds the next frame is due */
	unsigned long long period;  /* nanoseconds one frame lasts */
	struct IDirectSound *dsound;  /* the game's sound system, when it gave us one */
} BINK;

/* bink_playback.c:216, and the sizes it asserts at :323-333. The SDK's fields
   are 32 bits wide even though unsigned long is not on this target, which is
   why these are not "unsigned long" the way bink_playback.c spells them: the
   assert is the ABI, and writing 8 byte fields into a 4 byte layout would
   corrupt the frame counters the debug overlay reads. */
typedef struct BINKSUMMARY
{
	unsigned int Width;
	unsigned int Height;
	unsigned int TotalTime;
	unsigned int FileFrameRate;
	unsigned int FileFrameRateDiv;
	unsigned int FrameRate;
	unsigned int FrameRateDiv;
	unsigned int TotalOpenTime;
	unsigned int TotalFrames;
	unsigned int TotalPlayedFrames;
	unsigned int SkippedFrames;
	unsigned int SkippedBlits;
	unsigned int SoundSkips;
	unsigned int TotalBlitTime;
	unsigned int TotalReadTime;
	unsigned int TotalVideoDecompTime;
	unsigned int TotalAudioDecompTime;
	unsigned int TotalIdleReadTime;
	unsigned int TotalBackReadTime;
	unsigned int TotalReadSpeed;
	unsigned int SlowestFrameTime;
	unsigned int Slowest2FrameTime;
	unsigned int SlowestFrameNum;
	unsigned int Slowest2FrameNum;
	unsigned int AverageDataRate;
	unsigned int AverageFrameSize;
	unsigned int HighestMemAmount;
	unsigned int TotalIOMemory;
	unsigned int HighestIOUsed;
	unsigned int Highest1SecRate;
	unsigned int Highest1SecFrame;
} BINKSUMMARY;

typedef struct BINKREALTIME
{
	unsigned int FrameNum;
	unsigned int FrameRate;
	unsigned int FrameRateDiv;
	unsigned int Frames;
	unsigned int FramesTime;
	unsigned int FramesVideoDecompTime;
	unsigned int FramesAudioDecompTime;
	unsigned int FramesReadTime;
	unsigned int FramesIdleReadTime;
	unsigned int FramesThreadReadTime;
	unsigned int FramesBlitTime;
	unsigned int ReadBufferSize;
	unsigned int ReadBufferUsed;
	unsigned int FramesDataRate;
} BINKREALTIME;

typedef char bink_summary_size_assert[
	sizeof(BINKSUMMARY) == 0x7C ? 1 : -1];
typedef char bink_summary_skipped_frames_offset_assert[
	offsetof(BINKSUMMARY, SkippedFrames) == 0x28 ? 1 : -1];
typedef char bink_summary_skipped_blits_offset_assert[
	offsetof(BINKSUMMARY, SkippedBlits) == 0x2C ? 1 : -1];
typedef char bink_realtime_size_assert[
	sizeof(BINKREALTIME) == 0x38 ? 1 : -1];
typedef char bink_realtime_frames_offset_assert[
	offsetof(BINKREALTIME, Frames) == 0x0C ? 1 : -1];

/* The game's texture is D3DFMT_LIN_X8R8G8B8 (bink_playback.c:163), which is
   four bytes a pixel and little endian, so a pixel's bytes read B, G, R,
   then the unused byte. port/vita/host/vita_movie.c writes its pixels the
   same way, and matching it keeps the two ports looking alike. */
#define BINK_PIXEL_BYTES 4

/* The movie currently playing, so halo_movie_display_aspect can answer without
   a handle - the port asks for it before it has one. */
static BINK *bink_current;

static unsigned long long bink_now_ns(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;
	return (unsigned long long)now.tv_sec * 1000000000ull + (unsigned long long)now.tv_nsec;
}


static unsigned char bink_clamp(int value)
{
	if (value < 0)
		return 0;
	if (value > 255)
		return 255;
	return (unsigned char)value;
}

/* BT.601 full range, 8.8 fixed point. Each colour offset is hoisted out of the
   two pixels that share a chroma sample, so a 2x2 block costs three matrix
   rows and two luma multiplies rather than three per pixel.

   Full range, not the limited range port/vita/host/vita_movie.c uses, and the
   difference is not cosmetic: Bink's video is limited, but JFIF's YCbCr has no
   range flag, so tools/mjx_pack.py's mjpeg encode has already stretched it by
   255/219 on the way in (a luma of 193 is stored as 207). Reading that as
   limited would stretch it a second time and wash the picture out. The
   limited-range coefficients (298/100/208/516 with Y-16) are the ones to use
   only for a source that is still limited, which by this point nothing is. */
static void bink_store_pixel(unsigned char *at, int luma, int red, int green, int blue)
{
	unsigned int pixel = 0xff000000u
		| ((unsigned int)bink_clamp((luma + red) >> 8) << 16)
		| ((unsigned int)bink_clamp((luma + green) >> 8) << 8)
		| (unsigned int)bink_clamp((luma + blue) >> 8);

	memcpy(at, &pixel, BINK_PIXEL_BYTES);
}

/* ---------- audio

   bink_playback.c:541 hands over the game's IDirectSound once, before any movie
   is open, and never takes it back - the RAD SDK's sound system is a
   process-wide object for the same reason, and it is the only thing that ever
   sees what BinkOpenDirectSound returns. So there is one here, and BinkOpen
   points it at whichever movie is playing.

   The mixer in dsound_sdl.c owns the pacing: it drains its queue at exactly the
   format's sample rate and never corrects, so a stream that runs dry simply goes
   silent until the next packet arrives. What has to be right, therefore, is how
   many sample frames are handed over per picture frame - too many and the
   soundtrack runs ahead of the picture, too few and it falls behind, and
   "queue the next packet" gets neither right on its own. Each picture frame's
   share is worked out from the same rational rate the pictures use, as a running
   total, so the rounding never accumulates. */

#define MJX_AUDIO_RATE 44100
#define MJX_AUDIO_CHANNELS 2
#define BINK_AUDIO_PACKETS 16   /* the mixer queues 64; stay well inside it */
#define BINK_AUDIO_PREROLL 6    /* queued at BinkOpen, to cover output latency */

struct bink_audio
{
	struct IDirectSoundStream *stream;
	WAVEFORMATEX format;

	/* stream_process copies the XMEDIAPACKET and only decodes pvBuffer later,
	   on the mixer's own thread (dsound_sdl.c:988), so a packet's samples have
	   to stay put and stay unmodified until the mixer has finished with them.
	   That rules out one shared staging buffer: every packet needs its own,
	   reusable once its status has left PENDING. */
	struct
	{
		unsigned char *samples;
		unsigned long capacity;
		XMEDIAPACKET packet;
		unsigned long status;     /* XMEDIAPACKET_STATUS_*, written by the mixer */
		unsigned long completed;
	} packet[BINK_AUDIO_PACKETS];
	unsigned int head, count;

	struct mjx_movie *movie;     /* the PCM source, NULL while nothing is playing */
	unsigned long pushed;        /* sample frames taken from the movie so far */
	unsigned long accounted;     /* picture frames whose audio has been accounted for */
	unsigned long skipped;       /* sample frames the mixer was too far behind to take */
	unsigned long long decomp_ns;/* wall time spent reading audio, for the overlay */
	unsigned long long decomp_worst;
	int complained;
};

static struct bink_audio bink_audio;

/* The mixer completes packets on its own thread (dsound_sdl.c:875 writes
   *pdwStatus), so a slot only becomes ours again once the status leaves PENDING. */
static void bink_audio_reap(struct bink_audio *audio)
{
	while (audio->count > 0 && audio->packet[audio->head].status != XMEDIAPACKET_STATUS_PENDING)
	{
		audio->head = (audio->head + 1) % BINK_AUDIO_PACKETS;
		audio->count--;
	}
}

/* Queues one picture frame's share of the soundtrack. Separate from the pacing
   loop below because a picture frame is also the natural unit of work: the
   mixer's latency is a couple of packets, so anything coarser than this turns
   queue depth into latency. */
static void bink_audio_push_frame(struct bink_audio *audio, struct mjx_movie *movie,
	unsigned long block)
{
	unsigned long long target, wanted, started = bink_now_ns();
	unsigned long frames, bytes, got;
	unsigned int slot;

	audio->accounted++;
	if (audio->accounted > movie->frame_count)
	{
		audio->accounted = movie->frame_count;
		return;                     /* the picture outlives the soundtrack */
	}

	/* What the first `accounted` picture frames are worth, as one running
	   total rather than a sum of per frame amounts: 44100 * 100 / 2997 is
	   1471.47 sample frames per picture frame, and truncating that every time
	   would throw away a twentieth of a sample each time - 12 ms of silence
	   over the movie. */
	target = (unsigned long long)audio->accounted * MJX_AUDIO_RATE * movie->fps_denominator
		/ movie->fps_numerator;
	wanted = target > audio->pushed ? target - audio->pushed : 0;
	if (wanted == 0)
		return;

	/* A quarter second in a single packet is far more than any frame needs;
	   past that the container's rate is wrong and queueing it would only make
	   the sound worse. Drop the surplus and say so, once. */
	frames = (unsigned long)wanted;
	if (frames > MJX_AUDIO_RATE / 4)
	{
		audio->skipped += frames - MJX_AUDIO_RATE / 4;
		frames = MJX_AUDIO_RATE / 4;
		if (!audio->complained)
		{
			audio->complained = 1;
			platform_log("bink: %s wants %llu sample frames for picture %lu; that soundtrack cannot be that long",
				movie->name ? movie->name : "?", wanted, audio->accounted);
		}
	}
	bytes = frames * block;

	bink_audio_reap(audio);
	if (audio->count == BINK_AUDIO_PACKETS)
	{
		/* The mixer is behind. Drop this frame's audio rather than queue it
		   late: a gap is honest, a backlog plays against the picture. */
		audio->skipped += frames;
		audio->pushed = target;
		return;
	}

	slot = (audio->head + audio->count) % BINK_AUDIO_PACKETS;
	if (audio->packet[slot].capacity < bytes)
	{
		unsigned char *grown = (unsigned char *)realloc(audio->packet[slot].samples, bytes);

		if (!grown)
		{
			audio->skipped += frames;
			audio->pushed = target;
			return;
		}
		audio->packet[slot].samples = grown;
		audio->packet[slot].capacity = bytes;
	}

	/* Straight from the container: the soundtrack is already the PCM the
	   mixer wants, so there is no decode and no conversion to be wrong. */
	got = mjx_read_audio(movie, audio->pushed * block, audio->packet[slot].samples, bytes);
	if (got < bytes)
		memset(audio->packet[slot].samples + got, 0, bytes - got);

	audio->packet[slot].status = XMEDIAPACKET_STATUS_PENDING;
	audio->packet[slot].completed = 0;
	audio->packet[slot].packet.pvBuffer = audio->packet[slot].samples;
	audio->packet[slot].packet.dwMaxSize = bytes;
	audio->packet[slot].packet.pdwCompletedSize = &audio->packet[slot].completed;
	audio->packet[slot].packet.pdwStatus = &audio->packet[slot].status;
	audio->packet[slot].packet.pContext = NULL;   /* also hCompletionEvent: one union */
	audio->packet[slot].packet.prtTimestamp = NULL;

	if (IDirectSoundStream_Process(audio->stream, &audio->packet[slot].packet, NULL) != 0)
		audio->skipped += frames;      /* dsound_sdl.c:1000, its own queue is full */
	else
		audio->count++;

	audio->pushed = target;
	{
		unsigned long long spent = bink_now_ns() - started;

		audio->decomp_ns += spent;
		if (spent > audio->decomp_worst)
			audio->decomp_worst = spent;
	}
}

/* The soundtrack for video_frames more picture frames, one packet each. */
static void bink_audio_pump(struct bink_audio *audio, unsigned long video_frames)
{
	struct mjx_movie *movie = audio->movie;

	if (!audio->stream || !movie || !movie->audio_size)
		return;
	if (!movie->fps_numerator)
		return;
	while (video_frames-- > 0)
		bink_audio_push_frame(audio, movie, 2u * MJX_AUDIO_CHANNELS);
}

/* Points the sound stream at a movie and queues the first few pictures'
   soundtrack, so the mixer has something to play before the first picture is on
   screen. The preroll is not a delay: it is queue depth, and steady state keeps
   it. */
static void bink_audio_attach(struct mjx_movie *movie)
{
	if (!bink_audio.stream || !movie)
		return;
	if (movie->audio_size == 0)
		return;
	if (movie->audio_rate != MJX_AUDIO_RATE || movie->audio_channels != MJX_AUDIO_CHANNELS)
	{
		platform_log("bink: %s carries %lu Hz %u channel audio, the stream is %d Hz %d channel; playing it silent",
			movie->name ? movie->name : "?", movie->audio_rate, movie->audio_channels,
			MJX_AUDIO_RATE, MJX_AUDIO_CHANNELS);
		return;
	}

	bink_audio.movie = movie;
	bink_audio.pushed = 0;
	bink_audio.accounted = 0;
	bink_audio.skipped = 0;
	bink_audio.complained = 0;
	bink_audio.decomp_ns = 0;
	bink_audio.decomp_worst = 0;
	bink_audio_pump(&bink_audio, BINK_AUDIO_PREROLL);
}

/* Stops the soundtrack. Flush completes every packet the mixer still holds -
   synchronously, before it returns, which is what makes freeing the buffers
   below safe - so nothing is left playing over the next movie. */
static void bink_audio_detach(void)
{
	unsigned int i;

	bink_audio.movie = NULL;
	bink_audio.pushed = 0;
	bink_audio.accounted = 0;
	if (bink_audio.stream)
		IDirectSoundStream_Flush(bink_audio.stream);
	for (i = 0; i < BINK_AUDIO_PACKETS; i++)
	{
		free(bink_audio.packet[i].samples);
		bink_audio.packet[i].samples = NULL;
		bink_audio.packet[i].capacity = 0;
	}
	bink_audio.head = 0;
	bink_audio.count = 0;
}


/* ---------- the SDK entry points */

void __stdcall RADSetMemory(rad_memory_allocate_proc allocate, rad_memory_free_proc release)
{
	/* The SDK lets the game supply the allocator. Nothing here allocates
	   through the SDK - the pictures' planes and libjpeg's own state come
	   from malloc - so there is nothing to record. */
	(void)allocate;
	(void)release;
}

/* bink_playback.c:541-543 passes this to BinkSetSoundSystem along with its
   dsound_get(), and treats a zero return as "no DirectSound for bink" and
   raises an error. The real SDK would hand back its own sound system object
   and call it through a vtable; nothing outside this file ever sees that
   object, so what matters is only that a token comes back non-null and that
   BinkSetSoundSystem reports success. */
void *__stdcall BinkOpenDirectSound(unsigned long param)
{
	struct IDirectSound *dsound = (struct IDirectSound *)param;
	DSSTREAMDESC description;

	if (!dsound)
		return NULL;
	/* The game only ever does this once, but stay idempotent rather than
	   orphaning the first stream. */
	if (bink_audio.stream)
		return &bink_audio;

	memset(&bink_audio, 0, sizeof(bink_audio));
	bink_audio.format.wFormatTag = WAVE_FORMAT_PCM;
	bink_audio.format.nChannels = MJX_AUDIO_CHANNELS;
	bink_audio.format.nSamplesPerSec = MJX_AUDIO_RATE;
	bink_audio.format.wBitsPerSample = 16;
	bink_audio.format.nBlockAlign = MJX_AUDIO_CHANNELS * 2;
	bink_audio.format.nAvgBytesPerSec = MJX_AUDIO_RATE * MJX_AUDIO_CHANNELS * 2;
	bink_audio.format.cbSize = 0;

	memset(&description, 0, sizeof(description));
	description.dwFlags = 0;                 /* 2D, so no headroom is spent on 3D */
	description.dwMaxAttachedPackets = 1;     /* one outstanding packet is plenty */
	description.lpwfxFormat = &bink_audio.format;
	/* Stereo to the front pair, which is what the mixer gives a voice when
	   the mask is unset anyway; set so the real thing agrees. */
	description.dwMixBinMask = DSMIXBIN_FRONT_LEFT | DSMIXBIN_FRONT_RIGHT;

	if (IDirectSound_CreateSoundStream(dsound, &description, &bink_audio.stream, NULL) != 0
		|| !bink_audio.stream)
	{
		platform_log("bink: DirectSound would not give us a sound stream");
		bink_audio.stream = NULL;
		return NULL;
	}
	return &bink_audio;
}

long __stdcall BinkSetSoundSystem(bink_sound_system_open_proc open, unsigned long param)
{
	if (!open)
		return 0;
	return open(param) != NULL ? 1 : 0;
}

void __stdcall BinkSetIOSize(unsigned long io_size)
{
	/* The SDK's read-ahead buffer. mjx.c reads with stdio on demand, so the
	   size the game asks for has nothing to size. */
	(void)io_size;
}

static int bink_path_for(const char *name, char *path, unsigned long path_size)
{
	const char *base, *dot;
	size_t length;

	if (!name || !name[0])
		return 0;
	/* d:\bink\intro.bik (or introfr.bik) -> d:\bink\intro.mjx. Only the
	   extension changes: the directory is the game's, so whatever the port
	   has configured as the disc root is where the movies belong. */
	base = strrchr(name, '\\');
	base = base ? base + 1 : name;
	dot = strrchr(base, '.');
	length = dot ? (size_t)(dot - base) : strlen(base);
	if (length == 0)
		return 0;
	/* Rebuild the xbox path with the new extension, keeping the drive letter
	   and the directories the game asked for, so the port's own path rules
	   decide where it lands. */
	{
		size_t head = (size_t)(base - name);

		if (head + length + 5 > path_size)
			return 0;
		memcpy(path, name, head);
		snprintf(path + head, path_size - head, "%.*s.mjx", (int)length, base);
	}
	return 1;
}

HBINK __stdcall BinkOpen(const char *name, unsigned long flags)
{
	BINK *bink;
	char xbox_path[512];
	char host_path[1024];
	const char *error = NULL;

	(void)flags;
	if (!bink_path_for(name, xbox_path, sizeof(xbox_path)))
		return NULL;
	platform_translate_path(xbox_path, host_path, sizeof(host_path));

	bink = (BINK *)calloc(1, sizeof(*bink));
	if (!bink)
		return NULL;
	if (mjx_open(&bink->movie, host_path, &error) == 0)
	{
		platform_log("bink: cannot open %s: %s", host_path, error ? error : "?");
		free(bink);
		return NULL;
	}

	bink->Width = bink->movie.width;
	bink->Height = bink->movie.height;
	bink->Frames = bink->movie.frame_count;
	bink->FrameNum = 0;
	bink->LastFrameNum = 0;
	/* 2997/100 is a rate of 29.97, not of 30: the period has to come out of
	   the fraction, or the movie plays 0.1% fast and the audio drifts. */
	bink->period = bink->movie.fps_numerator ?
		1000000000ull * (unsigned long long)bink->movie.fps_denominator /
		(unsigned long long)bink->movie.fps_numerator : 0;
	bink->due = bink_now_ns();
	bink_current = bink;
	/* Queues the first few pictures' soundtrack before the game can ask for
	   the first picture, so the mixer is never waiting on us for data. */
	bink_audio_attach(&bink->movie);
	platform_log("bink: %s, %lux%lu, %lu frames at %lu/%lu, %s%lu sample frames of audio",
		host_path, bink->Width, bink->Height, bink->Frames,
		bink->movie.fps_numerator, bink->movie.fps_denominator,
		bink->movie.audio_size ? "" : "no ", bink->movie.audio_frames);
	return bink;
}

/* The port asks for the movie's aspect so it can letterbox rather than
   stretch. bink_null.c answers 0.0 to mean "no movie", and so does this once
   there is none; while one is open the picture's own proportions are the
   honest answer, which for the disc's movies is 4:3. */
float halo_movie_display_aspect(void)
{
	if (!bink_current || bink_current->Height == 0)
		return 0.0f;
	return (float)bink_current->Width / (float)bink_current->Height;
}

void __stdcall BinkClose(HBINK bink)
{
	if (!bink)
		return;
	if (bink_current == bink)
		bink_current = NULL;
	/* Before mjx_close: detach reads nothing further from the movie, and it
	   has to stop the mixer first so no packet outlives the container. */
	bink_audio_detach();
	mjx_close(&bink->movie);
	free(bink);
}

/* 0 when the picture is due, 1 when it is not yet. The game spins on this
   (bink_playback.c:1025), so it must always terminate: the deadline advances
   by exactly one period each call, and a clock that has gone backwards or a
   period of zero cannot wedge it. */
long __stdcall BinkWait(HBINK bink)
{
	unsigned long long now;

	if (!bink)
		return 0;
	/* At the end of the movie, never wait: bink_playback.c:1046 stops the
	   playback once FrameNum reaches Frames-1, and that check is in the same
	   update as this call, so a wait here would hang the game instead. */
	if (bink->Frames == 0 || bink->FrameNum + 1 >= bink->Frames)
		return 0;
	if (bink->period == 0)
		return 0;

	now = bink_now_ns();
	if (now >= bink->due)
	{
		unsigned long long next = bink->due + bink->period;

		/* A frame that took longer than its period - the first one always
		   does - must not try to catch up by decoding a burst of them. */
		bink->due = next > now + bink->period ? now + bink->period : next;
		return 0;
	}

	/* Not due yet. The real SDK blocks here on a timer; the game cannot,
	   because it spins (bink_playback.c:1025), so without this the movie
	   costs a core doing nothing for 30 ms out of every 33. Sleeping the gap
	   away instead is what the SDK's own wait would do, and it is safe
	   because decoding only needs about a sixth of the period. Capped so a
	   deadline that has moved cannot turn into one long oversleep. */
	{
		unsigned long long remaining = bink->due - now;
		struct timespec pause;

		if (remaining > 2000000ull)
			remaining = 2000000ull;
		pause.tv_sec = 0;
		pause.tv_nsec = (long)remaining;
		nanosleep(&pause, NULL);
	}
	return 1;
}

long __stdcall BinkDoFrame(HBINK bink)
{
	const char *error = NULL;

	if (!bink)
		return 0;
	if (mjx_decode_frame(&bink->movie, bink->FrameNum, &error) == 0)
	{
		/* Hold the last good picture rather than tearing the texture: the
		   game copies whatever is in the planes, so leaving them alone is
		   what keeps a dropped frame invisible. */
		if (bink->failure == NULL)
			bink->failure = error;
		platform_log("bink: frame %lu of %s did not decode: %s", bink->FrameNum,
			bink->movie.name ? bink->movie.name : "?", error ? error : "?");
		return 0;
	}
	bink->failure = NULL;
	/* One picture frame's worth of soundtrack, queued as the picture it
	   belongs to becomes current. The mixer is a few packets behind by now
	   (see BINK_AUDIO_PREROLL), so this keeps a queue topped up rather than
	   scheduling anything. */
	if (bink->FrameNum < bink->Frames)
		bink_audio_pump(&bink_audio, 1);
	return 0;
}

void __stdcall BinkNextFrame(HBINK bink)
{
	if (!bink || bink->Frames == 0)
		return;
	/* bink_playback.c:1000 reads FrameNum after this, and stops the movie
	   when it reaches Frames-1, so the last frame is played and then the
	   count is left there rather than run past it. */
	if (bink->FrameNum + 1 < bink->Frames)
	{
		bink->LastFrameNum = bink->FrameNum;
		bink->FrameNum++;
	}
}

long __stdcall BinkCopyToBuffer(HBINK bink, void *destination, long destination_pitch,
	unsigned long destination_height, unsigned long destination_x, unsigned long destination_y,
	unsigned long flags)
{
	const struct mjx_frame *frame;
	const struct mjx_component *luma, *chroma_blue, *chroma_red;
	unsigned long row, width, height;

	(void)flags;
	if (!bink || !destination || bink->movie.frame.component_count < 3)
		return 0;

	frame = &bink->movie.frame;
	luma = &frame->components[0];
	chroma_blue = &frame->components[1];
	chroma_red = &frame->components[2];
	if (destination_pitch <= 0)
		return 0;

	width = (unsigned long)luma->width < bink->Width ? (unsigned long)luma->width : bink->Width;
	height = (unsigned long)luma->height < bink->Height ? (unsigned long)luma->height : bink->Height;
	if (destination_height < height)
		height = destination_height;

	for (row = 0; row < height; row++)
	{
		const unsigned char *y_row = luma->samples + row * luma->stride;
		const unsigned char *u_row = chroma_blue->samples + (row >> 1) * chroma_blue->stride;
		const unsigned char *v_row = chroma_red->samples + (row >> 1) * chroma_red->stride;
		unsigned char *out = (unsigned char *)destination +
			(destination_y + row) * destination_pitch +
			destination_x * BINK_PIXEL_BYTES;
		unsigned long x = 0;

		for (; x + 1 < width; x += 2)
		{
			int u = u_row[x >> 1] - 128;
			int v = v_row[x >> 1] - 128;
			int red = 359 * v + 128;
			int green = -88 * u - 183 * v + 128;
			int blue = 454 * u + 128;
			int first = y_row[x] << 8;
			int second = y_row[x + 1] << 8;

			bink_store_pixel(out + x * BINK_PIXEL_BYTES, first, red, green, blue);
			bink_store_pixel(out + (x + 1) * BINK_PIXEL_BYTES, second, red, green, blue);
		}
		if (x < width)
		{
			/* Odd widths cannot happen in 4:2:0, but a pixel left over is a
			   pixel the game would read as whatever was in the buffer. */
			int u = u_row[x >> 1] - 128;
			int v = v_row[x >> 1] - 128;

			bink_store_pixel(out + x * BINK_PIXEL_BYTES, y_row[x] << 8,
				359 * v + 128, -88 * u - 183 * v + 128, 454 * u + 128);
		}
	}
	/* Nothing is written past width: bink_playback.c:588 fills the buffer
	   with rand() and leaves the row padding as it found it, so a whole
	   extra row here would show up as noise on the texture's edge. */
	return 0;
}

void __stdcall BinkGetSummary(HBINK bink, void *summary)
{
	BINKSUMMARY *out = (BINKSUMMARY *)summary;

	if (!out)
		return;
	memset(out, 0, sizeof(*out));
	if (!bink)
		return;
	out->Width = (unsigned int)bink->Width;
	out->Height = (unsigned int)bink->Height;
	out->TotalFrames = (unsigned int)bink->Frames;
	out->TotalPlayedFrames = (unsigned int)bink->FrameNum;
	out->FileFrameRate = (unsigned int)bink->movie.fps_numerator;
	out->FileFrameRateDiv = (unsigned int)bink->movie.fps_denominator;
	out->FrameRate = out->FileFrameRate;
	out->FrameRateDiv = out->FileFrameRateDiv;
	out->TotalTime = (unsigned int)(bink->FrameNum * bink->period / 1000000ull);
	/* The SDK reports these in milliseconds. There is no audio decode to time -
	   the soundtrack is already PCM - so the only honest figure is the time
	   spent pulling it out of the container, and SoundSkips is the sample
	   frames the mixer was too far behind to accept. */
	out->TotalAudioDecompTime = (unsigned int)(bink_audio.decomp_ns / 1000000ull);
	out->SoundSkips = (unsigned int)bink_audio.skipped;
}

void __stdcall BinkGetRealtime(HBINK bink, void *realtime, unsigned long frame_count)
{
	BINKREALTIME *out = (BINKREALTIME *)realtime;

	(void)frame_count;
	if (!out)
		return;
	memset(out, 0, sizeof(*out));
	if (!bink)
		return;
	out->FrameNum = (unsigned int)bink->FrameNum;
	out->Frames = (unsigned int)bink->Frames;
	out->FrameRate = (unsigned int)bink->movie.fps_numerator;
	out->FrameRateDiv = (unsigned int)bink->movie.fps_denominator;
	out->ReadBufferSize = (unsigned int)bink->movie.scratch_size;
}