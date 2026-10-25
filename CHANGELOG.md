# Changelog

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Release tags must
match a section heading exactly (for example `v1.0.0`): CI uses the matching
section as the GitHub Release notes.

## [Unreleased]

## [v1.2.2] - 2026-10-25

### Fixed

- The game could not be finished: the Hall of Fame asks for the player's
  name, typed on a keyboard, and the Game & Watch has none (the end
  sequence waited forever). The name is now entered with the buttons:
  Up/Down (or B) choose the letter, Right or A adds a letter, Left removes
  the last one, GAME or TIME accepts the name.
- Harness: `-W <ms>` jumps to the winning room (tests the end sequence and
  the name entry).

## [v1.2.1] - 2026-10-19

### Added

- A cover for the launcher's coverflow (the game's box and a room,
  `src/assets/cover.png`, 128x96, encoded by `scripts/make_cover.py`)
  instead of the generated title card.

## [v1.2.0] - 2026-10-17

### Changed

- The pre-rendered music is stored at 11 kHz by default: the pack is ~9 MB
  instead of ~17 MB. It is rendered at 22 kHz with the game's code, low-pass
  filtered (63 tap FIR) and decimated by `pop_pack.py`'s renderer, then
  played back with a 16 tap interpolation filter (47 dB in band, images
  43 dB down). Only the high harmonics above ~5 kHz are lost.
  `--music-rate 22050` keeps the full rate (sound identical to live
  synthesis). Renderer option `-M <rate>`.

## [v1.1.1] - 2026-10-17

### Fixed

- Windows music renderer: it rendered nothing ("music: 0 pieces"). SDLPoP's
  Windows-only UTF-8 file wrappers (`fopen_UTF8`, ...) replaced the file layer
  of the port and could not write the music files. They are now disabled in
  the GNW builds. Checked under Wine: same output as the Linux renderer.
- `pop_pack.py` warns (with the renderer output) when no music was rendered.

## [v1.1.0] - 2026-10-17

### Changed

- Music pre-rendered by `pop_pack.py`: the OPL3 synthesis of the MIDI music
  took most of the CPU (the game slowed down while music played). Every piece
  is now rendered once on the PC with the game's own code (same sound, sample
  exact with the first live playback) and only played back on the console:
  31 times less CPU on the title screen, 7.5 times less in a level (measured
  with the harness). Packs without music still work (live synthesis).
- `pop_pack.py`: `--music-codec pcm|adpcm`, `--no-music`, `--renderer`.
  Renderers for Linux, Windows and macOS in the release zip
  (`tools/build_renderers.sh`, zig cc).

## [v1.0.1] - 2026-10-17

### Fixed

- Savestates refused on the console: the pause menu could only save while
  the game waited between two frames, and the Game & Watch is often slower
  than the game's 12 fps timer, so that wait did not happen. The game now
  stops once per frame at a safe point where the menu runs (a press on
  PAUSE elsewhere in a level is remembered until then).
- Buttons ignored during slow passages (same cause): keys are now read at
  least once per frame.

## [v1.0.0] - 2026-10-17

### Added

- Prince of Persia (DOS) as a Retro-Go SD GWHB homebrew, built on SDLPoP 1.24
  and the retro-go-sd-templates SDK (firmware ABI v2, Retro-Go SD v2.0.0-rc1
  or later).
- Game data loaded from a `.pop` pack next to the `.bin`
  (`tools/pop_pack.py`, mods supported).
- Savestates, pause menu options (music, sound, bug fixes, restart level),
  4:3 scaling with optional filtering.
