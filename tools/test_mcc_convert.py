"""The host's MCC conversion (port/android/host/host_mcc_convert.c), built natively.

Many jobs are queued, converted on the workers and on the waiting thread, and
taken in the order they were asked for; each result must be the bytes the
codecs give converting the same input in turn on one thread (the guest's
fallback, mcc_audio.c and mcc_bitmaps.c, calls the same codecs). Guest
addresses are 32-bit, so the inputs are mapped below 4 GB.
"""
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]

DRIVER = r'''
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "host_mcc_convert.c"

void host_logf(int priority, const char *format, ...) {(void)priority;(void)format;}

static void *low(size_t size)
{
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p == MAP_FAILED || (uintptr_t)p + size > 0x100000000ULL) {printf("no low memory\n"); exit(2);}
    return p;
}

static uint32_t seed = 12345;
static uint32_t next(void) {seed = seed * 1103515245u + 12345u; return seed >> 8;}

#define SOUNDS 300
#define TEXTURES 120

int main(void)
{
    struct mcc_convert_job *requests;
    static unsigned char *inputs[SOUNDS];
    static uint32_t sizes[SOUNDS];
    static int ids[SOUNDS];
    uint32_t used[4] = {0, 0, 0, 0};
    int i, failures = 0, textures_done = 0;

    if (host_mcc_convert_workers() <= 0) {printf("no workers\n"); return 2;}
    /* (the guest's structures are below 4 GB, as its addresses say) */
    requests = low(SOUNDS * sizeof(*requests));
    /* sounds: 16-bit PCM, mono or stereo, 22050 or 44100, some encoded and
    some copied; four slots, each its permutations one after another */
    for (i = 0; i < SOUNDS; i++) {
        int channels = 1 + (i & 1);
        uint32_t frames = 64 + next() % 20000, f;
        int16_t *pcm;
        sizes[i] = frames * channels * 2;
        inputs[i] = low(sizes[i]);
        pcm = (int16_t *)inputs[i];
        for (f = 0; f < frames * channels; f++) pcm[f] = (int16_t)((int)(next() % 20000) - 10000);
        memset(&requests[i], 0, sizeof(requests[i]));
        requests[i].kind = MCC_CONVERT_AUDIO;
        requests[i].input = (uint32_t)(uintptr_t)inputs[i];
        requests[i].input_size = sizes[i];
        requests[i].parameters[0] = 0;
        requests[i].parameters[1] = (uint32_t)channels;
        requests[i].parameters[2] = (i % 3) ? 44100 : 22050;
        requests[i].parameters[3] = channels == 1 ? 22050 : requests[i].parameters[2];
        requests[i].parameters[4] = MCC_AUDIO_ENCODE;
        ids[i] = host_mcc_convert_submit((uint32_t)(uintptr_t)&requests[i]);
        if (ids[i] < 0) {printf("submit %d: %d\n", i, ids[i]); return 1;}
    }
    for (i = 0; i < SOUNDS; i++) {
        struct mcc_pcm pcm;
        uint32_t frames = 0, size, slot = (uint32_t)i % 4;
        unsigned char *expected, *got;
        long long offset;
        int result;
        while (!(result = host_mcc_convert_wait(ids[i], 5))) {}
        if (result != 1) {printf("sound %d failed: %d\n", i, result); failures++; continue;}
        mcc_pcm_open(inputs[i], sizes[i], 0, (int)requests[i].parameters[1], requests[i].parameters[2], &pcm);
        size = mcc_ima_encoded_size(&pcm, (int)requests[i].parameters[1], requests[i].parameters[3], &frames);
        expected = malloc(size);
        mcc_ima_encode(&pcm, (int)requests[i].parameters[1], requests[i].parameters[3], frames, expected, NULL);
        mcc_pcm_close(&pcm);
        if (host_mcc_convert_output_size(ids[i]) != size) {printf("sound %d size\n", i); failures++;}
        offset = host_mcc_audio_append(ids[i], slot);
        if (offset != (long long)used[slot]) {printf("sound %d offset %lld, not %u\n", i, offset, used[slot]); failures++;}
        got = low(size);
        if (!host_mcc_audio_read(slot, (uint32_t)offset, size, (uint32_t)(uintptr_t)got) || memcmp(got, expected, size)) {
            printf("sound %d bytes differ\n", i); failures++;
        }
        used[slot] += size;
        free(expected);
        host_mcc_convert_release(ids[i]);
    }
    /* a damaged permutation: IMA ADPCM with an impossible step */
    {
        struct mcc_convert_job *bad_request = low(sizeof(*bad_request));
        unsigned char *block = low(36);
        int id, result;
        memset(block, 0, 36); block[2] = 200;
        memset(bad_request, 0, sizeof(*bad_request));
        bad_request->kind = MCC_CONVERT_AUDIO; bad_request->input = (uint32_t)(uintptr_t)block; bad_request->input_size = 36;
        bad_request->parameters[0] = 1; bad_request->parameters[1] = 1; bad_request->parameters[2] = 22050;
        bad_request->parameters[3] = 22050; bad_request->parameters[4] = MCC_AUDIO_VALIDATE;
        id = host_mcc_convert_submit((uint32_t)(uintptr_t)bad_request);
        while (!(result = host_mcc_convert_wait(id, 5))) {}
        if (result != MCC_CONVERT_FAILED_DECODE) {printf("bad permutation: %d\n", result); failures++;}
        host_mcc_convert_release(id);
    }
    /* textures: BC7, transcoded as DXT5 and packed; or decoded (convert) and
    packed swizzled; the host's against the codecs run here */
    {
        struct mcc_convert_job *texture_requests = low(TEXTURES * sizeof(*texture_requests));
        struct mcc_convert_bitmap *descriptions = low(TEXTURES * sizeof(*descriptions));
        static int texture_ids[TEXTURES];
        static unsigned char *raws[TEXTURES], *targets[TEXTURES], *copies[TEXTURES];
        uint32_t *axes = malloc(MCC_BITMAP_AXES * sizeof(*axes));
        for (i = 0; i < TEXTURES; i++) {
            struct mcc_convert_bitmap *d = &descriptions[i];
            uint32_t side = 4u << (next() % 6), b, raw_size = 0, decoded_size = 0, level, levels;
            int convert = i % 3 == 0;
            for (level = 0, levels = 0; (side >> level) >= 1; level++, levels++) {
                uint32_t w = side >> level;
                raw_size += ((w + 3) / 4) * ((w + 3) / 4) * 16;
                decoded_size += w * w * 4;
                if (w == 1) {levels++; break;}
            }
            levels = level + 1;
            memset(d, 0, sizeof(*d));
            d->source_width = d->source_height = side; d->source_depth = 1; d->source_type = 0; d->source_format = 18;
            d->packed_width = d->packed_height = side; d->packed_depth = 1; d->packed_type = 0;
            d->packed_format = convert ? 11 : 16; d->packed_flags = convert ? 0 : 2;
            d->maximum = levels - 1; d->faces = 1; d->levels = levels;
            d->transcode = !convert; d->convert = convert;
            d->raw_size = raw_size; d->decoded_size = decoded_size;
            /* (packed: as many bytes as the source chain, decoded or not) */
            d->target_size = (((convert ? decoded_size : raw_size) + 127) & ~127u);
            raws[i] = low(raw_size); copies[i] = malloc(raw_size);
            for (b = 0; b < raw_size; b++) raws[i][b] = (unsigned char)next();
            memcpy(copies[i], raws[i], raw_size);
            targets[i] = low(d->target_size);
            memset(&texture_requests[i], 0, sizeof(texture_requests[i]));
            texture_requests[i].kind = MCC_CONVERT_BITMAP;
            texture_requests[i].input = (uint32_t)(uintptr_t)raws[i]; texture_requests[i].input_size = raw_size;
            texture_requests[i].output = (uint32_t)(uintptr_t)targets[i]; texture_requests[i].output_size = d->target_size;
            texture_requests[i].parameters[0] = (uint32_t)(uintptr_t)d;
            texture_ids[i] = host_mcc_convert_submit((uint32_t)(uintptr_t)&texture_requests[i]);
            if (texture_ids[i] < 0) {printf("texture submit %d\n", i); return 1;}
        }
        for (i = 0; i < TEXTURES; i++) {
            struct mcc_bitmap_work work;
            unsigned char *decoded, *expected;
            int result, done;
            while (!(result = host_mcc_convert_wait(texture_ids[i], 5))) {}
            mcc_bitmap_work_from(&descriptions[i], &work);
            decoded = work.convert ? malloc(work.decoded_size) : NULL;
            expected = malloc(work.target_size);
            done = mcc_bitmap_work_run(&work, copies[i], decoded, expected, axes, NULL);
            if (result != (done ? 1 : MCC_CONVERT_FAILED_DECODE)) {printf("texture %d: %d, not %d\n", i, result, done); failures++;}
            else if (done && memcmp(expected, targets[i], work.target_size)) {printf("texture %d bytes differ\n", i); failures++;}
            else if (done) textures_done++;
            free(decoded); free(expected);
            host_mcc_convert_release(texture_ids[i]);
        }
        free(axes);
    }
    host_mcc_convert_reset();
    /* (every texture described is one the codecs can do) */
    if (textures_done != TEXTURES) {printf("%d of %d textures done\n", textures_done, TEXTURES); failures++;}
    printf("%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
'''


@pytest.fixture(scope="module")
def driver(tmp_path_factory):
    compiler = shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("a C compiler is required")
    work = tmp_path_factory.mktemp("mcc-convert")
    (work / "host.h").write_text("#pragma once\n#define HOST_LOG_INFO 4\n"
                                 "void host_logf(int priority, const char *format, ...);\n")
    source = work / "driver.c"
    source.write_text(DRIVER)
    program = work / "driver"
    # (-fwrapv as the hosts build it: bcdec shifts signed values, as the guest's -fwrapv defines)
    command = [compiler, "-std=gnu99", "-O1", "-g", "-Wall", "-Werror", "-Wno-unused-function", "-fwrapv",
               "-fsanitize=undefined", "-fsanitize-trap=undefined", "-pthread",
               f"-I{work}", f"-I{ROOT / 'port/android/host'}", f"-I{ROOT / 'port/android/include'}", str(source), "-o", str(program), "-lm"]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return program


def test_jobs_match_one_thread(driver):
    result = subprocess.run([str(driver)], capture_output=True, text=True, timeout=600)
    assert result.returncode == 0 and result.stdout.strip().endswith("ok"), result.stdout + result.stderr
