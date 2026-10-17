/*
 * Platform independent part of the Prince of Persia core (see pop_core.h).
 * Compiled with the SDLPoP headers (it uses the game's globals).
 *
 * Distributed under the GPLv3 (same license as SDLPoP).
 */
#include "sdlpop.h"
#include "pop_core.h"

/* Set by play_level_2() (seg003.c, GNW patch) around the wait between two
 * frames of the main game loop. */
int gnw_in_level_loop;

static pop_core_options_t core_opts;
static char pending_load_path[256];
static bool pending_load;
static bool boot_load;

/* SDLPoP quicksave internals (seg000.c) */
extern FILE *quick_fp;
extern const char quick_version[];
extern char quick_control[];
typedef int process_func_type(void *data, size_t data_size);
int process_save(void *data, size_t data_size);
int process_load(void *data, size_t data_size);
int quick_process(process_func_type process_func);

#define QUICK_VERSION_SIZE 9 /* COUNT(quick_version), "V1.16b4 " + NUL */

void pop_core_default_options(pop_core_options_t *o)
{
    memset(o, 0, sizeof(*o));
    o->start_level = -1;
    o->fixes = true;
    o->music = true;
    o->sound = true;
    o->load_state = NULL;
}

/* Read the level number stored in a savestate, -1 if it is not valid. */
static int savestate_level(const char *path)
{
    FILE *f = (fopen)(path, "rb");
    if (!f)
        return -1;
    char version[QUICK_VERSION_SIZE];
    int lvl = -1;
    if (fread(version, 1, sizeof(version), f) == sizeof(version) && memcmp(version, quick_version, sizeof(version)) == 0) {
        /* level, checkpoint, upside_down, drawn_room, current_level */
        long offset = (long)(sizeof(version) + sizeof(level) + sizeof(checkpoint) + sizeof(upside_down) + sizeof(drawn_room));
        word cl;
        if (fseek(f, offset, SEEK_SET) == 0 && fread(&cl, sizeof(cl), 1, f) == 1 && cl <= 15)
            lvl = cl;
    }
    fclose(f);
    return lvl;
}

/* Called by SDLPoP after its options are loaded (GNW patch in pop_main). */
void gnw_pop_apply_options(void)
{
    enable_info_screen = 0; /* the SDLPoP splash talks about SDLPoP.ini and the keyboard */
    enable_copyprot = 0;
    enable_quicksave = 1;   /* savestates use the quicksave code */
    enable_replay = 0;
    turn_fixes_and_enhancements_on_off(core_opts.fixes ? 1 : 0);
    enable_music = core_opts.music ? 1 : 0;
    turn_sound_on_off(core_opts.sound ? 15 : 0);
    if (core_opts.start_level >= 0)
        start_level = core_opts.start_level;
    if (core_opts.load_state) {
        int lvl = savestate_level(core_opts.load_state);
        if (lvl >= 0) {
            /* Start the level of the savestate, then restore it as soon as
             * the level runs (first check_quick_op() call). */
            snprintf(pending_load_path, sizeof(pending_load_path), "%s", core_opts.load_state);
            pending_load = true;
            boot_load = true;
            start_level = lvl;
        } else {
            printf("pop: invalid savestate %s\n", core_opts.load_state);
        }
    }
}

static bool do_load(const char *path)
{
    quick_fp = (fopen)(path, "rb");
    if (quick_fp == NULL)
        return false;
    process_load(quick_control, QUICK_VERSION_SIZE);
    if (memcmp(quick_control, quick_version, QUICK_VERSION_SIZE) != 0) {
        fclose(quick_fp);
        quick_fp = NULL;
        return false;
    }
    stop_sounds();
    draw_rect(&screen_rect, color_0_black);
    int ok = quick_process(process_load);
    fclose(quick_fp);
    quick_fp = NULL;
    restore_room_after_quick_load();
    update_screen();
#ifdef POP_TRACE
    printf("TRACE load: lvl=%d room=%d x=%d y=%d frame=%d rem=%d\n", current_level, Kid.room, Kid.x, Kid.y, Kid.frame, rem_min);
#endif
    return ok != 0;
}

/* Called at the start of every frame of a level (GNW patch in check_quick_op). */
void gnw_pop_safe_point(void)
{
    if (pending_load) {
        pending_load = false;
        bool ok = do_load(pending_load_path);
        printf("pop: savestate %s %s\n", pending_load_path, ok ? "restored" : "could not be restored");
        /* Keep start_level as set for the boot load: SDLPoP reads
         * start_level < 0 as "on the title screen", where any key starts a
         * new game. It resets it itself when it goes back to the title. */
        boot_load = false;
    }
}

bool pop_core_can_save(void)
{
    return gnw_in_level_loop && !pending_load && Kid.alive < 0 && current_level >= 1 && current_level <= 14 &&
           (!is_feather_fall || fixes->fix_quicksave_during_feather);
}

bool pop_core_save_state(const char *path)
{
    if (!pop_core_can_save())
        return false;
    quick_fp = (fopen)(path, "wb");
    if (quick_fp == NULL)
        return false;
    int ok = process_save((void *)quick_version, QUICK_VERSION_SIZE);
    ok = ok && quick_process(process_save);
#ifdef POP_TRACE
    printf("TRACE save: lvl=%d room=%d x=%d y=%d frame=%d rem=%d\n", current_level, Kid.room, Kid.x, Kid.y, Kid.frame, rem_min);
#endif
    fclose(quick_fp);
    quick_fp = NULL;
    return ok != 0;
}

bool pop_core_load_state(const char *path)
{
    if (!gnw_in_level_loop)
        return false;
    if (savestate_level(path) < 0)
        return false;
    snprintf(pending_load_path, sizeof(pending_load_path), "%s", path);
    pending_load = true;
    return true;
}

void pop_core_set_music(bool on)
{
    core_opts.music = on;
    if (!on)
        stop_midi(); /* no-op when nothing plays */
    enable_music = on ? 1 : 0;
}

void pop_core_set_sound(bool on)
{
    core_opts.sound = on;
    turn_sound_on_off(on ? 15 : 0);
}

void pop_core_set_fixes(bool on)
{
    core_opts.fixes = on;
    turn_fixes_and_enhancements_on_off(on ? 1 : 0);
}

void pop_core_restart_level(void)
{
    if (gnw_in_level_loop && current_level != 15) {
        stop_sounds();
        is_restart_level = 1;
    }
}

#ifdef POP_TRACE
void pop_core_trace(const char *tag)
{
    printf("TRACE %s: lvl=%d room=%d x=%d y=%d frame=%d rem=%d inloop=%d\n", tag, current_level, Kid.room, Kid.x, Kid.y, Kid.frame, rem_min, gnw_in_level_loop);
}
#endif

int pop_core_current_level(void)
{
    return gnw_in_level_loop ? current_level : -1;
}

int pop_core_remaining_minutes(void)
{
    return gnw_in_level_loop ? rem_min : -1;
}

void pop_core_run(const pop_core_options_t *opts)
{
    static char *argv[] = { "prince", NULL };
    core_opts = *opts;
    g_argc = 1;
    g_argv = argv;
    pop_main();
    pop_plat_quit(0);
}
