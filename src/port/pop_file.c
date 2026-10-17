/*
 * File access for SDLPoP.
 *
 * Files of the asset pack (DAT files, SDLPoP.ini, mod.ini, sound names...)
 * are read-only memory blocks: they are opened as small in-memory handles
 * taken from a static pool. Every other path (savestates, PRINCE.SAV, hall
 * of fame...) is a real file opened with the C library of the firmware
 * (through the ABI bridge on the Game & Watch).
 *
 * gnw_pop_glue.h maps SDLPoP's stdio calls to the pop_f*() functions below.
 * They tell both kinds of FILE* apart by checking whether the pointer is
 * one of the pool handles.
 *
 * Distributed under the GPLv3 (same license as SDLPoP).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

#include "pop_platform.h"

#define MEM_FILE_MAGIC 0x504F5046u /* "POPF" */
#define MEM_FILE_COUNT 8
#define FILENO_BASE    1000

typedef struct {
    uint32_t magic;
    bool used;
    const uint8_t *base;
    size_t size;
    size_t pos;
    bool eof;
} mem_file_t;

static mem_file_t mem_files[MEM_FILE_COUNT];
static FILE *last_real_file; /* last real FILE* given to fileno() */

static mem_file_t *as_mem(FILE *f)
{
    uintptr_t p = (uintptr_t)f;
    if (p < (uintptr_t)&mem_files[0] || p >= (uintptr_t)&mem_files[MEM_FILE_COUNT])
        return NULL;
    mem_file_t *m = (mem_file_t *)f;
    return (m->magic == MEM_FILE_MAGIC && m->used) ? m : NULL;
}

static const char *base_name(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            b = p + 1;
    return b;
}

FILE *pop_fopen(const char *filename, const char *mode)
{
    if (!filename || !mode)
        return NULL;
    if (mode[0] == 'r' && !strchr(mode, '+')) {
        const uint8_t *data;
        uint32_t size;
        /* Paths SDLPoP builds look like "data/PRINCE.DAT" or "./SDLPoP.ini" */
        if (pop_plat_find_file(base_name(filename), &data, &size)) {
            if (size == 0)
                return NULL;
            for (int i = 0; i < MEM_FILE_COUNT; i++) {
                mem_file_t *m = &mem_files[i];
                if (!m->used) {
                    *m = (mem_file_t){ MEM_FILE_MAGIC, true, data, size, 0, false };
                    return (FILE *)m;
                }
            }
            printf("pop: too many open pack files (%s)\n", filename);
            return NULL;
        }
    }
    if (mode[0] == 'r') {
        /* The game data only comes from the pack: the only real files
         * the game may read are its own saves. */
        const char *dir = pop_plat_save_dir();
        if (dir && strncmp(filename, dir, strlen(dir)) != 0)
            return NULL;
    }
    return fopen(filename, mode);
}

int pop_fclose(FILE *f)
{
    mem_file_t *m = as_mem(f);
    if (m) {
        m->used = false;
        return 0;
    }
    return f ? fclose(f) : -1;
}

size_t pop_fread(void *ptr, size_t size, size_t n, FILE *f)
{
    mem_file_t *m = as_mem(f);
    if (!m)
        return fread(ptr, size, n, f);
    if (size == 0 || n == 0)
        return 0;
    size_t avail = (m->size - m->pos) / size;
    if (n > avail) {
        n = avail;
        m->eof = true;
    }
    memcpy(ptr, m->base + m->pos, n * size);
    m->pos += n * size;
    return n;
}

size_t pop_fwrite(const void *ptr, size_t size, size_t n, FILE *f)
{
    if (as_mem(f))
        return 0; /* read-only */
    return fwrite(ptr, size, n, f);
}

int pop_fseek(FILE *f, long offset, int whence)
{
    mem_file_t *m = as_mem(f);
    if (!m)
        return fseek(f, offset, whence);
    long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)m->pos : (long)m->size;
    long pos = base + offset;
    if (pos < 0 || pos > (long)m->size)
        return -1;
    m->pos = (size_t)pos;
    m->eof = false;
    return 0;
}

long pop_ftell(FILE *f)
{
    mem_file_t *m = as_mem(f);
    return m ? (long)m->pos : ftell(f);
}

int pop_feof(FILE *f)
{
    mem_file_t *m = as_mem(f);
    if (!m)
        return feof(f);
    return m->eof || m->pos >= m->size;
}

int pop_fgetc(FILE *f)
{
    mem_file_t *m = as_mem(f);
    if (!m)
        return fgetc(f);
    if (m->pos >= m->size) {
        m->eof = true;
        return EOF;
    }
    return m->base[m->pos++];
}

int pop_fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    if (as_mem(f))
        r = -1;
    else if (f == stdout || f == stderr)
        r = vprintf(fmt, ap);
    else
        r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

void pop_perror(const char *s)
{
    printf("%s: error\n", s ? s : "");
}

/* ------------------------------------------------------------------ */
/* stat() / fstat() / fileno()                                        */
/* ------------------------------------------------------------------ */
int pop_fileno(FILE *f)
{
    mem_file_t *m = as_mem(f);
    if (m)
        return FILENO_BASE + (int)(m - mem_files);
    /* Real file: remember it so that fstat() can measure it. */
    last_real_file = f;
    return FILENO_BASE + MEM_FILE_COUNT;
}

int pop_fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    int i = fd - FILENO_BASE;
    if (i >= 0 && i < MEM_FILE_COUNT && mem_files[i].used) {
        st->st_size = (off_t)mem_files[i].size;
        st->st_mode = S_IFREG;
        return 0;
    }
    if (i == MEM_FILE_COUNT && last_real_file) {
        FILE *f = last_real_file;
        long cur = ftell(f);
        if (cur < 0 || fseek(f, 0, SEEK_END) != 0)
            return -1;
        long end = ftell(f);
        fseek(f, cur, SEEK_SET);
        st->st_size = (off_t)end;
        st->st_mode = S_IFREG;
        return 0;
    }
    return -1;
}

/* Only the pack is visible: its files are regular files, nothing else
 * exists (no data/ folders, no mods/ folder). */
int pop_stat(const char *path, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    const uint8_t *data;
    uint32_t size;
    if (path && pop_plat_find_file(base_name(path), &data, &size)) {
        st->st_size = (off_t)size;
        st->st_mode = S_IFREG;
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* fscanf(): the small subset SDLPoP uses on its text files           */
/*   literals, whitespace, %d, %Ns, %N[set], %N[^set], %* (no store)  */
/* ------------------------------------------------------------------ */
typedef struct {
    FILE *f;
    mem_file_t *m;
    int peeked; /* -2: none */
} scan_src_t;

static int src_peek(scan_src_t *s)
{
    if (s->peeked == -2)
        s->peeked = pop_fgetc(s->f);
    return s->peeked;
}

static void src_next(scan_src_t *s)
{
    if (s->peeked == -2)
        pop_fgetc(s->f);
    s->peeked = -2;
}

static void src_done(scan_src_t *s)
{
    /* give back the character we looked at but did not use */
    if (s->peeked >= 0) {
        if (s->m)
            s->m->pos--;
        else
            fseek(s->f, -1, SEEK_CUR); /* ungetc() is not on the firmware ABI */
    }
    if (s->peeked == EOF && s->m)
        s->m->eof = true;
    s->peeked = -2;
}

static bool in_set(const char *set, int set_len, bool negate, int c)
{
    bool found = false;
    for (int i = 0; i < set_len; i++) {
        if (i + 2 < set_len && set[i + 1] == '-') {
            if (c >= (unsigned char)set[i] && c <= (unsigned char)set[i + 2])
                found = true;
            i += 2;
        } else if ((unsigned char)set[i] == c) {
            found = true;
        }
    }
    return negate ? !found : found;
}

int pop_fscanf(FILE *f, const char *fmt, ...)
{
    scan_src_t s = { f, as_mem(f), -2 };
    va_list ap;
    int assigned = 0;
    int conversions = 0;        /* successful ones, stored or not */
    bool input_failure = false; /* a directive failed on end of file */
    va_start(ap, fmt);

    while (*fmt) {
        if (isspace((unsigned char)*fmt)) {
            while (src_peek(&s) != EOF && isspace(src_peek(&s)))
                src_next(&s);
            fmt++;
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {
            int want = (unsigned char)(*fmt == '%' ? '%' : *fmt);
            if (src_peek(&s) != want) {
                input_failure = (src_peek(&s) == EOF);
                break;
            }
            src_next(&s);
            fmt += (*fmt == '%') ? 2 : 1;
            continue;
        }
        fmt++; /* '%' */
        bool store = true;
        if (*fmt == '*') {
            store = false;
            fmt++;
        }
        int width = 0;
        while (isdigit((unsigned char)*fmt))
            width = width * 10 + (*fmt++ - '0');
        char conv = *fmt++;

        if (conv == 'd') {
            while (src_peek(&s) != EOF && isspace(src_peek(&s)))
                src_next(&s);
            int sign = 1, val = 0, digits = 0;
            if (src_peek(&s) == '-' || src_peek(&s) == '+') {
                sign = src_peek(&s) == '-' ? -1 : 1;
                src_next(&s);
            }
            while (src_peek(&s) != EOF && isdigit(src_peek(&s)) && (width == 0 || digits < width)) {
                val = val * 10 + (src_peek(&s) - '0');
                src_next(&s);
                digits++;
            }
            if (digits == 0) {
                input_failure = (src_peek(&s) == EOF);
                break;
            }
            conversions++;
            if (store) {
                *va_arg(ap, int *) = sign * val;
                assigned++;
            }
        } else if (conv == 's' || conv == '[') {
            const char *set = NULL;
            int set_len = 0;
            bool negate = false;
            if (conv == '[') {
                if (*fmt == '^') {
                    negate = true;
                    fmt++;
                }
                set = fmt;
                if (*fmt == ']')
                    fmt++; /* "]" first in the set is a literal */
                while (*fmt && *fmt != ']')
                    fmt++;
                set_len = (int)(fmt - set);
                if (*fmt == ']')
                    fmt++;
            } else {
                while (src_peek(&s) != EOF && isspace(src_peek(&s)))
                    src_next(&s);
            }
            char *out = store ? va_arg(ap, char *) : NULL;
            int n = 0;
            while (src_peek(&s) != EOF && (width == 0 || n < width)) {
                int c = src_peek(&s);
                bool ok = (conv == 's') ? !isspace(c) : in_set(set, set_len, negate, c);
                if (!ok)
                    break;
                if (out)
                    out[n] = (char)c;
                n++;
                src_next(&s);
            }
            if (n == 0) {
                input_failure = (src_peek(&s) == EOF);
                break;
            }
            conversions++;
            if (out) {
                out[n] = '\0';
                assigned++;
            }
        } else {
            break; /* unsupported conversion */
        }
    }
    va_end(ap);
    src_done(&s);
    /* Like the C library: EOF when the input ends before the first
     * conversion, whitespace reaching the end of the file is not a failure. */
    if (input_failure && conversions == 0)
        return EOF;
    return assigned;
}
