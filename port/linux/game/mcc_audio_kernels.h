/* MCC audio's codecs: a permutation's samples decoded (16-bit PCM, IMA ADPCM
 * or Vorbis), resampled and downmixed, and encoded as IMA ADPCM. No game
 * state: compiled into the guest (mcc_audio.c, which converts on the game's
 * thread where the host cannot) and into the hosts (port/android/host/
 * host_mcc_convert.c, which convert on their own threads). malloc and free
 * are whichever the including file has: the game's in the guest (cseries.h),
 * the C library's in the host. */
#ifndef MCC_AUDIO_KERNELS_H
#define MCC_AUDIO_KERNELS_H
#include "mcc_vorbis.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MCC_AUDIO_FRAME_LIMIT 0x1000000u

struct mcc_pcm { short *samples; uint32_t frames, rate; int channels, vorbis_owned; };
struct mcc_ima { int value, step; };

#define MCC_AUDIO_PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
#define MCC_AUDIO_MIN(a, b) ((a) < (b) ? (a) : (b))

/* The IMA ADPCM quantizer tables are part of the public codec format. */
static int const mcc_ima_steps[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,
    80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,
    494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,
    2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,
    8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
    27086,29794,32767
};
static int const mcc_ima_changes[8] = {-1,-1,-1,-1,2,4,6,8};

static int mcc_ima_advance(struct mcc_ima *state, unsigned code)
{
    int quantizer=mcc_ima_steps[state->step];
    int delta=quantizer/8;
    if (code&1) delta+=quantizer/4;
    if (code&2) delta+=quantizer/2;
    if (code&4) delta+=quantizer;
    state->value+=(code&8) ? -delta : delta;
    state->value=MCC_AUDIO_PIN(state->value,-32768,32767);
    state->step=MCC_AUDIO_PIN(state->step+mcc_ima_changes[code&7],0,88);
    return state->value;
}

static unsigned mcc_ima_quantize(struct mcc_ima *state, int sample)
{
    int error=sample-state->value, divisor=mcc_ima_steps[state->step];
    unsigned code=error<0 ? 8 : 0, bit;
    if (error<0) error=-error;
    for (bit=4; bit; bit>>=1,divisor>>=1) {
        if (error>=divisor) {code|=bit; error-=divisor;}
    }
    mcc_ima_advance(state,code);
    return code;
}

static int mcc_pcm_open(unsigned char const *bytes, uint32_t size, int compression,
    int channels, uint32_t rate, struct mcc_pcm *pcm)
{
    uint32_t i;
    memset(pcm,0,sizeof(*pcm));
    pcm->channels=channels; pcm->rate=rate;
    if (compression==3) {
        pcm->vorbis_owned=1;
        return mcc_vorbis_decode(bytes,size,MCC_AUDIO_FRAME_LIMIT,
            &pcm->samples,&pcm->frames,&pcm->rate,&pcm->channels);
    }
    if (compression==0) {
        if (size%(2*channels)) return 0;
        pcm->frames=size/(2*channels);
    } else if (compression==1) {
        if (size%(36*channels)) return 0;
        pcm->frames=size/(36*channels)*64;
    } else return 0;
    if (!pcm->frames || pcm->frames>MCC_AUDIO_FRAME_LIMIT) return 0;
    pcm->samples=malloc(pcm->frames*channels*sizeof(short));
    if (!pcm->samples) return 0;
    if (!compression) {
        for (i=0;i<pcm->frames*(uint32_t)channels;i++)
            pcm->samples[i]=(short)(bytes[i*2]|bytes[i*2+1]<<8);
    } else {
        for (i=0;i<pcm->frames/64;i++) {
            int channel;
            unsigned char const *block=bytes+i*36*channels;
            for (channel=0;channel<channels;channel++) {
                struct mcc_ima state;
                uint32_t frame;
                state.value=(short)(block[channel*4]|block[channel*4+1]<<8);
                state.step=block[channel*4+2];
                if (state.step>88 || block[channel*4+3]) return 0;
                for (frame=0;frame<64;frame++) {
                    unsigned char packed=block[channels*4+(frame/8*channels+channel)*4+frame%8/2];
                    unsigned nibble=(packed>>(frame%2*4))&15;
                    pcm->samples[(i*64+frame)*channels+channel]=(short)mcc_ima_advance(&state,nibble);
                }
            }
        }
    }
    return 1;
}

static void mcc_pcm_close(struct mcc_pcm *pcm)
{
    /* mcc_vorbis uses the C runtime allocator; in the guest, cseries remaps
     * free to debug_free. Each allocation goes back to the allocator that
     * made it. */
    if (pcm->samples) {
        if (pcm->vorbis_owned) mcc_vorbis_free(pcm->samples);
        else free(pcm->samples);
    }
    pcm->samples=NULL;
}

static int mcc_pcm_sample(struct mcc_pcm const *pcm, uint32_t frame, int channel, int channels)
{
    if (pcm->channels==1) return pcm->samples[frame];
    if (channels==1) return ((int)pcm->samples[frame*2]+pcm->samples[frame*2+1])/2;
    return pcm->samples[frame*2+channel];
}

static int mcc_resampled(struct mcc_pcm const *pcm, uint32_t frame, int channel, int channels, uint32_t rate)
{
    uint64_t position=(uint64_t)frame*pcm->rate;
    uint32_t first=(uint32_t)(position/rate), fraction=(uint32_t)(position%rate), next;
    int a,b;
    if (first>=pcm->frames) first=pcm->frames-1;
    next=first+1<pcm->frames ? first+1 : first;
    a=mcc_pcm_sample(pcm,first,channel,channels);
    b=mcc_pcm_sample(pcm,next,channel,channels);
    return a+(int)((int64_t)(b-a)*fraction/rate);
}

/* The bytes of pcm as IMA ADPCM of channels at rate, and its frames; 0 where
 * there are none or too many */
static uint32_t mcc_ima_encoded_size(struct mcc_pcm const *pcm, int channels, uint32_t rate, uint32_t *frames)
{
    uint64_t frames64=((uint64_t)pcm->frames*rate+pcm->rate-1)/pcm->rate;
    if (!frames64 || frames64>MCC_AUDIO_FRAME_LIMIT) return 0;
    *frames=(uint32_t)frames64;
    return (*frames+63)/64*36*(uint32_t)channels;
}

/* pcm as IMA ADPCM into out (mcc_ima_encoded_size's bytes); pulse, where
 * given, is called every 256 blocks */
static void mcc_ima_encode(struct mcc_pcm const *pcm, int channels, uint32_t rate, uint32_t frames,
    unsigned char *output, void (*pulse)(void))
{
    uint32_t blocks=(frames+63)/64,block;
    struct mcc_ima states[2]={{0,0},{0,0}};
    for (block=0;block<blocks;block++) {
        unsigned char *out=output+block*36*channels;
        int channel;
        if (pulse && !(block & 255u)) pulse();
        for (channel=0;channel<channels;channel++) {
            uint32_t pair;
            out[channel*4]=(unsigned char)states[channel].value;
            out[channel*4+1]=(unsigned char)(states[channel].value>>8);
            out[channel*4+2]=(unsigned char)states[channel].step;
            out[channel*4+3]=0;
            for (pair=0;pair<32;pair++) {
                uint32_t f=block*64+pair*2;
                unsigned lo=mcc_ima_quantize(&states[channel],
                    mcc_resampled(pcm,MCC_AUDIO_MIN(f,frames-1),channel,channels,rate));
                unsigned hi=mcc_ima_quantize(&states[channel],
                    mcc_resampled(pcm,MCC_AUDIO_MIN(f+1,frames-1),channel,channels,rate));
                out[channels*4+(pair/4*channels+channel)*4+pair%4]=(unsigned char)(lo|(hi<<4));
            }
        }
    }
}

#endif
