#!/usr/bin/env python3
"""
Minimal PNG read/write in pure Python - this machine has no PIL and no numpy.

Used for two jobs:

  * measuring the approved mockup (where does a panel edge actually fall?)
  * comparing a `make uishot` render against that mockup

Commands:
  probe   FILE X Y [X Y ...]      pixel values at coordinates
  row     FILE Y [X0 X1]          run-length summary of a scanline
  col     FILE X [Y0 Y1]          run-length summary of a column
  edges   FILE AXIS N [LO HI]     luminance jumps along a row/column
  crop    IN OUT X Y W H          write a sub-rectangle
  scale   IN OUT DIV              integer box-downscale (DIV = 2, 3, ...)
  diff    A B OUT                 heat map + numeric report
  overlay A B OUT [ALPHA]         B blended over A
  filmstrip IN OUT SRCN DSTN      resample a vertical filmstrip
"""

import sys
import zlib
import struct


# -----------------------------------------------------------------------------
#  Read
# -----------------------------------------------------------------------------
def read(path):
    """Returns (width, height, rows) where each row is a bytearray of RGBA."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG: " + path

    pos, idat, pal, trns = 8, bytearray(), None, None
    w = h = depth = ctype = 0

    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length

        if ctag == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8, "only 8-bit PNGs are supported (got %d)" % depth
            assert interlace == 0, "interlaced PNGs are not supported"
        elif ctag == b"PLTE":
            pal = body
        elif ctag == b"tRNS":
            trns = body
        elif ctag == b"IDAT":
            idat += body
        elif ctag == b"IEND":
            break

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(bytes(idat))
    stride = w * channels

    # Undo the per-scanline filters.
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        ftype = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride

        if ftype == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        rows.append(line)
        prev = line

    # Normalise everything to RGBA.
    out = []
    for line in rows:
        rgba = bytearray(w * 4)
        for x in range(w):
            if ctype == 6:
                rgba[x * 4:x * 4 + 4] = line[x * 4:x * 4 + 4]
            elif ctype == 2:
                rgba[x * 4:x * 4 + 3] = line[x * 3:x * 3 + 3]
                rgba[x * 4 + 3] = 255
            elif ctype == 0:
                v = line[x]
                rgba[x * 4:x * 4 + 4] = bytes((v, v, v, 255))
            elif ctype == 4:
                v = line[x * 2]
                rgba[x * 4:x * 4 + 4] = bytes((v, v, v, line[x * 2 + 1]))
            else:  # palette
                i = line[x]
                rgba[x * 4:x * 4 + 3] = pal[i * 3:i * 3 + 3]
                rgba[x * 4 + 3] = trns[i] if (trns and i < len(trns)) else 255
        out.append(rgba)
    return w, h, out


# -----------------------------------------------------------------------------
#  Write
# -----------------------------------------------------------------------------
def write(path, w, h, rows):
    raw = bytearray()
    for line in rows:
        raw.append(0)          # filter: none, these are small one-off files
        raw += line
    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


def px(rows, x, y):
    return tuple(rows[y][x * 4:x * 4 + 4])


def lum(p):
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]


def hexof(p):
    return "#%02x%02x%02x" % p[:3] + ("" if p[3] == 255 else " a=%d" % p[3])


# -----------------------------------------------------------------------------
#  Commands
# -----------------------------------------------------------------------------
def cmd_probe(args):
    w, h, rows = read(args[0])
    print("%s  %dx%d" % (args[0], w, h))
    for i in range(1, len(args) - 1, 2):
        x, y = int(args[i]), int(args[i + 1])
        print("  (%4d,%4d)  %s" % (x, y, hexof(px(rows, x, y))))


def _runs(vals, coords):
    out, start, cur = [], 0, vals[0]
    for i in range(1, len(vals)):
        if vals[i] != cur:
            out.append((coords[start], coords[i - 1], cur))
            start, cur = i, vals[i]
    out.append((coords[start], coords[-1], cur))
    return out


def cmd_line(args, vertical):
    w, h, rows = read(args[0])
    n = int(args[1])
    lo = int(args[2]) if len(args) > 2 else 0
    hi = int(args[3]) if len(args) > 3 else (h - 1 if vertical else w - 1)
    lo = max(0, lo)
    hi = min(hi, (h - 1) if vertical else (w - 1))
    coords = list(range(lo, hi + 1))
    vals = [hexof(px(rows, n, c) if vertical else px(rows, c, n)) for c in coords]
    print("%s  %s=%d  %s %d..%d" % (args[0], "x" if vertical else "y", n,
                                    "y" if vertical else "x", lo, hi))
    for a, b, v in _runs(vals, coords):
        if b - a >= 1 or True:
            print("  %4d..%-4d (%3d)  %s" % (a, b, b - a + 1, v))


def cmd_edges(args):
    """Luminance jumps - where a panel border or a control edge actually is."""
    w, h, rows = read(args[0])
    axis, n = args[1], int(args[2])
    lo = int(args[3]) if len(args) > 3 else 0
    hi = int(args[4]) if len(args) > 4 else ((w - 1) if axis == "row" else (h - 1))
    thresh = float(args[5]) if len(args) > 5 else 12.0
    lo = max(0, lo)
    hi = min(hi, (w - 1) if axis == "row" else (h - 1))
    vals = [lum(px(rows, c, n)) if axis == "row" else lum(px(rows, n, c))
            for c in range(lo, hi + 1)]
    print("%s  %s %d, %d..%d  (jump > %.0f)" % (args[0], axis, n, lo, hi, thresh))
    for i in range(1, len(vals)):
        d = vals[i] - vals[i - 1]
        if abs(d) > thresh:
            print("  %4d  %6.1f -> %6.1f  %+7.1f" % (lo + i, vals[i - 1], vals[i], d))


def cmd_crop(args):
    src, dst = args[0], args[1]
    x, y, cw, ch = (int(v) for v in args[2:6])
    w, h, rows = read(src)
    out = [rows[yy][x * 4:(x + cw) * 4] for yy in range(y, y + ch)]
    write(dst, cw, ch, out)
    print("wrote %s (%dx%d from %d,%d)" % (dst, cw, ch, x, y))


def cmd_scale(args):
    src, dst, d = args[0], args[1], int(args[2])
    w, h, rows = read(src)
    ow, oh = w // d, h // d
    out = []
    for y in range(oh):
        line = bytearray(ow * 4)
        for x in range(ow):
            acc = [0, 0, 0, 0]
            for dy in range(d):
                r = rows[y * d + dy]
                for dx in range(d):
                    o = (x * d + dx) * 4
                    for c in range(4):
                        acc[c] += r[o + c]
            n = d * d
            line[x * 4:x * 4 + 4] = bytes(a // n for a in acc)
        out.append(line)
    write(dst, ow, oh, out)
    print("wrote %s (%dx%d)" % (dst, ow, oh))


def cmd_diff(args):
    aw, ah, a = read(args[0])
    bw, bh, b = read(args[1])
    if (aw, ah) != (bw, bh):
        print("SIZE MISMATCH: %dx%d vs %dx%d" % (aw, ah, bw, bh))
        return 2
    out, total, big = [], 0, 0
    for y in range(ah):
        line = bytearray(aw * 4)
        for x in range(aw):
            pa = a[y][x * 4:x * 4 + 3]
            pb = b[y][x * 4:x * 4 + 3]
            d = max(abs(pa[0] - pb[0]), abs(pa[1] - pb[1]), abs(pa[2] - pb[2]))
            total += d
            if d > 60:
                big += 1
            v = min(255, d * 3)
            # green = close, red = far
            line[x * 4:x * 4 + 4] = bytes((v, 255 - v, 40, 255))
        out.append(line)
    write(args[2], aw, ah, out)
    n = aw * ah
    print("mean channel delta %.1f   pixels off by >60: %d (%.1f%%)"
          % (total / n, big, 100.0 * big / n))
    print("wrote %s" % args[2])


def cmd_overlay(args):
    aw, ah, a = read(args[0])
    bw, bh, b = read(args[1])
    alpha = float(args[3]) if len(args) > 3 else 0.5
    out = []
    for y in range(min(ah, bh)):
        line = bytearray(aw * 4)
        for x in range(min(aw, bw)):
            for c in range(3):
                pa = a[y][x * 4 + c]
                pb = b[y][x * 4 + c]
                line[x * 4 + c] = int(pa * (1 - alpha) + pb * alpha)
            line[x * 4 + 3] = 255
        out.append(line)
    write(args[2], aw, min(ah, bh), out)


COMMANDS = {
    "probe": cmd_probe,
    "row": lambda a: cmd_line(a, False),
    "col": lambda a: cmd_line(a, True),
    "edges": cmd_edges,
    "crop": cmd_crop,
    "scale": cmd_scale,
    "diff": cmd_diff,
    "overlay": cmd_overlay,
}



# -----------------------------------------------------------------------------
#  Filmstrip decimation
#
#  Halving a vertically-stacked filmstrip. Frame indices are chosen so the
#  endpoints survive exactly - round(i * (src-1) / (dst-1)) - because the first
#  and last frames are a knob's hard min and max, and losing either shows.
# -----------------------------------------------------------------------------
def _filtered_idat(w, h, rows):
    """Per-row adaptive filtering (none/sub/up), which matters a lot on the
    smooth gradients in a knob render - unfiltered these files triple."""
    raw = bytearray()
    prev = bytearray(w * 4)
    for line in rows:
        stride = w * 4
        none = line
        sub = bytearray(stride)
        up = bytearray(stride)
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            sub[i] = (line[i] - a) & 0xFF
            up[i] = (line[i] - prev[i]) & 0xFF
        best, ftype = none, 0
        for cand, t in ((sub, 1), (up, 2)):
            if sum(min(b, 256 - b) for b in cand) < sum(min(b, 256 - b) for b in best):
                best, ftype = cand, t
        raw.append(ftype)
        raw += best
        prev = line
    return zlib.compress(bytes(raw), 9)


def cmd_filmstrip(args):
    src, dst, src_frames, dst_frames = args[0], args[1], int(args[2]), int(args[3])
    w, h, rows = read(src)
    fh = h // src_frames
    out = []
    for i in range(dst_frames):
        s = int(round(i * (src_frames - 1) / (dst_frames - 1)))
        out.extend(rows[s * fh:(s + 1) * fh])

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))
    with open(dst, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, len(out), 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", _filtered_idat(w, len(out), out)))
        f.write(chunk(b"IEND", b""))
    print("wrote %s  %d frames of %dx%d" % (dst, dst_frames, w, fh))


COMMANDS["filmstrip"] = cmd_filmstrip


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in COMMANDS:
        print(__doc__)
        sys.exit(1)
    sys.exit(COMMANDS[sys.argv[1]](sys.argv[2:]) or 0)
