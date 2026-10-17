/* SDL_image stub for the G&W Prince of Persia core: PNG assets are not
 * supported on the device (tools/pop_pack.py converts them into DAT
 * resources at packing time). */
#ifndef GNW_POP_SDL_IMAGE_H
#define GNW_POP_SDL_IMAGE_H

#include "SDL.h"

static inline SDL_Surface *IMG_Load(const char *file) { (void)file; return NULL; }
static inline SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc) { (void)src; (void)freesrc; return NULL; }
static inline int IMG_SavePNG(SDL_Surface *surface, const char *file) { (void)surface; (void)file; return -1; }
static inline const char *IMG_GetError(void) { return "SDL_image not available"; }

#endif
