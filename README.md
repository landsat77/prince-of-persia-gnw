# Prince of Persia for Game & Watch Retro-Go SD

The 1989/1990 DOS version of **Prince of Persia**, running on the Nintendo
Game & Watch with the [Retro-Go SD](https://github.com/sylverb/game-and-watch-retro-go-sd)
firmware, as a **GWHB homebrew**: a single `.bin` dropped in `/homebrews/`,
no firmware rebuild needed.

The game engine is [SDLPoP](https://github.com/NagyD/SDLPoP) (an open source
C reimplementation of the original game) running on a minimal SDL2 layer
written for the Game & Watch. The project is built from
[retro-go-sd-templates](https://github.com/sylverb/retro-go-sd-templates):
it talks to the firmware only through the firmware ABI.

**The game data is not included.** You need your own copy of the DOS game
(`PRINCE.DAT`, `KID.DAT`, `LEVELS.DAT`, `VDUNGEON.DAT`, ...) or the `data`
folder of SDLPoP.

## Installation

Requires Retro-Go SD **v2.0.0-rc1 or later** (firmware ABI v2).

1. Build the game data pack (Python 3; `pip install Pillow` is needed for
   SDLPoP's PNG data folders):
   ```
   python3 tools/pop_pack.py PrinceOfPersia.pop /path/to/PRINCE
   ```
2. Copy both files to the SD card, side by side, with the same name:
   ```
   /homebrews/PrinceOfPersia.bin
   /homebrews/PrinceOfPersia.pop
   ```
3. Start it from the **Homebrew** tab.

The pack is cached in the external flash the first time ("Caching game");
sprites, sounds and music are then used in place from there.

### Music

The MIDI music of the game is synthesized by an emulated AdLib (OPL3) chip,
which costs more CPU than the rest of the game together: the game slows down
while music plays. `pop_pack.py` therefore renders every music piece once,
on your computer, with the game's own code, and stores it in the pack: the
console only plays it back, with the same sound.

- The renderer is in the `pop_render/` folder of the release zip (Linux,
  Windows, macOS); `pop_pack.py` picks the right one. From a checkout, build
  them with `tools/build_renderers.sh` (needs `pip install ziglang`) or use
  `make harness`.
- The music adds about 16 MB to the pack (16 bit PCM). For a console with a
  small external flash, `--music-codec adpcm` makes it 4 MB (slight hiss), and
  `--no-music` keeps the old behaviour (live synthesis, 1.2 MB pack).

### Mods

A mod is a folder with custom levels / graphics / `mod.ini`. Give it first
and the original game after it, then copy the `.bin` under the same name:

```
python3 tools/pop_pack.py "Christmas of Persia.pop" /path/to/mods/xmas /path/to/PRINCE
cp PrinceOfPersia.bin "/sdcard/homebrews/Christmas of Persia.bin"
```

Each `.bin` loads the `.pop` file that has its name.

## Controls

| Game & Watch | Prince of Persia |
|---|---|
| D-pad | Move, climb (up), crouch / go down (down) |
| A | Action (Shift): grab ledges, pick up, sword strike, careful step |
| B | Jump (same as up) |
| GAME (START on Zelda units) | Pause (Esc) |
| TIME (SELECT on Zelda units) | Show the remaining time (Space) |
| PAUSE/SET | Retro-Go menu |

The Retro-Go menu adds savestates (they can be made while a level is
running, not on the title screen or in cutscenes) and the game options:
Music on/off (the MIDI music is emulated with an OPL3 chip and is the most
CPU hungry part), Sound on/off, SDLPoP bug fixes on/off and Restart level.
Display "Scaling": *Off* shows the original 320x200 picture with black
borders, the other modes stretch it to 320x240, the 4:3 aspect ratio of the
original game ("Filtering" smooths the stretched lines).

Options and the hall of fame are stored in `/data/pop/<name>/`.

## Building

Needs `arm-none-eabi-gcc` (hard-float, v10+), GNU Make, Python 3 + Pillow:

```
make            # → PrinceOfPersia.bin
make docker     # same, inside the Retro-Go SD builder image
```

### Testing without hardware

`harness/` builds the same game and port code for Linux, headless, with a
virtual clock, scripted key presses, PPM screenshots and WAV output:

```
make harness
harness/build/pop_host PrinceOfPersia.pop -t 30000 -e 1000 -o /tmp/out -w /tmp/out/a.wav
harness/build/pop_host PrinceOfPersia.pop -l 1 -s "3000:RIGHT:1,4000:RIGHT:0" -o /tmp/out
```

`-q file -Q ms` writes a savestate at that time (like the pause menu would),
`-L file` restores one at start, `-H kb` changes the heap size, `-F ms`
makes every frame of a level that much slower (a slow device). Build with `OPT="-O2 -g -DPOP_TRACE"`
to print the prince's position on save / load / exit.

## Layout

| Path | Role |
|------|------|
| `src/sdlpop/` | SDLPoP 1.24 RC (NagyD/SDLPoP @ 3c5add5). Every Game & Watch change is under `#ifdef GNW`. |
| `src/port/sdl/SDL2/SDL.h`, `src/port/pop_sdl.c` | Minimal SDL2 (surfaces, blits, events, timers, audio callback): only what SDLPoP uses. |
| `src/port/pop_alloc.c` | Heap on the RAM left after the binary (~540 KB, peak use ~340 KB). |
| `src/port/pop_pack.c` | `.pop` pack reader. |
| `src/port/pop_file.c` | stdio for SDLPoP: pack files are in-memory handles, saves are real files. |
| `src/port/pop_libc.c` | The few libc functions the firmware ABI does not export. |
| `src/port/pop_core.c` | Options and savestates (SDLPoP quicksave format). |
| `src/port/main_pop.c` | Retro-Go side: entry point, display, audio, buttons, pause menu. |
| `tools/pop_pack.py` | Builds the `.pop` pack. |
| `harness/` | Headless Linux test harness. |
| `sdk/` | Retro-Go SD core / homebrew SDK (from retro-go-sd-templates). |

See `CLAUDE.md` for the internals (memory, savestates, ABI notes).

## License

GPLv3 (see `LICENSE`), like SDLPoP. The SDK under `sdk/` comes from
retro-go-sd-templates (MIT, `sdk/LICENSE.MIT`; vendored firmware headers
keep their own licenses). Prince of Persia is a trademark of Ubisoft; the
original game was written by Jordan Mechner.
