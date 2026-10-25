/*
 * Headless Linux harness for the Prince of Persia core.
 *
 * Runs SDLPoP through exactly the same SDL shim, heap and asset pack code as
 * the Game & Watch, with a virtual clock (the game runs as fast as the host
 * can), scripted input and frame/audio dumps:
 *
 *   ./pop_host <pack.pop> [options]
 *     -t <ms>          stop after <ms> of game time (default 30000)
 *     -s <script>      key script: "ms:KEY:1,ms:KEY:0,..." (KEY = RETURN, LEFT,
 *                      RIGHT, UP, DOWN, LSHIFT, LCTRL, ESCAPE, SPACE, A..Z, ...)
 *     -S <ms,ms,...>   write frame_<ms>.ppm at these times
 *     -e <ms>          also write a frame every <ms>
 *     -w <file.wav>    write the audio output
 *     -o <dir>         output directory (default .)
 *     -H <kb>          heap size in KB (default 640)
 *     -l <level>       start at this level (cheat, like "prince 3")
 *     -q <file>        quicksave test: save to <file> at the time given by -Q
 *     -Q <ms>          time of the quicksave
 *     -L <file>        load this quicksave right after the start
 *     -R <dir>         render every MIDI music to <dir>/MIDInn.raw, then quit
 *     -M <rate>        with -R: rate of the .raw files (22050, or 11025: half
 *                      rate, low-pass filtered)
 *     -W <ms>          at <ms>, go to the winning room: the end sequence and
 *                      its Hall of Fame name entry
 *     -F <ms>          slow device: every frame of a level costs <ms> more
 *                      (the G&W can be slower than the 12 fps game timer)
 *
 * The quicksave of -q/-Q is made like on the device: the pause menu opens at
 * the first service tick after <ms> where the platform may run it (see
 * pop_core_menu_allowed()), and the save fails if the game is not at a point
 * where it can be saved.
 *
 * Distributed under the GPLv3 (same license as SDLPoP).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

#include "pop_platform.h"
#include "pop_pack.h"
#include "pop_core.h"

static uint64_t now_us;
static uint64_t end_us = 30000000ull;
static char out_dir[256] = ".";
static const uint16_t *last_frame;
static int last_pitch;
static uint64_t frames_presented;

/* scripted keys */
typedef struct { uint64_t t; int sc; int down; } key_ev_t;
static key_ev_t keys[512];
static int n_keys, next_key;

/* screenshots */
static uint64_t shots[128];
static int n_shots, next_shot;
static uint64_t shot_every, next_every;

/* audio */
static FILE *wav;
static uint64_t audio_frames_done;
static uint32_t wav_bytes;
#define RATE 22050

/* quicksave test */
static const char *qs_save_path;
static uint64_t qs_save_at;
static const char *qs_load_path;
static bool qs_saved;

/* -W: jump to the winning room (check_the_end, seg000.c GNW) */
extern int gnw_test_win;
static uint64_t win_at;

static uint8_t *pack;
static size_t pack_size;

uint64_t pop_plat_time_us(void) { return now_us; }
int pop_plat_audio_rate(void) { return RATE; }

static struct { const char *n; int sc; } names[] = {
    {"RETURN", 40}, {"ESCAPE", 41}, {"BACKSPACE", 42}, {"TAB", 43}, {"SPACE", 44},
    {"RIGHT", 79}, {"LEFT", 80}, {"DOWN", 81}, {"UP", 82}, {"HOME", 74}, {"PAGEUP", 75},
    {"LCTRL", 224}, {"LSHIFT", 225}, {"LALT", 226}, {"RSHIFT", 229}, {"RCTRL", 228},
    {"F6", 63}, {"F9", 66}, {NULL, 0}
};

static int parse_key(const char *s)
{
    for (int i = 0; names[i].n; i++)
        if (strcmp(names[i].n, s) == 0)
            return names[i].sc;
    if (strlen(s) == 1 && s[0] >= 'A' && s[0] <= 'Z')
        return 4 + (s[0] - 'A');
    if (strlen(s) == 1 && s[0] >= '1' && s[0] <= '9')
        return 30 + (s[0] - '1');
    if (strcmp(s, "0") == 0)
        return 39;
    fprintf(stderr, "unknown key %s\n", s);
    exit(2);
}

static void write_ppm(const char *name)
{
    if (!last_frame)
        return;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n320 200\n255\n");
    for (int y = 0; y < 200; y++) {
        const uint16_t *row = (const uint16_t *)((const uint8_t *)last_frame + y * last_pitch);
        for (int x = 0; x < 320; x++) {
            uint16_t p = row[x];
            uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 0x1F) << 3), (uint8_t)(((p >> 5) & 0x3F) << 2), (uint8_t)((p & 0x1F) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

static void wav_header(uint32_t data_bytes)
{
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    uint32_t v = 36 + data_bytes; memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    uint16_t s = 1; memcpy(h + 20, &s, 2);
    s = 1; memcpy(h + 22, &s, 2);
    v = RATE; memcpy(h + 24, &v, 4);
    v = RATE * 2; memcpy(h + 28, &v, 4);
    s = 2; memcpy(h + 32, &s, 2);
    s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_bytes, 4);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
    fseek(wav, 0, SEEK_END);
}

static void __attribute__((noreturn)) finish(int code)
{
    char name[64];
    snprintf(name, sizeof(name), "frame_end.ppm");
    write_ppm(name);
    if (wav) {
        wav_header(wav_bytes);
        fclose(wav);
    }
#ifdef POP_TRACE
    { extern void pop_core_trace(const char *); pop_core_trace("end"); }
#endif
    printf("pop_host: %llu ms, %llu frames presented, heap used %zu / peak %zu / size %zu\n",
           (unsigned long long)(now_us / 1000), (unsigned long long)frames_presented,
           pop_heap_used(), pop_heap_peak(), pop_heap_size());
    exit(code);
}

static int in_service;
static uint64_t frame_cost_us;
static uint64_t last_menu_tick;

/* The device runs its input loop (pause menu) every 16 ms of service. */
static void menu_tick(bool safe_point)
{
    if (!safe_point && now_us - last_menu_tick < 16000)
        return;
    if (!pop_core_menu_allowed())
        return; /* deferred to the next safe point, like main_pop.c */
    last_menu_tick = now_us;
    if (qs_save_path && !qs_saved && now_us >= qs_save_at) {
        qs_saved = true;
        bool ok = pop_core_save_state(qs_save_path);
        printf("pop_host: saving %s at %llu ms: %s\n", qs_save_path,
               (unsigned long long)(now_us / 1000), ok ? "ok" : "FAILED");
    }
}

static void service(int in_delay, bool safe_point);

void pop_plat_safe_point(void)
{
    service(0, true);
}

void pop_plat_service(int in_delay)
{
    service(in_delay, false);
}

static void service(int in_delay, bool safe_point)
{
    if (in_service)
        return;
    in_service = 1;
    now_us += in_delay ? 1000 : 50;

    while (next_key < n_keys && keys[next_key].t <= now_us) {
        pop_sdl_key(keys[next_key].sc, keys[next_key].down);
        next_key++;
    }
    while (next_shot < n_shots && shots[next_shot] <= now_us) {
        char name[64];
        snprintf(name, sizeof(name), "frame_%06llu.ppm", (unsigned long long)(shots[next_shot] / 1000));
        write_ppm(name);
        next_shot++;
    }
    if (shot_every && now_us >= next_every) {
        char name[64];
        snprintf(name, sizeof(name), "frame_%06llu.ppm", (unsigned long long)(next_every / 1000));
        write_ppm(name);
        next_every += shot_every;
    }
    /* audio: keep the output in sync with the virtual clock */
    uint64_t due = now_us * RATE / 1000000ull;
    while (audio_frames_done + 256 <= due) {
        int16_t buf[256];
        pop_sdl_audio_render(buf, 256);
        if (wav) {
            fwrite(buf, 2, 256, wav);
            wav_bytes += 512;
        }
        audio_frames_done += 256;
    }
    if (win_at && now_us >= win_at) {
        win_at = 0;
        gnw_test_win = 1;
    }
    menu_tick(safe_point);
#ifdef POP_TRACE
    {
        extern int gnw_level_active;
        static int last_active = -1;
        if (gnw_level_active != last_active) {
            printf("TRACE %llu ms: level_active=%d\n", (unsigned long long)(now_us / 1000), gnw_level_active);
            last_active = gnw_level_active;
        }
    }
#endif
    in_service = 0;
    if (now_us >= end_us)
        finish(0);
}

void pop_plat_present(const uint16_t *pixels, int pitch)
{
    last_frame = pixels;
    last_pitch = pitch;
    frames_presented++;
    /* slow device simulation: only the frames of a level (the cutscene
     * fades present many times per tick and would never end) */
    extern int gnw_level_active;
    if (gnw_level_active)
        now_us += frame_cost_us;
}

bool pop_plat_find_file(const char *name, const uint8_t **data, uint32_t *size)
{
    return pop_pack_find(name, data, size);
}

const char *pop_plat_save_dir(void)
{
    static char dir[300];
    snprintf(dir, sizeof(dir), "%s/save", out_dir);
    mkdir(dir, 0755);
    return dir;
}

void pop_plat_quit(int exit_code)
{
    printf("pop_host: game quit (%d)\n", exit_code);
    finish(exit_code);
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    size_t heap_kb = 640;
    int start_level = -1;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <pack.pop> [options]\n", argv[0]);
        return 2;
    }
    const char *pack_path = argv[1];
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "-t") && v) { end_us = strtoull(v, NULL, 10) * 1000; i++; }
        else if (!strcmp(a, "-o") && v) { snprintf(out_dir, sizeof(out_dir), "%s", v); i++; }
        else if (!strcmp(a, "-H") && v) { heap_kb = strtoul(v, NULL, 10); i++; }
        else if (!strcmp(a, "-l") && v) { start_level = atoi(v); i++; }
        else if (!strcmp(a, "-w") && v) { wav = fopen(v, "wb"); if (wav) { uint8_t z[44] = {0}; fwrite(z, 1, 44, wav); } i++; }
        else if (!strcmp(a, "-e") && v) { shot_every = strtoull(v, NULL, 10) * 1000; next_every = shot_every; i++; }
        else if (!strcmp(a, "-q") && v) { qs_save_path = v; i++; }
        else if (!strcmp(a, "-Q") && v) { qs_save_at = strtoull(v, NULL, 10) * 1000; i++; }
        else if (!strcmp(a, "-L") && v) { qs_load_path = v; i++; }
        else if (!strcmp(a, "-R") && v) { pop_core_set_render_dir(v); i++; }
        else if (!strcmp(a, "-M") && v) { pop_core_set_render_half_rate(atoi(v) < RATE); i++; }
        else if (!strcmp(a, "-W") && v) { win_at = strtoull(v, NULL, 10) * 1000; i++; }
        else if (!strcmp(a, "-F") && v) { frame_cost_us = strtoull(v, NULL, 10) * 1000; i++; }
        else if (!strcmp(a, "-S") && v) {
            char *s = strdup(v);
            for (char *tok = strtok(s, ","); tok && n_shots < 128; tok = strtok(NULL, ","))
                shots[n_shots++] = strtoull(tok, NULL, 10) * 1000;
            qsort(shots, n_shots, sizeof(shots[0]), cmp_u64);
            i++;
        } else if (!strcmp(a, "-s") && v) {
            char *s = strdup(v);
            for (char *tok = strtok(s, ","); tok && n_keys < 512; tok = strtok(NULL, ",")) {
                char key[32];
                unsigned long long t;
                int down;
                if (sscanf(tok, "%llu:%31[^:]:%d", &t, key, &down) != 3) {
                    fprintf(stderr, "bad script item %s\n", tok);
                    return 2;
                }
                keys[n_keys].t = t * 1000;
                keys[n_keys].sc = parse_key(key);
                keys[n_keys].down = down;
                n_keys++;
            }
            i++;
        } else {
            fprintf(stderr, "unknown option %s\n", a);
            return 2;
        }
    }

    FILE *f = fopen(pack_path, "rb");
    if (!f) {
        perror(pack_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    pack_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    pack = malloc(pack_size);
    if (fread(pack, 1, pack_size, f) != pack_size)
        return 1;
    fclose(f);
    int r = pop_pack_open(pack, (uint32_t)pack_size);
    if (r != POP_PACK_OK) {
        fprintf(stderr, "invalid pack (%d)\n", r);
        return 1;
    }

    static uint8_t *heap;
    heap = malloc(heap_kb * 1024);
    pop_heap_init(heap, heap_kb * 1024);
    pop_heap_ignore_range(pack, pack + pack_size);

    pop_core_options_t opts;
    pop_core_default_options(&opts);
    opts.start_level = start_level;
    opts.load_state = qs_load_path; /* like the device: restored once the level runs */
    pop_core_run(&opts); /* does not return */
    return 0;
}
