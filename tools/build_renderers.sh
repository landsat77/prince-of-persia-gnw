#!/bin/sh
# Build the music renderer used by tools/pop_pack.py (the headless harness,
# option -R) for Linux, Windows and macOS, with "zig cc" as cross compiler:
#   pip install ziglang
#   tools/build_renderers.sh            -> tools/bin/pop_render-<os>-<arch>
set -e
cd "$(dirname "$0")/.."
OUT=tools/bin
mkdir -p "$OUT"
SRC="src/sdlpop/data.c src/sdlpop/seg000.c src/sdlpop/seg001.c src/sdlpop/seg002.c \
src/sdlpop/seg003.c src/sdlpop/seg004.c src/sdlpop/seg005.c src/sdlpop/seg006.c \
src/sdlpop/seg007.c src/sdlpop/seg008.c src/sdlpop/seg009.c src/sdlpop/seqtbl.c \
src/sdlpop/options.c src/sdlpop/midi.c src/sdlpop/opl3.c src/port/pop_core.c"
PORT="src/port/pop_sdl.c src/port/pop_alloc.c src/port/pop_pack.c src/port/gnw_music.c \
src/port/pop_file.c harness/pop_host.c"
ZIG="${ZIG:-python3 -m ziglang}"
TARGETS="${TARGETS:-x86_64-linux-musl:linux-x86_64 aarch64-linux-musl:linux-arm64 x86_64-windows-gnu:windows-x86_64.exe x86_64-macos:macos-x86_64 aarch64-macos:macos-arm64}"
for target in $TARGETS; do
    triple=${target%%:*}
    name=pop_render-${target#*:}
    tmp=$(mktemp -d)
    objs=""
    for f in $SRC; do
        o="$tmp/s_$(basename "$f" .c).o"
        $ZIG cc -target "$triple" -O2 -std=gnu11 -DGNW -w -Isrc/port/sdl -Isrc/port -Isrc/sdlpop \
            -include src/port/gnw_pop_glue.h -c "$f" -o "$o"
        objs="$objs $o"
    done
    for f in $PORT; do
        o="$tmp/p_$(basename "$f" .c).o"
        $ZIG cc -target "$triple" -O2 -std=gnu11 -DGNW -w -Isrc/port/sdl -Isrc/port -Isrc/sdlpop -c "$f" -o "$o"
        objs="$objs $o"
    done
    $ZIG cc -target "$triple" -O2 -s $objs -lm -o "$OUT/$name"
    rm -f "$OUT/${name%.exe}.pdb"
    rm -rf "$tmp"
    echo "built $OUT/$name"
done
