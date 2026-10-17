/*
 * Interface between SDLPoP (+ the SDL shim) and the platform it runs on.
 *
 * Two implementations exist:
 *  - Core/Src/porting/pop/main_pop.c : the Game & Watch core (retro-go)
 *  - linux/pop/pop_host.c            : a headless Linux harness used to test
 *                                      the port without hardware
 */
#ifndef POP_PLATFORM_H
#define POP_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- implemented by the platform ---- */

/* Monotonic time in microseconds. */
uint64_t pop_plat_time_us(void);

/* Called very often by SDL_Delay() (in_delay = 1) and SDL_PollEvent()
 * (in_delay = 0). The platform reads its buttons and pushes key events with
 * pop_sdl_key(), feeds the audio output with pop_sdl_audio_render(), and may
 * open its own menus. */
void pop_plat_service(int in_delay);

/* Show a 320x200 RGB565 frame. pitch is in bytes. */
void pop_plat_present(const uint16_t *pixels, int pitch);

/* Output sample rate (mono, signed 16 bit). */
int pop_plat_audio_rate(void);

/* Look up a file of the asset pack (DAT files, mod.ini, PRINCE.EXE...).
 * The returned memory is read-only and stays valid while the game runs. */
bool pop_plat_find_file(const char *name, const uint8_t **data, uint32_t *size);

/* Directory (without trailing '/') where SDLPoP may write its files
 * (PRINCE.SAV, PRINCE.HOF, QUICKSAVE.SAV...). */
const char *pop_plat_save_dir(void);

/* The game asked to quit. Never returns. */
void pop_plat_quit(int exit_code) __attribute__((noreturn));

/* ---- implemented by the SDL shim (pop_sdl.c) ---- */

/* Inject a key press/release (SDL scancode). Modifier keys (shift, ctrl,
 * alt) update the modifier state used for the following events. */
void pop_sdl_key(int scancode, int down);

/* Release every key that is currently held. */
void pop_sdl_release_all_keys(void);

/* Render `frames` mono samples by pulling the SDLPoP audio callback.
 * Outputs silence when audio is not opened or paused. */
void pop_sdl_audio_render(int16_t *out, int frames);

/* ---- implemented by pop_file.c ---- */
/* stdio for SDLPoP: files found in the asset pack are read-only in-memory
 * handles, every other path is a real file. */
struct stat;
FILE  *pop_fopen(const char *filename, const char *mode);
int    pop_fclose(FILE *f);
size_t pop_fread(void *ptr, size_t size, size_t n, FILE *f);
size_t pop_fwrite(const void *ptr, size_t size, size_t n, FILE *f);
int    pop_fseek(FILE *f, long offset, int whence);
long   pop_ftell(FILE *f);
int    pop_feof(FILE *f);
int    pop_fgetc(FILE *f);
int    pop_fscanf(FILE *f, const char *fmt, ...);
int    pop_fprintf(FILE *f, const char *fmt, ...);
void   pop_perror(const char *s);
int    pop_fileno(FILE *f);
int    pop_fstat(int fd, struct stat *st);
int    pop_stat(const char *path, struct stat *st);

/* ---- implemented by pop_alloc.c ---- */
void   pop_heap_init(void *base, size_t size);
void  *pop_malloc(size_t size);
void  *pop_calloc(size_t n, size_t size);
void  *pop_realloc(void *ptr, size_t size);
void   pop_free(void *ptr);
char  *pop_strdup(const char *s);
size_t pop_heap_used(void);
size_t pop_heap_peak(void);
size_t pop_heap_size(void);
/* Host debug builds only (POP_HOST_SYSTEM_MALLOC): never free pointers in this range. */
void   pop_heap_ignore_range(const void *lo, const void *hi);

#ifdef __cplusplus
}
#endif

#endif
