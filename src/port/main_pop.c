/*
 * Prince of Persia for the Game & Watch (Retro-Go SD), as a GWHB homebrew.
 *
 * The game itself is SDLPoP (src/sdlpop), an open source port of the DOS
 * version, running on a minimal SDL2 layer (pop_sdl.c). This file is the
 * retro-go side: it maps the asset pack (the .pop file next to this .bin,
 * see tools/pop_pack.py) from the SD card into the external flash, gives the
 * free RAM to the game's heap, and implements the platform hooks (display,
 * audio, buttons, menu, savestates).
 *
 * Everything that reaches the firmware goes through the ABI bridge
 * (sdk/src/gw_core_bridge.*): this binary is not linked with the firmware.
 *
 * SDLPoP keeps control of its main loop: whenever it waits (SDL_Delay) or
 * polls events, pop_plat_service() runs the retro-go duties: audio DMA
 * refill, button reading, pause menu, overlays and frame presentation.
 *
 * Distributed under the GPLv3 (same license as SDLPoP).
 */
#include <odroid_system.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"
#include "gw_lcd.h"
#include "gw_buttons.h"
#include "gw_audio.h"
#include "gw_malloc.h"
#include "common.h"
#include "rom_manager.h"
#include "appid.h"
#include "rg_storage.h"
#include "odroid_overlay.h"
#include "odroid_settings.h"

#include "pop_platform.h"
#include "pop_pack.h"
#include "pop_core.h"

#include "gw_core_i18n.h"
#include "gw_core_bridge.h" /* last: rewrites ACTIVE_FILE, common_emu_state, dma_counter */

#define POP_SAMPLE_RATE         22050
#define POP_AUDIO_BUFFER_LENGTH 368      /* ~16.7 ms per DMA half buffer */
#define POP_TICK_MS             16       /* buttons / menu / overlay rate */

#define POP_WIDTH  320
#define POP_HEIGHT 200

/* SDLPoP scancodes (SDL2 values) */
enum {
    SC_RETURN = 40, SC_ESCAPE = 41, SC_SPACE = 44,
    SC_RIGHT = 79, SC_LEFT = 80, SC_DOWN = 81, SC_UP = 82,
    SC_LCTRL = 224, SC_LSHIFT = 225,
};

static odroid_gamepad_state_t joystick;

static const uint16_t *frame_pixels;
static int frame_pitch;
static bool frame_dirty;

static uint32_t last_dma_counter;
static uint32_t last_tick_ms;
static int in_service;
static bool game_running;

static char *boot_state_path;
static char save_dir[160];
static char options_path[200];

/* Options (saved per pack) */
typedef struct {
    uint32_t magic;
    uint8_t music;
    uint8_t sound;
    uint8_t fixes;
    uint8_t reserved;
} pop_options_t;
#define POP_OPTIONS_MAGIC 0x504F5031 /* "POP1" */
static pop_options_t options = { POP_OPTIONS_MAGIC, 1, 1, 1, 0 };

/* ------------------------------------------------------------------ */
/* Options                                                            */
/* ------------------------------------------------------------------ */
static void options_load(void)
{
    FILE *f = fopen(options_path, "rb");
    if (!f)
        return;
    pop_options_t o;
    if (fread(&o, sizeof(o), 1, f) == 1 && o.magic == POP_OPTIONS_MAGIC)
        options = o;
    fclose(f);
}

static void options_save(void)
{
    FILE *f = fopen(options_path, "wb");
    if (!f)
        return;
    fwrite(&options, sizeof(options), 1, f);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Platform: time, files                                              */
/* ------------------------------------------------------------------ */
uint64_t pop_plat_time_us(void)
{
    return (uint64_t)get_elapsed_time() * 1000ull;
}

int pop_plat_audio_rate(void)
{
    return POP_SAMPLE_RATE;
}

bool pop_plat_find_file(const char *name, const uint8_t **data, uint32_t *size)
{
    return pop_pack_find(name, data, size);
}

const char *pop_plat_save_dir(void)
{
    return save_dir;
}

void pop_plat_quit(int exit_code)
{
    printf("pop: game quit (%d)\n", exit_code);
    audio_stop_playing();
    odroid_system_switch_app(0);
}

/* ------------------------------------------------------------------ */
/* Display                                                            */
/* ------------------------------------------------------------------ */
static inline uint16_t blend565(uint16_t a, uint16_t b, uint32_t w /* 0..256 weight of b */)
{
    /* spread RGB565 so that the three channels can be blended at once */
    uint32_t x = (a | ((uint32_t)a << 16)) & 0x07E0F81F;
    uint32_t y = (b | ((uint32_t)b << 16)) & 0x07E0F81F;
    uint32_t r = (x + (((y - x) * (w >> 3)) >> 5)) & 0x07E0F81F;
    return (uint16_t)(r | (r >> 16));
}

/* Draw the 320x200 game screen into a 320x240 framebuffer. */
static void draw_frame(uint16_t *fb)
{
    if (!frame_pixels) {
        memset(fb, 0, GW_LCD_WIDTH * GW_LCD_HEIGHT * 2);
        return;
    }
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
    if (scaling == ODROID_DISPLAY_SCALING_OFF) {
        /* 1:1, letterboxed */
        int top = (GW_LCD_HEIGHT - POP_HEIGHT) / 2;
        memset(fb, 0, top * GW_LCD_WIDTH * 2);
        for (int y = 0; y < POP_HEIGHT; y++)
            memcpy(fb + (top + y) * GW_LCD_WIDTH, (const uint8_t *)frame_pixels + y * frame_pitch, POP_WIDTH * 2);
        memset(fb + (top + POP_HEIGHT) * GW_LCD_WIDTH, 0, (GW_LCD_HEIGHT - top - POP_HEIGHT) * GW_LCD_WIDTH * 2);
        return;
    }
    /* 320x200 -> 320x240: the 4:3 aspect ratio of the original DOS game */
    bool smooth = odroid_display_get_filter_mode() != ODROID_DISPLAY_FILTER_OFF;
    for (int y = 0; y < GW_LCD_HEIGHT; y++) {
        uint16_t *dst = fb + y * GW_LCD_WIDTH;
        uint32_t pos = (uint32_t)y * POP_HEIGHT * 256 / GW_LCD_HEIGHT; /* 8.8 fixed point */
        int sy = pos >> 8;
        uint32_t frac = pos & 0xFF;
        const uint16_t *s0 = (const uint16_t *)((const uint8_t *)frame_pixels + sy * frame_pitch);
        if (!smooth || frac == 0 || sy + 1 >= POP_HEIGHT) {
            memcpy(dst, s0, POP_WIDTH * 2);
        } else {
            const uint16_t *s1 = (const uint16_t *)((const uint8_t *)s0 + frame_pitch);
            for (int x = 0; x < POP_WIDTH; x++) {
                uint16_t a = s0[x], b = s1[x];
                dst[x] = (a == b) ? a : blend565(a, b, frac);
            }
        }
    }
}

static void flush_frame(void)
{
    if (lcd_is_swap_pending())
        return;
    uint16_t *fb = lcd_get_active_buffer();
    draw_frame(fb);
    common_ingame_overlay();
    lcd_swap();
    frame_dirty = false;
}

void pop_plat_present(const uint16_t *pixels, int pitch)
{
    frame_pixels = pixels;
    frame_pitch = pitch;
    frame_dirty = true;
    flush_frame(); /* if a swap is still pending, the next service tick shows it */
}

static void repaint(void)
{
    uint16_t *fb = lcd_get_active_buffer();
    draw_frame(fb);
    common_ingame_overlay();
}

static void *screenshot(void)
{
    lcd_wait_for_vblank();
    uint16_t *fb = lcd_get_active_buffer();
    draw_frame(fb);
    return fb;
}

/* ------------------------------------------------------------------ */
/* Audio                                                              */
/* ------------------------------------------------------------------ */
static void feed_audio(void)
{
    if (dma_counter == last_dma_counter)
        return;
    last_dma_counter = dma_counter;
    int16_t *buf = audio_get_active_buffer();
    uint16_t len = audio_get_buffer_length();
    if (common_emu_sound_loop_is_muted()) {
        /* keep the game's sound state moving */
        pop_sdl_audio_render(buf, len);
        audio_clear_active_buffer();
        return;
    }
    pop_sdl_audio_render(buf, len);
    int32_t factor = common_emu_sound_get_volume();
    for (int i = 0; i < len; i++)
        buf[i] = (int16_t)(((int32_t)buf[i] * factor) >> 8);
}

/* ------------------------------------------------------------------ */
/* Pause menu                                                         */
/* ------------------------------------------------------------------ */
static char music_value[12], sound_value[12], fixes_value[12];

static const gw_i18n_entry_t i18n_yes[] = {
    { "en", "Yes" }, { "fr", "Oui" }, { "es", "S\xc3\xad" }, { "de", "Ja" }, { "it", "S\xc3\xac" }, { "pt", "Sim" }, GW_I18N_END
};
static const gw_i18n_entry_t i18n_no[] = {
    { "en", "No" }, { "fr", "Non" }, { "es", "No" }, { "de", "Nein" }, { "it", "No" }, { "pt", "N\xc3\xa3o" }, GW_I18N_END
};
static const gw_i18n_entry_t i18n_music[] = {
    { "en", "Music" }, { "fr", "Musique" }, { "es", "M\xc3\xbasica" }, { "de", "Musik" }, { "it", "Musica" }, { "pt", "M\xc3\xbasica" }, GW_I18N_END
};
static const gw_i18n_entry_t i18n_sound[] = {
    { "en", "Sound" }, { "fr", "Son" }, { "es", "Sonido" }, { "de", "Ton" }, { "it", "Suono" }, { "pt", "Som" }, GW_I18N_END
};
static const gw_i18n_entry_t i18n_fixes[] = {
    { "en", "Bug fixes" }, { "fr", "Corrections" }, { "es", "Correcciones" }, { "de", "Fehlerkorrekturen" }, { "it", "Correzioni" }, { "pt", "Corre\xc3\xa7\xc3\xb5" "es" }, GW_I18N_END
};
static const gw_i18n_entry_t i18n_restart[] = {
    { "en", "Restart level" }, { "fr", "Recommencer le niveau" }, { "es", "Reiniciar nivel" }, { "de", "Level neu starten" }, { "it", "Ricomincia livello" }, { "pt", "Reiniciar n\xc3\xadvel" }, GW_I18N_END
};
static const gw_i18n_entry_t i18n_no_pack[] = {
    { "en", "Game data not found:" }, { "fr", "Donn\xc3\xa9" "es du jeu introuvables :" }, GW_I18N_END
};
static const gw_i18n_entry_t i18n_bad_pack[] = {
    { "en", "Not a valid .pop file:" }, { "fr", "Fichier .pop invalide :" }, GW_I18N_END
};

static void bool_label(char *dst, bool v)
{
    strncpy(dst, gw_i18n(v ? i18n_yes : i18n_no), 11);
    dst[11] = '\0';
}

static bool music_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT || event == ODROID_DIALOG_ENTER) {
        options.music = !options.music;
        pop_core_set_music(options.music);
        options_save();
    }
    bool_label(option->value, options.music);
    return false;
}

static bool sound_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT || event == ODROID_DIALOG_ENTER) {
        options.sound = !options.sound;
        pop_core_set_sound(options.sound);
        options_save();
    }
    bool_label(option->value, options.sound);
    return false;
}

static bool fixes_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT || event == ODROID_DIALOG_ENTER) {
        options.fixes = !options.fixes;
        pop_core_set_fixes(options.fixes);
        options_save();
    }
    bool_label(option->value, options.fixes);
    return false;
}

static bool restart_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_ENTER)
        pop_core_restart_level();
    return event == ODROID_DIALOG_ENTER;
}

static odroid_dialog_choice_t game_options[5];

static void setup_game_options(void)
{
    /* Labels are looked up once, in the firmware's current language. */
    game_options[0] = (odroid_dialog_choice_t){300, gw_i18n(i18n_music), music_value, 1, &music_cb};
    game_options[1] = (odroid_dialog_choice_t){301, gw_i18n(i18n_sound), sound_value, 1, &sound_cb};
    game_options[2] = (odroid_dialog_choice_t){302, gw_i18n(i18n_fixes), fixes_value, 1, &fixes_cb};
    game_options[3] = (odroid_dialog_choice_t){303, gw_i18n(i18n_restart), NULL, 1, &restart_cb};
    game_options[4] = (odroid_dialog_choice_t)ODROID_DIALOG_CHOICE_LAST;
}

/* ------------------------------------------------------------------ */
/* Buttons                                                            */
/* ------------------------------------------------------------------ */
/*
 * Game & Watch      Prince of Persia
 *   D-pad            arrows (run, climb, crouch, ...)
 *   A                Shift (action: grab ledges, pick up, strike, careful step)
 *   B                Up (jump)
 *   GAME / START     Esc (pause the game) / Enter
 *   TIME / SELECT    Space (show the remaining time)
 *   PAUSE/SET        retro-go menu (savestates, options, ...)
 */
static void update_keys(const odroid_gamepad_state_t *j)
{
    pop_sdl_key(SC_LEFT, j->values[ODROID_INPUT_LEFT]);
    pop_sdl_key(SC_RIGHT, j->values[ODROID_INPUT_RIGHT]);
    pop_sdl_key(SC_UP, j->values[ODROID_INPUT_UP] || j->values[ODROID_INPUT_B]);
    pop_sdl_key(SC_DOWN, j->values[ODROID_INPUT_DOWN]);
    pop_sdl_key(SC_LSHIFT, j->values[ODROID_INPUT_A]);
    pop_sdl_key(SC_ESCAPE, j->values[ODROID_INPUT_START] || j->values[ODROID_INPUT_X]);
    pop_sdl_key(SC_SPACE, j->values[ODROID_INPUT_SELECT] || j->values[ODROID_INPUT_Y]);
}

/* ------------------------------------------------------------------ */
/* Service: called whenever the game waits or polls events            */
/* ------------------------------------------------------------------ */
void pop_plat_service(int in_delay)
{
    if (in_service)
        return;
    in_service = 1;

    wdog_refresh();
    feed_audio();

    uint32_t now = get_elapsed_time();
    if (now - last_tick_ms >= POP_TICK_MS && !lcd_is_swap_pending()) {
        last_tick_ms = now;

        bool_label(music_value, options.music);
        bool_label(sound_value, options.sound);
        bool_label(fixes_value, options.fixes);

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, game_options, &repaint);
        update_keys(&joystick);

        /* last_tick_ms is refreshed after a menu so that we don't rush */
        last_tick_ms = get_elapsed_time();
        last_dma_counter = dma_counter;

        if (frame_dirty || common_emu_state.overlay != 0 || common_emu_state.clear_frames)
            frame_dirty = true;
    }
    if (frame_dirty)
        flush_frame();

    in_service = 0;
    if (in_delay)
        __WFI(); /* SysTick (1 kHz) or the audio DMA wake us up */
}

/* ------------------------------------------------------------------ */
/* Savestates                                                         */
/* ------------------------------------------------------------------ */
static bool pop_load_state(const char *path)
{
    if (!game_running) {
        /* Called before the game starts: restore once the level is loaded. */
        free(boot_state_path);
        boot_state_path = strdup(path);
        return true;
    }
    return pop_core_load_state(path);
}

static bool pop_save_state(const char *path)
{
    return pop_core_save_state(path);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */
static void show_error(const char *msg, const char *detail)
{
    printf("pop: %s %s\n", msg, detail);
    odroid_dialog_choice_t choices[] = {
        {0, msg, "", -1, NULL},
        {0, detail, "", -1, NULL},
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {1, "OK", "", 1, NULL},
        ODROID_DIALOG_CHOICE_LAST,
    };
    lcd_clear_buffers();
    odroid_overlay_dialog("Prince of Persia", choices, 3, NULL, 0);
    odroid_system_switch_app(0);
}

/* "/roms/homebrew/Prince of Persia.bin" -> "/roms/homebrew/Prince of Persia.pop" */
static void pack_path_for(const char *bin_path, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", bin_path);
    char *slash = strrchr(out, '/');
    char *dot = strrchr(out, '.');
    if (dot && (!slash || dot > slash))
        *dot = '\0';
    size_t len = strlen(out);
    snprintf(out + len, out_size - len, ".pop");
}

void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    gw_core_bridge_init();
    printf("Prince of Persia start\n");

    odroid_system_init(APPID_HOMEBREW, POP_SAMPLE_RATE);
    odroid_system_emu_init(&pop_load_state, &pop_save_state, &screenshot, NULL, NULL, NULL, NULL);
    setup_game_options();

    /* Same name as this .bin: several packs (mods) = several copies of the
     * .bin, each with its own .pop. */
    const char *bin_path = ACTIVE_FILE->path;
    static char pack_path[256];
    pack_path_for(bin_path, pack_path, sizeof(pack_path));
    const char *pack_name = strrchr(pack_path, '/');
    pack_name = pack_name ? pack_name + 1 : pack_path;

    /* Folder for SDLPoP's own files (options, PRINCE.SAV, PRINCE.HOF) */
    {
        char name[64];
        snprintf(name, sizeof(name), "%s", pack_name);
        char *dot = strrchr(name, '.');
        if (dot)
            *dot = '\0';
        rg_storage_mkdir(ODROID_BASE_PATH_SAVES "/pop");
        snprintf(save_dir, sizeof(save_dir), ODROID_BASE_PATH_SAVES "/pop/%s", name);
        rg_storage_mkdir(save_dir);
        snprintf(options_path, sizeof(options_path), "%s/gnw_options.bin", save_dir);
        options_load();
    }

    /* Map the asset pack into the external flash */
    FILE *probe = fopen(pack_path, "rb");
    if (!probe) {
        show_error(gw_i18n(i18n_no_pack), pack_name);
        return;
    }
    fclose(probe);
    uint32_t pack_size = 0;
    uint8_t *pack = odroid_overlay_cache_file_in_flash(pack_path, &pack_size, false);
    int r = pack ? pop_pack_open(pack, pack_size) : POP_PACK_ERR_MAGIC;
    if (r != POP_PACK_OK) {
        show_error(gw_i18n(i18n_bad_pack), pack_name);
        return;
    }

    /* Everything left in RAM_EMU is the game's heap */
    size_t heap_size = ram_get_free_size();
    void *heap = ram_malloc(heap_size);
    pop_heap_init(heap, heap_size);
    printf("pop: pack %lu bytes, heap %u bytes\n", (unsigned long)pack_size, (unsigned)heap_size);

    if (load_state)
        odroid_system_emu_load_state(save_slot); /* only records the path */

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / 60 + 0.5f);

    lcd_clear_buffers();
    audio_start_playing(POP_AUDIO_BUFFER_LENGTH);
    last_dma_counter = dma_counter;
    last_tick_ms = get_elapsed_time();

    pop_core_options_t opts;
    pop_core_default_options(&opts);
    opts.music = options.music;
    opts.sound = options.sound;
    opts.fixes = options.fixes;
    opts.load_state = boot_state_path;
    game_running = true;
    pop_core_run(&opts); /* never returns */
}
