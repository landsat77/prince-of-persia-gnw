#!/usr/bin/env python3
"""
Build a Prince of Persia asset pack (.pop) for the Game & Watch PoP core.

The core runs SDLPoP and needs the data files of the original DOS game
(PRINCE.DAT, KID.DAT, VDUNGEON.DAT, LEVELS.DAT, ...). On the Game & Watch,
all of them are stored in a single "pack" file that the core maps into the
external flash, so that sprites and sounds can be used in place without
filling the 1 MB of RAM.

While packing, every sprite is decompressed and stored as an uncompressed
8 bits per pixel image (still in the regular DAT format), which is what lets
the core draw them straight from flash.

Accepted inputs (each one is a directory):
  * the folder of the original DOS game (PRINCE.DAT, KID.DAT, ...)
  * an SDLPoP "data" folder, where some DAT files are replaced by folders
    of PNG/PAL/BIN files (KID/, VDUNGEON/, ...)
  * a mod folder (custom LEVELS.DAT, graphics, mod.ini, PRINCE.EXE): give it
    first and the base game folder after it, the first folder containing a
    given DAT file wins, like in SDLPoP.

Optional files copied as is when found: SDLPoP.ini, mod.ini, PRINCE.EXE.

Music: the MIDI music is synthesized by an OPL3 emulator, the most CPU hungry
part of the game on the Game & Watch. When the renderer is available
(pop_render/ folder of the release zip, tools/build_renderers.sh, the test
harness built with "make harness", or --renderer), every
music piece is rendered once here, with the game's own code, and stored in the
pack (MIDInn.MUS files): the console then only plays it back, with the same
sound. Without the renderer the music is synthesized live, as before.
  --music-rate 11025   stored at 11 kHz (default, ~8 MB more), low-pass
                       filtered, played back at 22 kHz with interpolation
  --music-rate 22050   stored at the output rate: identical sound (~16 MB)
  --music-codec pcm    16 bit PCM (default)
  --music-codec adpcm  IMA ADPCM, 4 times smaller, slight hiss
  --no-music           do not pre-render the music

Examples:
  python3 tools/pop_pack.py "Prince of Persia.pop" ~/dos/PRINCE
  python3 tools/pop_pack.py "Christmas of Persia.pop" ~/mods/xmas ~/dos/PRINCE

Copy the resulting .pop file next to the homebrew, with the same name:
/homebrews/PrinceOfPersia.pop for /homebrews/PrinceOfPersia.bin.
"""

import argparse
import array
import io
import os
import platform
import shutil
import struct
import subprocess
import sys
import tempfile

MAGIC = b"GWPOP1\0\0"
ENTRY_NAME_LEN = 24

# DAT files used by SDLPoP (the folder variant uses the same name without .DAT)
DAT_NAMES = [
    "PRINCE.DAT", "KID.DAT", "LEVELS.DAT", "TITLE.DAT", "PV.DAT",
    "VDUNGEON.DAT", "VPALACE.DAT", "GUARD.DAT", "GUARD1.DAT", "GUARD2.DAT",
    "FAT.DAT", "SKEL.DAT", "VIZIER.DAT", "SHADOW.DAT",
    "IBM_SND1.DAT", "IBM_SND2.DAT", "DIGISND1.DAT", "DIGISND2.DAT", "DIGISND3.DAT",
    "MIDISND1.DAT", "MIDISND2.DAT",
    "FONT",  # SDLPoP: optional replacement font (folder "font")
]
EXTRA_FILES = ["SDLPoP.ini", "mod.ini", "PRINCE.EXE"]
# DATs holding sounds / levels only: copied unchanged, no image scan
NO_IMAGE_DATS = {"LEVELS.DAT", "IBM_SND1.DAT", "IBM_SND2.DAT", "DIGISND1.DAT",
                 "DIGISND2.DAT", "DIGISND3.DAT", "MIDISND1.DAT", "MIDISND2.DAT"}
# Optional graphics of the 1.3/1.4 versions (load_more_opt_graf): ids 1201.. use palette 200
OPTGRAF_DATS = {"VDUNGEON.DAT", "VPALACE.DAT"}

GNW_IMAGE_FLAG_16_COLORS = 0x0001
MAX_RES_SIZE = 0xFFFF


# --------------------------------------------------------------------------
# DAT reading / writing
# --------------------------------------------------------------------------
def read_dat(data):
    """Return an ordered dict {id: bytes} (resource data without checksum)."""
    table_offset, table_size = struct.unpack_from("<IH", data, 0)
    count = struct.unpack_from("<H", data, table_offset)[0]
    res = {}
    for i in range(count):
        rid, off, size = struct.unpack_from("<HIH", data, table_offset + 2 + i * 8)
        res[rid] = data[off + 1: off + 1 + size]
    return res


def write_dat(resources):
    out = io.BytesIO()
    out.write(b"\0" * 6)
    table = []
    for rid, blob in resources.items():
        if len(blob) > MAX_RES_SIZE:
            raise ValueError("resource %d too big (%d bytes)" % (rid, len(blob)))
        off = out.tell()
        checksum = (0xFF - (sum(blob) & 0xFF)) & 0xFF
        out.write(bytes([checksum]))
        out.write(blob)
        table.append((rid, off, len(blob)))
    table_offset = out.tell()
    out.write(struct.pack("<H", len(table)))
    for rid, off, size in table:
        out.write(struct.pack("<HIH", rid, off, size))
    table_size = out.tell() - table_offset
    data = bytearray(out.getvalue())
    struct.pack_into("<IH", data, 0, table_offset, table_size)
    return bytes(data)


# --------------------------------------------------------------------------
# Image codecs (ports of SDLPoP's seg009.c decompressors)
# --------------------------------------------------------------------------
def _s8(b):
    return b - 256 if b >= 128 else b


def rle_lr(src, dest_len):
    dst = bytearray(dest_len)
    s = d = 0
    rem = dest_len
    src = src + b"\0" * 4
    while rem > 0:
        count = _s8(src[s]); s += 1
        if count >= 0:
            count += 1
            while True:
                dst[d] = src[s]; d += 1; s += 1; rem -= 1; count -= 1
                if not (count and rem):
                    break
        else:
            al = src[s]; s += 1
            count = -count
            while True:
                dst[d] = al; d += 1; rem -= 1; count -= 1
                if not (count and rem):
                    break
    return dst


def rle_ud(src, dest_len, stride, height):
    dst = bytearray(dest_len)
    s = 0
    d = 0
    rem = dest_len
    rem_h = height
    src = src + b"\0" * 4
    def step(d, rem_h):
        d += stride
        rem_h -= 1
        if rem_h == 0:
            d -= dest_len - 1
            rem_h = height
        return d, rem_h
    while rem > 0:
        count = _s8(src[s]); s += 1
        if count >= 0:
            count += 1
            while True:
                dst[d] = src[s]; s += 1
                d, rem_h = step(d, rem_h)
                rem -= 1; count -= 1
                if not (count and rem):
                    break
        else:
            al = src[s]; s += 1
            count = -count
            while True:
                dst[d] = al
                d, rem_h = step(d, rem_h)
                rem -= 1; count -= 1
                if not (count and rem):
                    break
    return dst


def lzg_lr(src, dest_len):
    window = bytearray(0x400)
    wpos = 0x400 - 0x42
    dst = bytearray(dest_len)
    d = 0
    s = 0
    rem = dest_len
    mask = 0
    src = src + b"\0" * 4
    while rem > 0:
        mask >>= 1
        if (mask & 0xFF00) == 0:
            mask = src[s] | 0xFF00; s += 1
        if mask & 1:
            window[wpos] = dst[d] = src[s]
            wpos = (wpos + 1) & 0x3FF; d += 1; s += 1; rem -= 1
        else:
            info = (src[s] << 8) | src[s + 1]; s += 2
            cs = info & 0x3FF
            length = (info >> 10) + 3
            while True:
                window[wpos] = dst[d] = window[cs]
                wpos = (wpos + 1) & 0x3FF; d += 1; cs = (cs + 1) & 0x3FF
                rem -= 1; length -= 1
                if not (rem and length):
                    break
    return dst


def lzg_ud(src, dest_len, stride, height):
    window = bytearray(0x400)
    wpos = 0x400 - 0x42
    dst = bytearray(dest_len)
    d = 0
    s = 0
    remaining = height
    dest_end = dest_len - 1
    left = dest_len
    mask = 0
    src = src + b"\0" * 4
    while left > 0:
        mask >>= 1
        if (mask & 0xFF00) == 0:
            mask = src[s] | 0xFF00; s += 1
        if mask & 1:
            window[wpos] = dst[d] = src[s]
            wpos = (wpos + 1) & 0x3FF; s += 1
            d += stride
            remaining -= 1
            if remaining == 0:
                d -= dest_end
                remaining = height
            left -= 1
        else:
            info = (src[s] << 8) | src[s + 1]; s += 2
            cs = info & 0x3FF
            length = (info >> 10) + 3
            while True:
                window[wpos] = dst[d] = window[cs]
                wpos = (wpos + 1) & 0x3FF; cs = (cs + 1) & 0x3FF
                d += stride
                remaining -= 1
                if remaining == 0:
                    d -= dest_end
                    remaining = height
                left -= 1; length -= 1
                if not (left and length):
                    break
    return dst


def decode_image(blob):
    """Decode a DAT image resource. Returns (width, height, depth, pixels8) or None."""
    if len(blob) < 6:
        return None
    height, width, flags = struct.unpack_from("<HHH", blob, 0)
    depth = ((flags >> 12) & 7) + 1
    cmeth = (flags >> 8) & 0x0F
    if height == 0 or width == 0 or depth not in (1, 2, 4, 8) or cmeth > 4:
        return None
    stride = (depth * width + 7) // 8
    size = stride * height
    src = blob[6:]
    try:
        if cmeth == 0:
            raw = bytearray(src[:size]) + bytearray(max(0, size - len(src)))
        elif cmeth == 1:
            raw = rle_lr(src, size)
        elif cmeth == 2:
            raw = rle_ud(src, size, stride, height)
        elif cmeth == 3:
            raw = lzg_lr(src, size)
        else:
            raw = lzg_ud(src, size, stride, height)
    except IndexError:
        return None
    # expand to 8 bpp
    out = bytearray(width * height)
    per_byte = 8 // depth
    m = (1 << depth) - 1
    for y in range(height):
        row = raw[y * stride:(y + 1) * stride]
        o = y * width
        x = 0
        for b in row:
            shift = 8
            for _ in range(per_byte):
                if x >= width:
                    break
                shift -= depth
                out[o + x] = (b >> shift) & m
                x += 1
    return width, height, depth, out


def encode_raw8(width, height, pixels, few_colors):
    flags = (7 << 12) | (0 << 8) | (GNW_IMAGE_FLAG_16_COLORS if few_colors else 0)
    return struct.pack("<HHH", height, width, flags) + bytes(pixels)


def encode_raw4(width, height, pixels):
    """Fallback for images too big for raw 8 bpp: uncompressed 4 bpp."""
    stride = (width + 1) // 2
    out = bytearray(stride * height)
    for y in range(height):
        for x in range(width):
            v = pixels[y * width + x] & 0x0F
            i = y * stride + x // 2
            out[i] |= (v << 4) if (x & 1) == 0 else v
    flags = (3 << 12) | (0 << 8)
    return struct.pack("<HHH", height, width, flags) + bytes(out)


def convert_image(blob, stats):
    dec = decode_image(blob)
    if dec is None:
        stats["kept"] += 1
        return blob
    width, height, depth, pixels = dec
    few = depth <= 4
    raw = encode_raw8(width, height, pixels, few)
    if len(raw) <= MAX_RES_SIZE:
        stats["raw8"] += 1
        return raw
    if few:
        stats["raw4"] += 1
        return encode_raw4(width, height, pixels)
    stats["kept"] += 1
    return blob


# --------------------------------------------------------------------------
# SDLPoP folders (PNG / PAL / BIN)
# --------------------------------------------------------------------------
def png_to_image(path, pal_rgb, stats):
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is required to convert PNG files: python3 -m pip install Pillow")
    im = Image.open(path)
    w, h = im.size
    if im.mode == "P":
        pixels = bytearray(im.tobytes())
        ncolors = len(im.getpalette() or []) // 3
        few = max(pixels, default=0) < 16 and ncolors <= 256
    else:
        # True colour image: map to the closest colour of the sprite palette.
        im = im.convert("RGBA")
        cols = pal_rgb or [(0, 0, 0)] * 16
        cache = {}
        pixels = bytearray(w * h)
        data = im.tobytes()
        for i in range(w * h):
            r, g, b, a = data[4 * i:4 * i + 4]
            if a < 128:
                continue
            key = (r, g, b)
            if key not in cache:
                best = min(range(1, len(cols)), key=lambda c: (cols[c][0] - r) ** 2 + (cols[c][1] - g) ** 2 + (cols[c][2] - b) ** 2)
                cache[key] = best
            pixels[i] = cache[key]
        few = True
    raw = encode_raw8(w, h, pixels, few)
    if len(raw) > MAX_RES_SIZE:
        stats["raw4"] += 1
        return encode_raw4(w, h, pixels)
    stats["raw8"] += 1
    return raw


def read_folder_dat(folder, stats):
    res = {}
    pal_for = {}
    files = sorted(os.listdir(folder))
    # palettes first (needed to quantize true colour PNGs)
    for f in files:
        name, ext = os.path.splitext(f)
        if not name.lower().startswith("res"):
            continue
        try:
            rid = int(name[3:])
        except ValueError:
            continue
        ext = ext.lower()
        p = os.path.join(folder, f)
        if ext in (".pal", ".bin"):
            with open(p, "rb") as fh:
                res[rid] = fh.read()
            if ext == ".pal" and len(res[rid]) >= 52:
                d = res[rid]
                pal_for[rid] = [(d[4 + 3 * i] << 2, d[5 + 3 * i] << 2, d[6 + 3 * i] << 2) for i in range(16)]
    for f in files:
        name, ext = os.path.splitext(f)
        if ext.lower() != ".png" or not name.lower().startswith("res"):
            continue
        try:
            rid = int(name[3:])
        except ValueError:
            continue
        # closest palette below this id
        pal_ids = [k for k in pal_for if k < rid]
        pal = pal_for[max(pal_ids)] if pal_ids else None
        res[rid] = png_to_image(os.path.join(folder, f), pal, stats)
    return res


# --------------------------------------------------------------------------
# Image detection inside DAT files
# --------------------------------------------------------------------------
# Non-image resources stored in the sprite DATs
BINARY_IDS = {"PRINCE.DAT": {1, 10, 20}}


def plausible_image(blob):
    if len(blob) < 6:
        return False
    height, width, flags = struct.unpack_from("<HHH", blob, 0)
    depth = ((flags >> 12) & 7) + 1
    cmeth = (flags >> 8) & 0x0F
    if not (0 < width <= 320 and 0 < height <= 200) or depth not in (1, 2, 4, 8) or cmeth > 4:
        return False
    if cmeth == 0 and len(blob) < 6 + ((depth * width + 7) // 8) * height:
        return False
    return True


def image_ids(dat_name, res):
    ids = set()
    for rid, blob in res.items():
        # sprite table header ("pal" resource): n_images + dat_pal_type
        if len(blob) == 100 and rid + 1 in res:
            n = blob[0]
            for i in range(1, n + 1):
                if rid + i in res:
                    ids.add(rid + i)
    # Sprites whose palette lives in another DAT (GUARD.DAT uses GUARD1/2.DAT,
    # optional graphics of VDUNGEON/VPALACE use palette 200...).
    binary = BINARY_IDS.get(dat_name, set())
    for rid, blob in res.items():
        if rid in ids or rid in binary or len(blob) == 100:
            continue
        if plausible_image(blob):
            ids.add(rid)
    return ids


def find_case_insensitive(folder, name):
    try:
        entries = os.listdir(folder)
    except OSError:
        return None
    for e in entries:
        if e.lower() == name.lower():
            return os.path.join(folder, e)
    return None


def collect_dat(name, inputs, stats, verbose):
    """Return (blob, source) for a DAT name, or (None, None)."""
    base = name[:-4] if name.endswith(".DAT") else name
    for folder in inputs:
        for sub in ("", "data"):
            root = os.path.join(folder, sub) if sub else folder
            dat_path = find_case_insensitive(root, name) if name.endswith(".DAT") else None
            dir_path = find_case_insensitive(root, base)
            if dir_path and not os.path.isdir(dir_path):
                dir_path = None
            if not dat_path and not dir_path:
                continue
            res = {}
            if dat_path and os.path.isfile(dat_path):
                with open(dat_path, "rb") as fh:
                    res = read_dat(fh.read())
                if name not in NO_IMAGE_DATS:
                    imgs = image_ids(name, res)
                    for rid in list(res):
                        if rid in imgs:
                            res[rid] = convert_image(res[rid], stats)
            if dir_path:
                extra = read_folder_dat(dir_path, stats)
                for rid, blob in extra.items():
                    # Like SDLPoP: folders fill in what the DAT does not have
                    # (or holds as an empty placeholder).
                    if rid not in res or len(res[rid]) <= 2:
                        res[rid] = blob
            if verbose:
                print("  %-13s <- %s (%d resources)" % (name, dat_path or dir_path, len(res)))
            return write_dat(dict(sorted(res.items()))), (dat_path or dir_path)
    return None, None


def collect_files(inputs, verbose=True):
    stats = {"raw8": 0, "raw4": 0, "kept": 0}
    files = []
    for name in DAT_NAMES:
        blob, src = collect_dat(name, inputs, stats, verbose)
        if blob is not None:
            files.append(("font" if name == "FONT" else name, blob))
    for name in EXTRA_FILES:
        for folder in inputs:
            p = find_case_insensitive(folder, name)
            if p and os.path.isfile(p):
                with open(p, "rb") as fh:
                    files.append((name, fh.read()))
                if verbose:
                    print("  %-13s <- %s" % (name, p))
                break

    names = {n.upper() for n, _ in files}
    required = ["PRINCE.DAT", "KID.DAT", "LEVELS.DAT", "VDUNGEON.DAT", "VPALACE.DAT", "GUARD.DAT"]
    missing = [n for n in required if n not in names]
    if missing:
        sys.exit("error: missing game data: %s (is this the Prince of Persia folder?)" % ", ".join(missing))
    if verbose:
        print("images: %d raw 8bpp, %d raw 4bpp, %d left as is" % (stats["raw8"], stats["raw4"], stats["kept"]))
    return files


def write_pack(out_path, files, verbose=True):
    header_size = 16 + len(files) * (ENTRY_NAME_LEN + 8)
    offset = (header_size + 3) & ~3
    entries = []
    for name, blob in files:
        entries.append((name, offset, len(blob)))
        offset = (offset + len(blob) + 3) & ~3
    total = offset
    out = bytearray(total)
    out[0:8] = MAGIC
    struct.pack_into("<II", out, 8, len(files), total)
    for i, (name, off, size) in enumerate(entries):
        nb = name.encode("ascii")[:ENTRY_NAME_LEN - 1]
        struct.pack_into("<%dsII" % ENTRY_NAME_LEN, out, 16 + i * (ENTRY_NAME_LEN + 8), nb, off, size)
    for (name, blob), (_, off, size) in zip(files, entries):
        out[off:off + size] = blob
    with open(out_path, "wb") as fh:
        fh.write(out)
    if verbose:
        print("wrote %s: %d files, %d bytes" % (out_path, len(files), total))
    return total


# --------------------------------------------------------------------------
# Pre-rendered music (see src/port/gnw_music.c for the .MUS format)
OUTPUT_RATE = 22050       # output rate of the game on the Game & Watch
ADPCM_BLOCK = 1024
IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
            50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
            253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
            1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
            3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
            11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
            32767]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def ima_adpcm_encode(samples):
    out = bytearray()
    step_tab, idx_tab = IMA_STEP, IMA_INDEX
    for b in range(0, len(samples), ADPCM_BLOCK):
        blk = samples[b:b + ADPCM_BLOCK]
        pred = blk[0]
        idx = 0
        if len(blk) > 1:
            d = abs(blk[1] - blk[0])
            while idx < 88 and step_tab[idx] < d:
                idx += 1
        out += struct.pack("<hBB", pred, idx, 0)
        codes = bytearray()
        for s in blk:
            step = step_tab[idx]
            diff = s - pred
            code = 0
            if diff < 0:
                code = 8
                diff = -diff
            dq = step >> 3
            if diff >= step:
                code |= 4
                diff -= step
                dq += step
            step >>= 1
            if diff >= step:
                code |= 2
                diff -= step
                dq += step
            step >>= 1
            if diff >= step:
                code |= 1
                dq += step
            pred = pred - dq if code & 8 else pred + dq
            if pred > 32767:
                pred = 32767
            elif pred < -32768:
                pred = -32768
            idx += idx_tab[code & 7]
            if idx < 0:
                idx = 0
            elif idx > 88:
                idx = 88
            codes.append(code)
        if len(codes) & 1:
            codes.append(0)
        out += bytes(codes[i] | (codes[i + 1] << 4) for i in range(0, len(codes), 2))
    return bytes(out)


def encode_mus(raw, codec, rate):
    samples = array.array("h")
    samples.frombytes(raw)
    if sys.byteorder != "little":
        samples.byteswap()
    if codec == "pcm":
        data = raw
        codec_id = 0
    else:
        data = ima_adpcm_encode(samples)
        codec_id = 1
    return b"GWMU" + struct.pack("<HHII", 1, codec_id, rate, len(samples)) + data


def default_renderer():
    """The renderer for this computer: pop_render-<os>-<arch> in pop_render/
    next to this script (release zip) or in tools/bin/ (tools/build_renderers.sh),
    else the harness built with 'make harness'."""
    here = os.path.dirname(os.path.abspath(__file__))
    system = {"Linux": "linux", "Windows": "windows", "Darwin": "macos"}.get(platform.system())
    machine = platform.machine().lower()
    arch = "arm64" if machine in ("arm64", "aarch64") else "x86_64"
    candidates = []
    if system:
        name = "pop_render-%s-%s%s" % (system, arch, ".exe" if system == "windows" else "")
        names = [name]
        if system == "windows" and arch == "arm64":  # x86_64 emulation
            names.append("pop_render-windows-x86_64.exe")
        for n in names:
            # release zip: pop_render/ next to this script; repository: tools/bin/
            candidates += [os.path.join(here, "pop_render", n), os.path.join(here, "bin", n),
                           os.path.join(here, n)]
    candidates += [os.path.join(here, "..", "harness", "build", "pop_host")]
    for p in candidates:
        if os.path.isfile(p):
            prepare_executable(p)
            return p
    return None


def prepare_executable(path):
    """Make a renderer from a zip file runnable (exec bit, macOS quarantine)."""
    if os.name != "nt" and not os.access(path, os.X_OK):
        try:
            os.chmod(path, os.stat(path).st_mode | 0o111)
        except OSError:
            pass
    if platform.system() == "Darwin":
        subprocess.run(["xattr", "-d", "com.apple.quarantine", path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def render_music(files, renderer, codec, rate, verbose=True):
    """Render every MIDI piece with the game's own code, return .MUS files."""
    tmp = tempfile.mkdtemp(prefix="pop_music_")
    try:
        pack = os.path.join(tmp, "base.pop")
        write_pack(pack, files, verbose=False)
        out_dir = os.path.join(tmp, "music")
        os.mkdir(out_dir)
        r = subprocess.run([renderer, pack, "-R", out_dir, "-M", str(rate), "-o", tmp, "-t", "3600000"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout)
            sys.exit("error: the music renderer failed (%s)" % renderer)
        result = []
        total = 0.0
        for name in sorted(os.listdir(out_dir)):
            if not (name.startswith("MIDI") and name.endswith(".raw")):
                continue
            with open(os.path.join(out_dir, name), "rb") as fh:
                raw = fh.read()
            total += len(raw) / 2 / rate
            result.append((name[:-4] + ".MUS", encode_mus(raw, codec, rate)))
        if not result:
            sys.stderr.write(r.stdout[-2000:])
            print("warning: the renderer (%s) produced no music: the pack is written without it\n"
                  "         (music synthesized live on the console). Please report the lines above." % renderer)
            return result
        if verbose:
            size = sum(len(b) for _, b in result)
            print("music: %d pieces, %.0f s, %s %d Hz, %d bytes" % (len(result), total, codec.upper(), rate, size))
        return result
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def build_pack(out_path, inputs, verbose=True, music=True, renderer=None, codec="pcm", rate=11025):
    files = collect_files(inputs, verbose)
    if music:
        renderer = renderer or default_renderer()
        if renderer:
            files += render_music(files, renderer, codec, rate, verbose)
        elif verbose:
            print("note: music renderer not found (pop_render-* next to this script, 'make harness',\n"
                  "      or --renderer): the music will be synthesized live on the console (slower)")
    return write_pack(out_path, files, verbose)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output", help="pack to write, e.g. 'Prince of Persia.pop'")
    ap.add_argument("inputs", nargs="+", help="game / SDLPoP data / mod folders, highest priority first")
    ap.add_argument("-q", "--quiet", action="store_true")
    ap.add_argument("--no-music", action="store_true", help="do not pre-render the music")
    ap.add_argument("--music-codec", choices=("pcm", "adpcm"), default="pcm",
                    help="pcm: identical sound (default); adpcm: 4 times smaller")
    ap.add_argument("--music-rate", type=int, choices=(11025, 22050), default=11025,
                    help="11025: half the size (default); 22050: identical sound")
    ap.add_argument("--renderer", help="music renderer (harness/build/pop_host by default)")
    args = ap.parse_args()
    for d in args.inputs:
        if not os.path.isdir(d):
            sys.exit("error: %s is not a folder" % d)
    if args.renderer and not os.path.isfile(args.renderer):
        sys.exit("error: renderer %s not found" % args.renderer)
    build_pack(args.output, args.inputs, verbose=not args.quiet, music=not args.no_music,
               renderer=args.renderer, codec=args.music_codec, rate=args.music_rate)


if __name__ == "__main__":
    main()
