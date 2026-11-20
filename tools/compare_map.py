#!/usr/bin/env python3
"""compare_map.py - diff one of our exported sub-map PNGs against a real
framebuffer captured from the running game.

Everything is compared in **5-bit SNES colour space** (`c >> 3`): the game
emits `(c<<3)|(c>>2)` while snesmaps-style captures truncate to `c<<3`, so the
8-bit values differ even when the 15-bit colour is identical.  Comparing raw
8-bit bytes produces false mismatches.

The real frame is a 256x224 viewport; the export is the whole map (512x512,
16 px per cell).  So we sweep the offset of the viewport inside the export and
report the best match.  Offsets are searched at 1-px resolution around an
expected position when one is supplied.

usage:
    tools/compare_map.py <export.png> <real.png> [options]

options:
    --expect X,Y     expected viewport origin inside the export
    --radius N       search radius in px around --expect (default 40)
    --step N         sample every Nth pixel (default 1)
    --json           machine-readable one-line summary
    --dump-png PATH  write a diff image (red = mismatch)

exit status: 0 if the best match is >= 90 %, else 1.
"""
import json
import struct
import sys
import zlib


# --------------------------------------------------------------------------
# minimal PNG decoder (no external deps)
# --------------------------------------------------------------------------
def load_png(path):
    """Return (width, height, bytes_per_pixel, raw pixel bytes, stride)."""
    data = open(path, "rb").read()
    pos = 8
    w = h = ct = 0
    idat = b""
    plte = b""
    trns = b""
    while pos < len(data):
        (ln,) = struct.unpack(">I", data[pos:pos + 4])
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, _bd, ct = struct.unpack(">IIBB", body[:10])
        elif typ == b"IDAT":
            idat += body
        elif typ == b"PLTE":
            plte = body
        elif typ == b"tRNS":
            trns = body
        elif typ == b"IEND":
            break
        pos += 12 + ln

    if ct == 3:                      # palette -> expand to RGB
        bpp = 1
        raw = _unfilter(zlib.decompress(idat), w, h, bpp)
        out = bytearray(w * h * 3)
        for i in range(w * h):
            idx = raw[i]
            if idx * 3 + 2 < len(plte):
                out[i * 3:i * 3 + 3] = plte[idx * 3:idx * 3 + 3]
            elif trns and idx < len(trns):
                v = trns[idx]
                out[i * 3:i * 3 + 3] = bytes((v, v, v))
        return w, h, 3, bytes(out), w * 3

    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    raw = _unfilter(zlib.decompress(idat), w, h, bpp)
    if bpp == 4:                     # drop alpha
        out = bytearray(w * h * 3)
        for i in range(w * h):
            out[i * 3:i * 3 + 3] = raw[i * 4:i * 4 + 3]
        return w, h, 3, bytes(out), w * 3
    if bpp == 2:                     # gray+alpha
        out = bytearray(w * h * 3)
        for i in range(w * h):
            out[i * 3] = out[i * 3 + 1] = out[i * 3 + 2] = raw[i * 2]
        return w, h, 3, bytes(out), w * 3
    return w, h, bpp, raw, w * bpp


def _unfilter(raw, w, h, bpp):
    stride = w * bpp
    out = bytearray()
    prev = bytearray(stride)
    o = 0
    for _y in range(h):
        f = raw[o]
        o += 1
        row = bytearray(raw[o:o + stride])
        o += stride
        if f == 1:
            for i in range(bpp, stride):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif f == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = row[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                row[i] = (row[i] + pr) & 0xFF
        out += row
        prev = row
    return bytes(out)


class Image:
    """RGB image with 5-bit-quantised lookup."""

    def __init__(self, path):
        self.w, self.h, bpp, raw, stride = load_png(path)
        self.px = [((raw[y * stride + x * bpp] >> 3,
                     raw[y * stride + x * bpp + 1] >> 3,
                     raw[y * stride + x * bpp + 2] >> 3),
                    (raw[y * stride + x * bpp],
                     raw[y * stride + x * bpp + 1],
                     raw[y * stride + x * bpp + 2]))
                   for y in range(self.h) for x in range(self.w)]

    def q(self, x, y):
        return self.px[y * self.w + x][0]

    def rgb(self, x, y):
        return self.px[y * self.w + x][1]


def common_colours(img, frac=0.003):
    """Colours covering more than `frac` of the image (a cheap sprite mask)."""
    from collections import Counter
    c = Counter()
    n = 0
    for y in range(0, img.h, 2):
        for x in range(0, img.w, 2):
            c[img.q(x, y)] += 1
            n += 1
    return {k for k, v in c.items() if v > n * frac}


def build_anim_mask(paths):
    """Pixels that differ between two real captures are ANIMATED -> exclude.

    FF4 animates water/torch tiles by swapping tile graphics (bg_anim.asm +
    MapGfxAnim), so a static export can never match them.  Instead of decoding
    the animation we simply capture the same view twice a few frames apart and
    treat any disagreeing pixel as "animated, not comparable".
    """
    if len(paths) < 2:
        return None
    imgs = [Image(p) for p in paths]
    w = min(i.w for i in imgs)
    h = min(i.h for i in imgs)
    base = imgs[0]
    mask = set()
    for y in range(h):
        for x in range(w):
            v = base.q(x, y)
            for other in imgs[1:]:
                if other.q(x, y) != v:
                    mask.add((x, y))
                    break
    return mask


def match(exp, real, ox, oy, step, mask=None, anim=None):
    """Count matching pixels with the real viewport at (ox,oy) in `exp`."""
    hits = tot = 0
    for y in range(0, real.h - step + 1, step):
        for x in range(0, real.w - step + 1, step):
            if anim is not None and (x, y) in anim:
                continue
            r = real.q(x, y)
            if mask is not None and r not in mask:
                continue
            ex, ey = ox + x, oy + y
            if 0 <= ex < exp.w and 0 <= ey < exp.h:
                tot += 1
                if exp.q(ex, ey) == r:
                    hits += 1
    return hits, tot


def write_diff(path, exp, real, ox, oy, mask=None):
    w, h = real.w, real.h
    rgb = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            ex, ey = ox + x, oy + y
            ok = (0 <= ex < exp.w and 0 <= ey < exp.h and
                  exp.q(ex, ey) == real.q(x, y))
            if mask is not None and real.q(x, y) not in mask:
                v = real.rgb(x, y)                    # ignored -> show real
            elif ok:
                v = tuple(c >> 1 for c in real.rgb(x, y))   # dim = match
            else:
                v = (255, 0, 0)                       # red = mismatch
            rgb[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes(v)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body +
                struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    exp = Image(argv[1])
    real = Image(argv[2])
    expect = None
    radius = 40
    step = 1
    as_json = False
    diff_path = None
    mask_frames = []
    i = 3
    while i < len(argv):
        a = argv[i]
        if a == "--expect":
            i += 1
            expect = tuple(int(v) for v in argv[i].split(","))
        elif a == "--radius":
            i += 1
            radius = int(argv[i])
        elif a == "--step":
            i += 1
            step = int(argv[i])
        elif a == "--mask-frame":
            i += 1
            mask_frames.append(argv[i])
        elif a == "--json":
            as_json = True
        elif a == "--dump-png":
            i += 1
            diff_path = argv[i]
        else:
            print("unknown option %s" % a)
            return 2
        i += 1

    if expect is None:
        # default: assume the viewport is centred on the export
        expect = ((exp.w - real.w) // 2, (exp.h - real.h) // 2)

    mask = common_colours(real)
    anim = build_anim_mask(mask_frames) if mask_frames else None
    best = (0, 0, -1)
    seen = 0
    for oy in range(expect[1] - radius, expect[1] + radius + 1):
        for ox in range(expect[0] - radius, expect[0] + radius + 1):
            if ox < 0 or oy < 0 or ox + real.w > exp.w or oy + real.h > exp.h:
                continue
            seen += 1
            hits, _tot = match(exp, real, ox, oy, step, None, anim)
            if hits > best[0]:
                best = (hits, ox, oy)
    if best[2] < 0:
        print("no valid offset in range")
        return 1

    hits, ox, oy = best
    _h2, tot = match(exp, real, ox, oy, step, None, anim)
    bh, btot = match(exp, real, ox, oy, step, mask, anim)
    pct = 100.0 * hits / tot if tot else 0.0
    bpct = 100.0 * bh / btot if btot else 0.0

    if diff_path:
        write_diff(diff_path, exp, real, ox, oy, mask)

    if as_json:
        print(json.dumps({"export": argv[1], "real": argv[2],
                          "export_size": [exp.w, exp.h],
                          "real_size": [real.w, real.h],
                          "expect": list(expect),
                          "offset": [ox, oy],
                          "offset_delta": [ox - expect[0], oy - expect[1]],
                          "match_pct": round(pct, 2),
                          "bg_match_pct": round(bpct, 2),
                          "animated_px": len(anim) if anim else 0,
                          "searched": seen}))
    else:
        print("%s vs %s" % (argv[1], argv[2]))
        print("  export %dx%d   real frame %dx%d" % (exp.w, exp.h,
                                                     real.w, real.h))
        print("  expected origin (%d,%d)   searched %d offsets"
              % (expect[0], expect[1], seen))
        print("  best origin     (%d,%d)   delta (%+d,%+d)"
              % (ox, oy, ox - expect[0], oy - expect[1]))
        if anim is not None:
            print("  animated px masked: %d" % len(anim))
        print("  match           %d/%d = %.2f %%" % (hits, tot, pct))
        print("  bg-only match   %d/%d = %.2f %%" % (bh, btot, bpct))
    return 0 if pct >= 90.0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
