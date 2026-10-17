/*
 * Stubs for SDLPoP features that are compiled out of the G&W build.
 *
 * OGG music (data/music/ *.ogg files) is not supported: SDLPoP only uses it when
 * those files exist, which never happens with an asset pack, but the code
 * referencing stb_vorbis is still linked.
 */
#include <stddef.h>

typedef struct stb_vorbis stb_vorbis;
typedef struct stb_vorbis_alloc stb_vorbis_alloc;

stb_vorbis *stb_vorbis_open_memory(const unsigned char *data, int len, int *error, const stb_vorbis_alloc *alloc)
{
    (void)data; (void)len; (void)alloc;
    if (error)
        *error = 1;
    return NULL;
}

void stb_vorbis_close(stb_vorbis *f) { (void)f; }

int stb_vorbis_get_samples_short_interleaved(stb_vorbis *f, int channels, short *buffer, int num_shorts)
{
    (void)f; (void)channels; (void)buffer; (void)num_shorts;
    return 0;
}

unsigned int stb_vorbis_stream_length_in_samples(stb_vorbis *f) { (void)f; return 0; }

int stb_vorbis_seek_start(stb_vorbis *f) { (void)f; return 0; }
