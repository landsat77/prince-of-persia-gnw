# Changelog

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Release tags must
match a section heading exactly (for example `v1.0.0`): CI uses the matching
section as the GitHub Release notes.

## [Unreleased]

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
