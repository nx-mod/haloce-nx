/* A piece of an MCC map's conversion, done on the host's threads
 * (port/android/host/host_mcc_convert.c) for the guest's loader (mcc_audio.c,
 * mcc_bitmaps.c). Shared by both sides: 32-bit fields only, guest addresses
 * as numbers, so the guest's ILP32 layout and the host's LP64 one agree. The
 * host reads the input where the guest has it, and writes an output either
 * into the guest's memory (a texture's) or into its own (a sound's, which it
 * then keeps and hands back a piece at a time: host_mcc_audio_read). */
#ifndef MCC_CONVERT_JOB_H
#define MCC_CONVERT_JOB_H
#include <stdint.h>

enum {
    /* a sound permutation: parameters compression, channels, rate, output
    rate and audio mode; the output, IMA ADPCM, kept by the host */
    MCC_CONVERT_AUDIO = 1,
    /* a texture's pixels: parameters as mcc_convert_bitmap's; input and
    output in the guest's memory */
    MCC_CONVERT_BITMAP = 2
};

/* what an audio job does with a permutation it decodes */
enum {
    /* decoded only, to check it: it stays where it is in the map */
    MCC_AUDIO_VALIDATE = 0,
    /* encoded again as IMA ADPCM at the output rate */
    MCC_AUDIO_ENCODE = 1,
    /* checked, then kept as it is (external ADPCM, byte for byte) */
    MCC_AUDIO_COPY = 2
};

/* why a job failed (host_mcc_convert_wait's negative results) */
enum {
    MCC_CONVERT_FAILED_DECODE = -1,
    MCC_CONVERT_FAILED_ENCODE = -2,
    MCC_CONVERT_FAILED_MEMORY = -3,
    MCC_CONVERT_FAILED_REQUEST = -4
};

struct mcc_convert_job {
    uint32_t kind;
    uint32_t input, input_size;
    uint32_t output, output_size;
    uint32_t parameters[12];
};

/* A texture job's description (its parameters[0] is this one's guest
address): the shape it is read in and the one it is packed to, and what is
done between (mcc_bitmap_kernels.h's mcc_bitmap_work, from which this is
made and back). The input is its raw pixels, in the guest's memory, which a
BC7 one is encoded into as DXT5 in place; the output, also the guest's, its
pixels packed for the hardware. */
struct mcc_convert_bitmap {
    uint32_t source_width, source_height, source_depth, source_type, source_format, source_flags, source_mipmaps;
    uint32_t packed_width, packed_height, packed_depth, packed_type, packed_format, packed_flags, packed_mipmaps;
    uint32_t maximum, usage, faces, levels, transcode, convert;
    uint32_t raw_size, decoded_size, target_size, bytes_per_pixel;
    /* the guest's P8 palette (global_vector_palette), 256 entries */
    uint32_t palette;
};

#endif
