/*
 * Pre-rendered music for SDLPoP.
 *
 * SDLPoP synthesizes its MIDI music with an OPL3 emulator, which costs more
 * CPU than the rest of the game together on the Game & Watch. The music
 * pieces are fixed, so tools/pop_pack.py renders them once on the PC with the
 * very same code (Linux harness, option -R) and stores them in the pack as
 * "MIDInn.MUS" files (nn = SDLPoP sound index).
 *
 * SDLPoP can already replace a MIDI piece with an OGG file (sound_ogg, played
 * by ogg_callback() through the stb_vorbis API). This file implements that
 * API for the .MUS format, reading the samples in place from the pack (memory
 * mapped flash): load_sound() (seg009.c, GNW patch) opens them, the rest of
 * the OGG path plays them.
 *
 * .MUS format (little endian):
 *   0x00 char magic[4]   "GWMU"
 *   0x04 u16  version    1
 *   0x06 u16  codec      0 = signed 16 bit PCM, 1 = IMA ADPCM
 *   0x08 u32  rate       sample rate (the output rate of the game: 22050)
 *   0x0C u32  samples    number of mono samples
 *   0x10      data
 *               PCM:   int16 samples[samples]
 *               ADPCM: blocks of GWMU_ADPCM_BLOCK samples:
 *                        int16 predictor, u8 step index, u8 0,
 *                        then 4 bit codes (low nibble first)
 *
 * Distributed under the GPLv3 (same license as SDLPoP).
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "pop_platform.h"

#define GWMU_HEADER_SIZE   16
#define GWMU_ADPCM_BLOCK   1024
#define GWMU_ADPCM_BLOCK_BYTES (4 + GWMU_ADPCM_BLOCK / 2)

enum { CODEC_PCM16 = 0, CODEC_ADPCM = 1 };

typedef struct stb_vorbis {
    const uint8_t *data; /* after the header */
    uint32_t data_size;
    uint32_t samples;
    uint32_t rate;
    int codec;
    uint32_t pos;        /* next sample */
    /* ADPCM decoder state */
    int32_t predictor;
    int step_index;
} stb_vorbis;

typedef struct stb_vorbis_alloc stb_vorbis_alloc;

static const int16_t ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};
static const int8_t ima_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

stb_vorbis *stb_vorbis_open_memory(const unsigned char *data, int len, int *error, const stb_vorbis_alloc *alloc)
{
    (void)alloc;
    if (error)
        *error = 1;
    if (!data || len < GWMU_HEADER_SIZE || memcmp(data, "GWMU", 4) != 0 || rd16(data + 4) != 1)
        return NULL;
    int codec = rd16(data + 6);
    uint32_t samples = rd32(data + 12);
    uint32_t size = (uint32_t)len - GWMU_HEADER_SIZE;
    if (codec == CODEC_PCM16) {
        if ((uint64_t)samples * 2 > size)
            return NULL;
    } else if (codec == CODEC_ADPCM) {
        uint32_t blocks = (samples + GWMU_ADPCM_BLOCK - 1) / GWMU_ADPCM_BLOCK;
        if ((uint64_t)blocks * GWMU_ADPCM_BLOCK_BYTES > (uint64_t)size + GWMU_ADPCM_BLOCK / 2)
            return NULL;
    } else {
        return NULL;
    }
    stb_vorbis *f = pop_malloc(sizeof(*f));
    if (!f)
        return NULL;
    memset(f, 0, sizeof(*f));
    f->data = data + GWMU_HEADER_SIZE;
    f->data_size = size;
    f->samples = samples;
    f->rate = rd32(data + 8);
    f->codec = codec;
    if (error)
        *error = 0;
    return f;
}

void stb_vorbis_close(stb_vorbis *f)
{
    pop_free(f);
}

int stb_vorbis_seek_start(stb_vorbis *f)
{
    if (!f)
        return 0;
    f->pos = 0;
    return 1;
}

unsigned int stb_vorbis_stream_length_in_samples(stb_vorbis *f)
{
    return f ? f->samples : 0;
}

static inline int16_t next_sample(stb_vorbis *f)
{
    uint32_t pos = f->pos++;
    if (f->codec == CODEC_PCM16) {
        const uint8_t *p = f->data + pos * 2;
        return (int16_t)(p[0] | (p[1] << 8));
    }
    uint32_t in_block = pos % GWMU_ADPCM_BLOCK;
    const uint8_t *block = f->data + (pos / GWMU_ADPCM_BLOCK) * GWMU_ADPCM_BLOCK_BYTES;
    if (in_block == 0) {
        f->predictor = (int16_t)rd16(block);
        f->step_index = block[2] > 88 ? 88 : block[2];
    }
    uint8_t byte = block[4 + in_block / 2];
    int code = (in_block & 1) ? (byte >> 4) : (byte & 15);
    int step = ima_step[f->step_index];
    int diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    int32_t p = (code & 8) ? f->predictor - diff : f->predictor + diff;
    if (p > 32767) p = 32767;
    if (p < -32768) p = -32768;
    f->predictor = p;
    int idx = f->step_index + ima_index[code & 7];
    f->step_index = idx < 0 ? 0 : idx > 88 ? 88 : idx;
    return (int16_t)p;
}

/* Used by ogg_callback() (seg009.c, GNW version): adds the next `frames`
 * samples to `out` (interleaved, `channels` channels) when `audible`, or
 * only moves forward. Returns the number of frames that were available
 * (0 when the piece has ended). */
int gnw_music_mix(stb_vorbis *f, short *out, int frames, int channels, int audible)
{
    if (!f || f->pos >= f->samples)
        return 0;
    uint32_t left = f->samples - f->pos;
    int n = frames < (int)left ? frames : (int)left;
    if (!audible) {
        if (f->codec == CODEC_PCM16) {
            f->pos += n;
        } else {
            for (int i = 0; i < n; i++)
                next_sample(f); /* ADPCM must be decoded in order */
        }
        return n;
    }
    for (int i = 0; i < n; i++) {
        int s = next_sample(f);
        for (int c = 0; c < channels; c++)
            out[i * channels + c] = (short)(out[i * channels + c] + s); /* wraps like midi_callback() */
    }
    return n;
}

/* Unused (the OGG path of SDLPoP is replaced by gnw_music_mix()), kept for
 * the link. */
int stb_vorbis_get_samples_short_interleaved(stb_vorbis *f, int channels, short *buffer, int num_shorts)
{
    (void)f; (void)channels; (void)buffer; (void)num_shorts;
    return 0;
}
