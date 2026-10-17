/*
 * Force-included (-include) when compiling SDLPoP for the Game & Watch.
 *
 * Redirects the C library calls SDLPoP makes to the core's own heap and to
 * the asset pack. System headers are included first so that the macros below
 * only affect SDLPoP's own code.
 */
#ifndef GNW_POP_GLUE_H
#define GNW_POP_GLUE_H

#ifndef GNW
#define GNW 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include "pop_platform.h"

#define malloc(s)     pop_malloc(s)
#define calloc(n, s)  pop_calloc(n, s)
#define realloc(p, s) pop_realloc(p, s)
#define free(p)       pop_free(p)
#define strdup(s)     pop_strdup(s)

/* stdio: pack files are in-memory handles, the rest are real files
 * (pop_file.c) */
/* newlib implements some of them as macros */
#undef feof
#undef fileno
#undef fgetc
#define fopen(f, m)          pop_fopen(f, m)
#define fclose(f)            pop_fclose(f)
#define fread(p, s, n, f)    pop_fread(p, s, n, f)
#define fwrite(p, s, n, f)   pop_fwrite(p, s, n, f)
#define fseek(f, o, w)       pop_fseek(f, o, w)
#define ftell(f)             pop_ftell(f)
#define feof(f)              pop_feof(f)
#define fgetc(f)             pop_fgetc(f)
#define fscanf(f, ...)       pop_fscanf(f, __VA_ARGS__)
#define fprintf(f, ...)      pop_fprintf(f, __VA_ARGS__)
#define perror(s)            pop_perror(s)
#define fileno(f)            pop_fileno(f)
#define fstat(fd, st)        pop_fstat(fd, st)
#define stat(p, st)          pop_stat(p, st)

/* SDLPoP globals whose names clash with other cores linked in the same
 * firmware image (every core lives in the same ELF, in its own overlay). */
#define palette       gnw_pop_palette

/* SDLPoP's common.h defines it again */
#undef _POSIX_C_SOURCE

/* Hooks implemented by the platform glue (main_pop.c / pop_host.c). */
void gnw_pop_apply_options(void);

#endif
