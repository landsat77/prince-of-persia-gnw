/*
 * C library functions SDLPoP needs that the Retro-Go firmware ABI does not
 * export. Only built for the Game & Watch (the Linux harness uses glibc).
 *
 * Distributed under the GPLv3 (same license as SDLPoP).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

/* options.c parses numbers from the .ini files */
intmax_t strtoimax(const char *nptr, char **endptr, int base)
{
    return (intmax_t)strtol(nptr, endptr, base);
}

/* midi.c: note frequencies / volume curves. The ABI has pow() (double). */
float powf(float x, float y)
{
    return (float)pow((double)x, (double)y);
}

float log2f(float x)
{
    /* log2(x) = log10(x) / log10(2) */
    return (float)(log10((double)x) * 3.32192809488736234787);
}

/* seg000.c: analog joystick direction. There is no analog stick on the
 * Game & Watch, but keep a correct implementation (max error ~0.005 rad). */
double atan2(double y, double x)
{
    const double pi = 3.14159265358979323846;
    if (x == 0.0) {
        if (y > 0.0) return pi / 2;
        if (y < 0.0) return -pi / 2;
        return 0.0;
    }
    double ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    double a = (ax > ay) ? ay / ax : ax / ay; /* 0..1 */
    double s = a * a;
    double r = ((-0.0464964749 * s + 0.15931422) * s - 0.327622764) * s * a + a;
    if (ay > ax) r = pi / 2 - r;
    if (x < 0) r = pi - r;
    if (y < 0) r = -r;
    return r;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}
