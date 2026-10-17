/*
 * Minimal SDL2 implementation for the G&W Prince of Persia core.
 * See sdl/SDL2/SDL.h for the scope of this layer.
 *
 * Distributed under the GPLv3 (same license as SDLPoP).
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "SDL2/SDL.h"
#include "pop_platform.h"

#define malloc  pop_malloc
#define calloc  pop_calloc
#define free    pop_free
#define realloc pop_realloc

#ifndef POP_FAST
#define POP_FAST
#endif

/* ------------------------------------------------------------------ */
/* Misc                                                               */
/* ------------------------------------------------------------------ */
const char *SDL_GetError(void) { return "unsupported"; }

void SDL_GetVersion(SDL_version *ver)
{
    SDL_VERSION(ver);
}

/* ------------------------------------------------------------------ */
/* Palettes / formats                                                 */
/* ------------------------------------------------------------------ */
SDL_Palette *SDL_AllocPalette(int ncolors)
{
    SDL_Palette *p = malloc(sizeof(SDL_Palette) + ncolors * sizeof(SDL_Color));
    if (!p)
        return NULL;
    p->ncolors = ncolors;
    p->colors = (SDL_Color *)(p + 1);
    p->version = 1;
    p->refcount = 1;
    memset(p->colors, 0xFF, ncolors * sizeof(SDL_Color)); /* SDL inits to white */
    return p;
}

void SDL_FreePalette(SDL_Palette *p)
{
    if (!p)
        return;
    if (--p->refcount <= 0)
        free(p);
}

int SDL_SetPaletteColors(SDL_Palette *p, const SDL_Color *colors, int first, int n)
{
    if (!p)
        return -1;
    int status = 0;
    if (first + n > p->ncolors) {
        n = p->ncolors - first;
        status = -1;
    }
    if (n > 0) {
        memcpy(p->colors + first, colors, n * sizeof(SDL_Color));
        p->version++;
        if (p->version == 0)
            p->version = 1;
    }
    return status;
}

static void init_format(SDL_PixelFormat *f, Uint32 format)
{
    memset(f, 0, sizeof(*f));
    f->format = format;
    switch (format) {
    case SDL_PIXELFORMAT_INDEX8:
        f->BitsPerPixel = 8; f->BytesPerPixel = 1;
        break;
    case SDL_PIXELFORMAT_RGB565:
        f->BitsPerPixel = 16; f->BytesPerPixel = 2;
        f->Rmask = 0xF800; f->Gmask = 0x07E0; f->Bmask = 0x001F;
        break;
    case SDL_PIXELFORMAT_ARGB8888:
    default:
        f->format = SDL_PIXELFORMAT_ARGB8888;
        f->BitsPerPixel = 32; f->BytesPerPixel = 4;
        f->Rmask = 0x00FF0000; f->Gmask = 0x0000FF00; f->Bmask = 0x000000FF; f->Amask = 0xFF000000;
        break;
    }
}

const char *SDL_GetPixelFormatName(Uint32 format)
{
    switch (format) {
    case SDL_PIXELFORMAT_INDEX8: return "SDL_PIXELFORMAT_INDEX8";
    case SDL_PIXELFORMAT_RGB565: return "SDL_PIXELFORMAT_RGB565";
    case SDL_PIXELFORMAT_ARGB8888: return "SDL_PIXELFORMAT_ARGB8888";
    default: return "SDL_PIXELFORMAT_UNKNOWN";
    }
}

static inline Uint16 rgb565(Uint8 r, Uint8 g, Uint8 b)
{
    return (Uint16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

Uint32 SDL_MapRGBA(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    switch (f->format) {
    case SDL_PIXELFORMAT_RGB565:
        return rgb565(r, g, b);
    case SDL_PIXELFORMAT_ARGB8888:
        return ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
    case SDL_PIXELFORMAT_INDEX8: {
        if (!f->palette)
            return 0;
        int best = 0, bestd = 0x7FFFFFFF;
        for (int i = 0; i < f->palette->ncolors; i++) {
            const SDL_Color *c = &f->palette->colors[i];
            int dr = c->r - r, dg = c->g - g, db = c->b - b;
            int d = dr * dr + dg * dg + db * db;
            if (d < bestd) { bestd = d; best = i; if (d == 0) break; }
        }
        return best;
    }
    default:
        return 0;
    }
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b)
{
    return SDL_MapRGBA(f, r, g, b, 255);
}

/* ------------------------------------------------------------------ */
/* Surfaces                                                           */
/* ------------------------------------------------------------------ */
static Uint32 format_for_depth(int depth)
{
    switch (depth) {
    case 8:  return SDL_PIXELFORMAT_INDEX8;
    case 15:
    case 16:
    case 24: return SDL_PIXELFORMAT_RGB565;   /* "24 bit" screens are RGB565 here */
    default: return SDL_PIXELFORMAT_ARGB8888;
    }
}

static SDL_Surface *create_surface(int w, int h, Uint32 format, void *pixels, int pitch, SDL_Palette *shared_palette)
{
    if (w < 0 || h < 0)
        return NULL;
    SDL_Surface *s = malloc(sizeof(SDL_Surface));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(*s));
    init_format(&s->fmt_storage, format);
    s->format = &s->fmt_storage;
    s->w = w;
    s->h = h;
    s->refcount = 1;
    s->alpha_mod = 255;
    s->blend_mode = (format == SDL_PIXELFORMAT_ARGB8888) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
    s->clip_rect.w = w;
    s->clip_rect.h = h;
    if (pixels) {
        s->pixels = pixels;
        s->pitch = pitch;
        s->flags |= SDL_PREALLOC;
    } else {
        int bpp = s->format->BytesPerPixel;
        s->pitch = (w * bpp + 3) & ~3;
        /* +4: tolerate code reading a whole Uint32 from the last pixel */
        size_t sz = (size_t)s->pitch * (h ? h : 1) + 4;
        s->pixels = malloc(sz);
        if (!s->pixels) {
            free(s);
            return NULL;
        }
        memset(s->pixels, 0, sz);
    }
    if (format == SDL_PIXELFORMAT_INDEX8) {
        if (shared_palette) {
            shared_palette->refcount++;
            s->format->palette = shared_palette;
        } else {
            s->format->palette = SDL_AllocPalette(256);
            if (!s->format->palette) {
                SDL_FreeSurface(s);
                return NULL;
            }
        }
    }
    return s;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth, Uint32 R, Uint32 G, Uint32 B, Uint32 A)
{
    (void)flags; (void)R; (void)G; (void)B; (void)A;
    return create_surface(w, h, format_for_depth(depth), NULL, 0, NULL);
}

SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h, int depth, Uint32 format)
{
    (void)flags; (void)depth;
    return create_surface(w, h, format, NULL, 0, NULL);
}

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int w, int h, int depth, int pitch, Uint32 R, Uint32 G, Uint32 B, Uint32 A)
{
    (void)R; (void)G; (void)B; (void)A;
    return create_surface(w, h, format_for_depth(depth), pixels, pitch, NULL);
}

SDL_Surface *GNW_CreateIndexedSurfaceFrom(const void *pixels, int w, int h, int pitch, SDL_Palette *palette)
{
    return create_surface(w, h, SDL_PIXELFORMAT_INDEX8, (void *)pixels, pitch, palette);
}

void SDL_FreeSurface(SDL_Surface *s)
{
    if (!s)
        return;
    if (--s->refcount > 0)
        return;
    if (s->format->palette)
        SDL_FreePalette(s->format->palette);
    if (!(s->flags & SDL_PREALLOC))
        free(s->pixels);
    free(s);
}

int SDL_SetColorKey(SDL_Surface *s, int flag, Uint32 key)
{
    if (!s)
        return -1;
    s->has_colorkey = flag ? 1 : 0;
    s->colorkey = key;
    return 0;
}

int SDL_GetColorKey(SDL_Surface *s, Uint32 *key)
{
    if (!s || !s->has_colorkey)
        return -1;
    if (key)
        *key = s->colorkey;
    return 0;
}

int SDL_SetSurfaceBlendMode(SDL_Surface *s, SDL_BlendMode mode)
{
    if (!s)
        return -1;
    s->blend_mode = (Uint8)mode;
    return 0;
}

int SDL_SetSurfaceAlphaMod(SDL_Surface *s, Uint8 alpha)
{
    if (!s)
        return -1;
    s->alpha_mod = alpha;
    return 0;
}

int SDL_SetSurfacePalette(SDL_Surface *s, SDL_Palette *palette)
{
    if (!s || s->format->format != SDL_PIXELFORMAT_INDEX8)
        return -1;
    if (palette == s->format->palette)
        return 0;
    if (palette)
        palette->refcount++;
    if (s->format->palette)
        SDL_FreePalette(s->format->palette);
    s->format->palette = palette;
    return 0;
}

static int intersect(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *out)
{
    int x0 = a->x > b->x ? a->x : b->x;
    int y0 = a->y > b->y ? a->y : b->y;
    int x1 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    int y1 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    out->x = x0;
    out->y = y0;
    out->w = x1 > x0 ? x1 - x0 : 0;
    out->h = y1 > y0 ? y1 - y0 : 0;
    return out->w > 0 && out->h > 0;
}

SDL_bool SDL_SetClipRect(SDL_Surface *s, const SDL_Rect *rect)
{
    if (!s)
        return SDL_FALSE;
    SDL_Rect full = { 0, 0, s->w, s->h };
    if (!rect) {
        s->clip_rect = full;
        return SDL_TRUE;
    }
    return intersect(rect, &full, &s->clip_rect) ? SDL_TRUE : SDL_FALSE;
}

/* ------------------------------------------------------------------ */
/* Pixel access helpers                                               */
/* ------------------------------------------------------------------ */
static inline Uint32 rgba_from_565(Uint16 p)
{
    Uint32 r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* Read pixel at x,y as ARGB8888 (colour key -> alpha 0 when use_key). */
static inline Uint32 get_argb(SDL_Surface *s, const Uint8 *row, int x)
{
    switch (s->format->format) {
    case SDL_PIXELFORMAT_INDEX8: {
        Uint8 i = row[x];
        SDL_Palette *p = s->format->palette;
        if (!p || i >= p->ncolors)
            return 0xFF000000u;
        const SDL_Color *c = &p->colors[i];
        return ((Uint32)c->a << 24) | ((Uint32)c->r << 16) | ((Uint32)c->g << 8) | c->b;
    }
    case SDL_PIXELFORMAT_RGB565:
        return rgba_from_565(((const Uint16 *)row)[x]);
    default:
        return ((const Uint32 *)row)[x];
    }
}

static inline Uint32 get_raw(SDL_Surface *s, const Uint8 *row, int x)
{
    switch (s->format->BytesPerPixel) {
    case 1: return row[x];
    case 2: return ((const Uint16 *)row)[x];
    default: return ((const Uint32 *)row)[x];
    }
}

static inline void put_argb(SDL_Surface *d, Uint8 *row, int x, Uint32 c)
{
    switch (d->format->format) {
    case SDL_PIXELFORMAT_INDEX8:
        row[x] = (Uint8)SDL_MapRGBA(d->format, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, c >> 24);
        break;
    case SDL_PIXELFORMAT_RGB565:
        ((Uint16 *)row)[x] = rgb565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
        break;
    default:
        ((Uint32 *)row)[x] = c;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Blitting                                                           */
/* ------------------------------------------------------------------ */
/* Palette -> RGB565 lookup table, cached per palette version. */
static const SDL_Palette *lut_palette;
static Uint32 lut_version;
static Uint16 lut565[256];
static Uint8  lutalpha[256];

static void build_lut(const SDL_Palette *p)
{
    if (p == lut_palette && p->version == lut_version)
        return;
    int n = p->ncolors > 256 ? 256 : p->ncolors;
    for (int i = 0; i < n; i++) {
        lut565[i] = rgb565(p->colors[i].r, p->colors[i].g, p->colors[i].b);
        lutalpha[i] = p->colors[i].a;
    }
    for (int i = n; i < 256; i++) {
        lut565[i] = 0;
        lutalpha[i] = 255;
    }
    lut_palette = p;
    lut_version = p->version;
}

static inline Uint16 blend565(Uint16 d, Uint32 sr, Uint32 sg, Uint32 sb, Uint32 a)
{
    int dr = (d >> 11) & 0x1F, dg = (d >> 5) & 0x3F, db = d & 0x1F;
    int r5 = (int)(sr >> 3), g6 = (int)(sg >> 2), b5 = (int)(sb >> 3);
    int ia = (int)a;
    dr = dr + (((r5 - dr) * ia) >> 8);
    dg = dg + (((g6 - dg) * ia) >> 8);
    db = db + (((b5 - db) * ia) >> 8);
    return (Uint16)(((dr & 0x1F) << 11) | ((dg & 0x3F) << 5) | (db & 0x1F));
}

/* Copy w x h pixels from (sx,sy) of src to (dx,dy) of dst. Rectangles have
 * already been clipped. */
POP_FAST
static void blit_rect(SDL_Surface *src, int sx, int sy, SDL_Surface *dst, int dx, int dy, int w, int h)
{
    const Uint32 sf = src->format->format, df = dst->format->format;
    const int key = src->has_colorkey;
    const Uint32 keyval = src->colorkey;
    const int blend = src->blend_mode == SDL_BLENDMODE_BLEND;
    const Uint32 amod = src->alpha_mod;
    const Uint8 *srow = (const Uint8 *)src->pixels + sy * src->pitch + sx * src->format->BytesPerPixel;
    Uint8 *drow = (Uint8 *)dst->pixels + dy * dst->pitch + dx * dst->format->BytesPerPixel;

    if (sf == SDL_PIXELFORMAT_INDEX8 && df == SDL_PIXELFORMAT_RGB565 && src->format->palette) {
        build_lut(src->format->palette);
        if (blend) {
            for (int y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch) {
                Uint16 *d = (Uint16 *)drow;
                for (int x = 0; x < w; x++) {
                    Uint8 i = srow[x];
                    if (key && i == keyval)
                        continue;
                    Uint32 a = (lutalpha[i] * amod) / 255;
                    if (a >= 255) { d[x] = lut565[i]; continue; }
                    if (a == 0) continue;
                    const SDL_Color *c = &src->format->palette->colors[i];
                    d[x] = blend565(d[x], c->r, c->g, c->b, a);
                }
            }
        } else if (key) {
            for (int y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch) {
                Uint16 *d = (Uint16 *)drow;
                for (int x = 0; x < w; x++) {
                    Uint8 i = srow[x];
                    if (i != keyval)
                        d[x] = lut565[i];
                }
            }
        } else {
            for (int y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch) {
                Uint16 *d = (Uint16 *)drow;
                for (int x = 0; x < w; x++)
                    d[x] = lut565[srow[x]];
            }
        }
        return;
    }

    if (sf == df && !blend) {
        const int bpp = src->format->BytesPerPixel;
        if (!key) {
            for (int y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch)
                memmove(drow, srow, (size_t)w * bpp);
            return;
        }
        if (bpp == 1) {
            for (int y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch)
                for (int x = 0; x < w; x++)
                    if (srow[x] != keyval) drow[x] = srow[x];
            return;
        }
        if (bpp == 2) {
            for (int y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch) {
                const Uint16 *s = (const Uint16 *)srow;
                Uint16 *d = (Uint16 *)drow;
                for (int x = 0; x < w; x++)
                    if (s[x] != keyval) d[x] = s[x];
            }
            return;
        }
    }

    if (sf == SDL_PIXELFORMAT_ARGB8888 && df == SDL_PIXELFORMAT_RGB565) {
        for (int y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch) {
            const Uint32 *s = (const Uint32 *)srow;
            Uint16 *d = (Uint16 *)drow;
            for (int x = 0; x < w; x++) {
                Uint32 c = s[x];
                if (key && c == keyval)
                    continue;
                Uint32 r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                if (blend) {
                    Uint32 a = ((c >> 24) * amod) / 255;
                    if (a == 0) continue;
                    if (a < 255) { d[x] = blend565(d[x], r, g, b, a); continue; }
                }
                d[x] = rgb565(r, g, b);
            }
        }
        return;
    }

    /* Generic (slow) path. */
    for (int y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch) {
        for (int x = 0; x < w; x++) {
            if (key && get_raw(src, srow, x) == keyval)
                continue;
            Uint32 c = get_argb(src, srow, x);
            if (!blend && sf == SDL_PIXELFORMAT_INDEX8)
                c |= 0xFF000000u;
            if (blend) {
                Uint32 a = ((c >> 24) * amod) / 255;
                if (a == 0) continue;
                if (a < 255) {
                    Uint32 dc = get_argb(dst, drow, x);
                    int r = ((dc >> 16) & 0xFF), g = ((dc >> 8) & 0xFF), b = (dc & 0xFF);
                    int ia = (int)a;
                    r += (((int)((c >> 16) & 0xFF) - r) * ia) >> 8;
                    g += (((int)((c >> 8) & 0xFF) - g) * ia) >> 8;
                    b += (((int)(c & 0xFF) - b) * ia) >> 8;
                    Uint32 da = dc >> 24;
                    da = a + ((da * (255 - a)) / 255);
                    c = (da << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
                }
            }
            put_argb(dst, drow, x, c);
        }
    }
}

int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    if (!src || !dst)
        return -1;
    SDL_Rect sr = srcrect ? *srcrect : (SDL_Rect){ 0, 0, src->w, src->h };
    int dx = dstrect ? dstrect->x : 0;
    int dy = dstrect ? dstrect->y : 0;

    /* clip source to the source surface */
    if (sr.x < 0) { dx -= sr.x; sr.w += sr.x; sr.x = 0; }
    if (sr.y < 0) { dy -= sr.y; sr.h += sr.y; sr.y = 0; }
    if (sr.x + sr.w > src->w) sr.w = src->w - sr.x;
    if (sr.y + sr.h > src->h) sr.h = src->h - sr.y;

    /* clip destination to the clip rect */
    const SDL_Rect *c = &dst->clip_rect;
    if (dx < c->x) { int d = c->x - dx; sr.x += d; sr.w -= d; dx = c->x; }
    if (dy < c->y) { int d = c->y - dy; sr.y += d; sr.h -= d; dy = c->y; }
    if (dx + sr.w > c->x + c->w) sr.w = c->x + c->w - dx;
    if (dy + sr.h > c->y + c->h) sr.h = c->y + c->h - dy;

    if (dstrect) {
        dstrect->x = dx;
        dstrect->y = dy;
        dstrect->w = sr.w > 0 ? sr.w : 0;
        dstrect->h = sr.h > 0 ? sr.h : 0;
    }
    if (sr.w <= 0 || sr.h <= 0)
        return 0;
    blit_rect(src, sr.x, sr.y, dst, dx, dy, sr.w, sr.h);
    return 0;
}

int SDL_UpperBlitScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    /* Only used by code paths that are disabled on G&W: fall back to a plain blit. */
    return SDL_UpperBlit(src, srcrect, dst, dstrect);
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
    if (!dst)
        return -1;
    SDL_Rect r;
    if (rect) {
        if (!intersect(rect, &dst->clip_rect, &r))
            return 0;
    } else {
        r = dst->clip_rect;
    }
    Uint8 *row = (Uint8 *)dst->pixels + r.y * dst->pitch + r.x * dst->format->BytesPerPixel;
    for (int y = 0; y < r.h; y++, row += dst->pitch) {
        switch (dst->format->BytesPerPixel) {
        case 1:
            memset(row, (int)color, r.w);
            break;
        case 2: {
            Uint16 *p = (Uint16 *)row;
            for (int x = 0; x < r.w; x++) p[x] = (Uint16)color;
            break;
        }
        default: {
            Uint32 *p = (Uint32 *)row;
            for (int x = 0; x < r.w; x++) p[x] = color;
            break;
        }
        }
    }
    return 0;
}

SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 format, Uint32 flags)
{
    (void)flags;
    if (!src)
        return NULL;
    SDL_Surface *d = create_surface(src->w, src->h, format, NULL, 0, NULL);
    if (!d)
        return NULL;
    const Uint32 sf = src->format->format;
    if (format == SDL_PIXELFORMAT_INDEX8 && sf == SDL_PIXELFORMAT_INDEX8 && src->format->palette) {
        SDL_SetPaletteColors(d->format->palette, src->format->palette->colors, 0,
                             src->format->palette->ncolors < 256 ? src->format->palette->ncolors : 256);
    }
    /* raw conversion: no blending, colour key turned into alpha for ARGB */
    const Uint8 *srow = (const Uint8 *)src->pixels;
    Uint8 *drow = (Uint8 *)d->pixels;
    for (int y = 0; y < src->h; y++, srow += src->pitch, drow += d->pitch) {
        if (sf == format) {
            memcpy(drow, srow, (size_t)src->w * src->format->BytesPerPixel);
            continue;
        }
        for (int x = 0; x < src->w; x++) {
            Uint32 c = get_argb(src, srow, x) | 0xFF000000u;
            if (src->has_colorkey && get_raw(src, srow, x) == src->colorkey)
                c &= 0x00FFFFFFu;
            put_argb(d, drow, x, c);
        }
    }
    if (src->has_colorkey && format != SDL_PIXELFORMAT_ARGB8888) {
        Uint32 key = src->colorkey;
        if (sf != format) {
            /* re-map the key colour into the destination format */
            Uint8 tmp[4] = { 0 };
            Uint32 c = 0;
            if (sf == SDL_PIXELFORMAT_INDEX8) {
                tmp[0] = (Uint8)key;
                c = get_argb(src, tmp, 0);
            } else if (sf == SDL_PIXELFORMAT_RGB565) {
                c = rgba_from_565((Uint16)key);
            } else {
                c = key;
            }
            key = SDL_MapRGB(d->format, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
        }
        SDL_SetColorKey(d, SDL_TRUE, key);
    }
    d->blend_mode = (format == SDL_PIXELFORMAT_ARGB8888) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
    return d;
}

SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, const SDL_PixelFormat *fmt, Uint32 flags)
{
    if (!fmt)
        return NULL;
    SDL_Surface *d = SDL_ConvertSurfaceFormat(src, fmt->format, flags);
    if (d && fmt->format == SDL_PIXELFORMAT_INDEX8 && fmt->palette)
        SDL_SetSurfacePalette(d, fmt->palette);
    return d;
}

SDL_Surface *GNW_FlipSurfaceH(SDL_Surface *src)
{
    if (!src)
        return NULL;
    SDL_Surface *d = create_surface(src->w, src->h, src->format->format, NULL, 0,
                                    src->format->format == SDL_PIXELFORMAT_INDEX8 ? src->format->palette : NULL);
    if (!d)
        return NULL;
    const int bpp = src->format->BytesPerPixel;
    for (int y = 0; y < src->h; y++) {
        const Uint8 *s = (const Uint8 *)src->pixels + y * src->pitch;
        Uint8 *o = (Uint8 *)d->pixels + y * d->pitch;
        for (int x = 0, rx = src->w - 1; x < src->w; x++, rx--)
            memcpy(o + rx * bpp, s + x * bpp, bpp);
    }
    d->has_colorkey = src->has_colorkey;
    d->colorkey = src->colorkey;
    d->blend_mode = src->blend_mode;
    d->alpha_mod = src->alpha_mod;
    return d;
}

void GNW_BlitMono(SDL_Surface *src, int x, int y, SDL_Surface *dst, Uint32 color)
{
    if (!src || !dst || src->format->format != SDL_PIXELFORMAT_INDEX8)
        return;
    SDL_Rect r = { x, y, src->w, src->h }, c;
    if (!intersect(&r, &dst->clip_rect, &c))
        return;
    for (int yy = c.y; yy < c.y + c.h; yy++) {
        const Uint8 *s = (const Uint8 *)src->pixels + (yy - y) * src->pitch + (c.x - x);
        Uint8 *d = (Uint8 *)dst->pixels + yy * dst->pitch + c.x * dst->format->BytesPerPixel;
        for (int xx = 0; xx < c.w; xx++) {
            if (s[xx] == 0)
                continue;
            switch (dst->format->BytesPerPixel) {
            case 1: d[xx] = (Uint8)color; break;
            case 2: ((Uint16 *)d)[xx] = (Uint16)color; break;
            default: ((Uint32 *)d)[xx] = color; break;
            }
        }
    }
}

void GNW_FadeSurface(SDL_Surface *dst, SDL_Surface *src, int sub8)
{
    if (!dst || !src || dst->format->format != SDL_PIXELFORMAT_RGB565 || src->format->format != SDL_PIXELFORMAT_RGB565)
        return;
    int h = dst->h < src->h ? dst->h : src->h;
    int w = dst->w < src->w ? dst->w : src->w;
    if (sub8 < 0)
        sub8 = 0;
    int sr = sub8 >> 3, sg = sub8 >> 2, sb = sub8 >> 3;
    for (int y = 0; y < h; y++) {
        const Uint16 *s = (const Uint16 *)((const Uint8 *)src->pixels + y * src->pitch);
        Uint16 *d = (Uint16 *)((Uint8 *)dst->pixels + y * dst->pitch);
        for (int x = 0; x < w; x++) {
            Uint16 p = s[x];
            int r = ((p >> 11) & 0x1F) - sr;
            int g = ((p >> 5) & 0x3F) - sg;
            int b = (p & 0x1F) - sb;
            if (r < 0) r = 0;
            if (g < 0) g = 0;
            if (b < 0) b = 0;
            d[x] = (Uint16)((r << 11) | (g << 5) | b);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Keyboard + events                                                  */
/* ------------------------------------------------------------------ */
#define EVENT_QUEUE_SIZE 64
static SDL_Event event_queue[EVENT_QUEUE_SIZE];
static int eq_head, eq_tail;
static Uint8 keyboard_state[SDL_NUM_SCANCODES];
static Uint16 mod_state;

int SDL_PushEvent(SDL_Event *e)
{
    int next = (eq_tail + 1) % EVENT_QUEUE_SIZE;
    if (next == eq_head)
        return 0; /* full: drop */
    event_queue[eq_tail] = *e;
    eq_tail = next;
    return 1;
}

int SDL_PollEvent(SDL_Event *e)
{
    pop_plat_service(0);
    if (eq_head == eq_tail)
        return 0;
    if (e)
        *e = event_queue[eq_head];
    eq_head = (eq_head + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

const Uint8 *SDL_GetKeyboardState(int *numkeys)
{
    if (numkeys)
        *numkeys = SDL_NUM_SCANCODES;
    return keyboard_state;
}

const char *SDL_GetScancodeName(SDL_Scancode sc)
{
    (void)sc;
    return "";
}

static Uint16 mod_for(int sc)
{
    switch (sc) {
    case SDL_SCANCODE_LSHIFT: return KMOD_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return KMOD_RSHIFT;
    case SDL_SCANCODE_LCTRL:  return KMOD_LCTRL;
    case SDL_SCANCODE_RCTRL:  return KMOD_RCTRL;
    case SDL_SCANCODE_LALT:   return KMOD_LALT;
    case SDL_SCANCODE_RALT:   return KMOD_RALT;
    default: return 0;
    }
}

void pop_sdl_key(int sc, int down)
{
    if (sc <= 0 || sc >= SDL_NUM_SCANCODES)
        return;
    down = down ? 1 : 0;
    if (keyboard_state[sc] == down)
        return;
    keyboard_state[sc] = (Uint8)down;
    Uint16 m = mod_for(sc);
    if (m) {
        if (down) mod_state |= m; else mod_state &= ~m;
    }
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = (Uint8)down;
    e.key.keysym.scancode = (SDL_Scancode)sc;
    e.key.keysym.mod = mod_state;
    SDL_PushEvent(&e);
}

void pop_sdl_release_all_keys(void)
{
    for (int sc = 1; sc < SDL_NUM_SCANCODES; sc++)
        if (keyboard_state[sc])
            pop_sdl_key(sc, 0);
}

/* ------------------------------------------------------------------ */
/* Time                                                               */
/* ------------------------------------------------------------------ */
Uint32 SDL_GetTicks(void)
{
    return (Uint32)(pop_plat_time_us() / 1000);
}

Uint64 SDL_GetPerformanceCounter(void)
{
    return pop_plat_time_us();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return 1000000;
}

void SDL_Delay(Uint32 ms)
{
    uint64_t end = pop_plat_time_us() + (uint64_t)ms * 1000;
    do {
        pop_plat_service(1);
    } while (pop_plat_time_us() < end);
}

/* ------------------------------------------------------------------ */
/* Audio                                                              */
/* ------------------------------------------------------------------ */
static SDL_AudioSpec *audio_spec;
static int audio_paused = 1;

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    if (!desired)
        return -1;
    desired->freq = pop_plat_audio_rate();
    desired->format = AUDIO_S16SYS;
    desired->channels = 2;
    desired->silence = 0;
    if (desired->samples == 0)
        desired->samples = 512;
    desired->size = desired->samples * 4;
    if (obtained)
        *obtained = *desired;
    audio_spec = desired;
    audio_paused = 1;
    return 0;
}

void SDL_CloseAudio(void)
{
    audio_spec = NULL;
}

void SDL_PauseAudio(int pause_on)
{
    audio_paused = pause_on;
}

SDL_AudioStatus SDL_GetAudioStatus(void)
{
    if (!audio_spec)
        return SDL_AUDIO_STOPPED;
    return audio_paused ? SDL_AUDIO_PAUSED : SDL_AUDIO_PLAYING;
}

#define AUDIO_CHUNK 256
static int16_t audio_tmp[AUDIO_CHUNK * 2];
static int audio_in_callback;

void pop_sdl_audio_render(int16_t *out, int frames)
{
    if (!audio_spec || audio_paused || !audio_spec->callback || audio_in_callback) {
        memset(out, 0, (size_t)frames * sizeof(int16_t));
        return;
    }
    audio_in_callback = 1;
    while (frames > 0) {
        int n = frames > AUDIO_CHUNK ? AUDIO_CHUNK : frames;
        memset(audio_tmp, 0, (size_t)n * 4);
        audio_spec->callback(audio_spec->userdata, (Uint8 *)audio_tmp, n * 4);
        for (int i = 0; i < n; i++) {
            int s = ((int)audio_tmp[2 * i] + (int)audio_tmp[2 * i + 1]) >> 1;
            out[i] = (int16_t)s;
        }
        out += n;
        frames -= n;
    }
    audio_in_callback = 0;
}

/* ------------------------------------------------------------------ */
/* RWops                                                              */
/* ------------------------------------------------------------------ */
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size)
{
    SDL_RWops *rw = malloc(sizeof(SDL_RWops));
    if (!rw)
        return NULL;
    rw->base = mem;
    rw->size = (size_t)size;
    rw->pos = 0;
    return rw;
}

SDL_RWops *SDL_RWFromMem(void *mem, int size)
{
    return SDL_RWFromConstMem(mem, size);
}

int SDL_RWclose(SDL_RWops *rw)
{
    free(rw);
    return 0;
}

size_t SDL_RWread(SDL_RWops *rw, void *ptr, size_t size, size_t n)
{
    if (!rw || size == 0)
        return 0;
    size_t avail = (rw->size - rw->pos) / size;
    if (n > avail)
        n = avail;
    memcpy(ptr, rw->base + rw->pos, n * size);
    rw->pos += n * size;
    return n;
}

