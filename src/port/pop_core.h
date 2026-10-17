/*
 * Platform independent part of the Prince of Persia core: runs SDLPoP with
 * the options chosen by the user and implements savestates on top of the
 * SDLPoP quicksave format.
 */
#ifndef POP_CORE_H
#define POP_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  start_level;       /* -1: title screen, else start directly at this level */
    bool fixes;             /* SDLPoP "fixes and enhancements" */
    bool music;             /* MIDI music (OPL3 emulation, CPU hungry) */
    bool sound;             /* all sounds */
    const char *load_state; /* savestate to restore once the level is running (or NULL) */
} pop_core_options_t;

void pop_core_default_options(pop_core_options_t *opts);

/* Start SDLPoP. Never returns (the game calls pop_plat_quit() to exit). */
void pop_core_run(const pop_core_options_t *opts) __attribute__((noreturn));

/* True while the game is waiting between two frames of a level: the only
 * moment where a savestate can be written. */
bool pop_core_can_save(void);
/* Write a savestate (SDLPoP quicksave format) to `path`. */
bool pop_core_save_state(const char *path);
/* Restore a savestate. Applied at the start of the next game frame. */
bool pop_core_load_state(const char *path);

void pop_core_set_music(bool on);
void pop_core_set_sound(bool on);
void pop_core_set_fixes(bool on);
/* Restart the current level (like Ctrl+A). */
void pop_core_restart_level(void);
/* Current level (0 = demo, 1..14), -1 when not in a level. */
int pop_core_current_level(void);
/* Remaining time in minutes (as shown by the game) or -1. */
int pop_core_remaining_minutes(void);

#ifdef __cplusplus
}
#endif

#endif
