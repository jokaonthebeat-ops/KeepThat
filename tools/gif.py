#!/usr/bin/env python3
"""
gif.py - animated GIF encoder, written because this machine has no ffmpeg,
no PIL and no numpy.

Takes a numbered PNG sequence (as produced by `uishot ... frames=N`) and
writes a looping GIF89a.

Three things keep the file small enough to put in a README or a store page:

  * a global palette built by median cut over a sample of every frame, so
    the neon reds and cyans survive quantisation;
  * frame differencing - a pixel identical to the previous frame is written
    as the transparent index, and with disposal method 1 the previous pixel
    simply stays put. The interface is mostly static, so most of each frame
    after the first costs nothing;
  * LZW, which then compresses those long transparent runs very hard.

Usage:
    gif.py out.gif <frame glob prefix> [--fps N] [--width W] [--stride S]
"""
import sys, os, glob, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import png


# ---------------------------------------------------------------- palette ---
def median_cut(colours, want):
    """colours: list of (r,g,b). Returns up to `want` representative colours."""
    boxes = [colours]
    while len(boxes) < want:
        # split the box with the largest spread on its widest channel
        target, best = None, -1
        for b in boxes:
            if len(b) < 2:
                continue
            for ch in range(3):
                lo = min(c[ch] for c in b)
                hi = max(c[ch] for c in b)
                if hi - lo > best:
                    best, target, axis = hi - lo, b, ch
        if target is None or best <= 0:
            break
        target.sort(key=lambda c: c[axis])
        mid = len(target) // 2
        boxes.remove(target)
        boxes.append(target[:mid])
        boxes.append(target[mid:])

    out = []
    for b in boxes:
        if not b:
            continue
        n = len(b)
        out.append((sum(c[0] for c in b) // n,
                    sum(c[1] for c in b) // n,
                    sum(c[2] for c in b) // n))
    return out


def build_palette(frames, want=255, sample_step=11):
    seen = {}
    for w, h, rows in frames:
        for y in range(0, h, 3):
            row = rows[y]
            for x in range(0, w * 4, 4 * sample_step):
                c = (row[x], row[x + 1], row[x + 2])
                seen[c] = seen.get(c, 0) + 1
    colours = list(seen.keys())
    if len(colours) <= want:
        return colours
    return median_cut(colours, want)


# -------------------------------------------------------------------- LZW ---
def lzw_encode(indices, min_code_size):
    clear = 1 << min_code_size
    eoi = clear + 1
    code_size = min_code_size + 1
    table = {(i,): i for i in range(clear)}
    next_code = eoi + 1

    out = bytearray()
    bitbuf = bitcnt = 0

    def emit(code):
        nonlocal bitbuf, bitcnt
        bitbuf |= code << bitcnt
        bitcnt += code_size
        while bitcnt >= 8:
            out.append(bitbuf & 0xFF)
            bitbuf >>= 8
            bitcnt -= 8

    emit(clear)
    prefix = ()
    for idx in indices:
        nxt = prefix + (idx,)
        if nxt in table:
            prefix = nxt
            continue
        emit(table[prefix])
        table[nxt] = next_code
        next_code += 1
        if next_code > (1 << code_size):
            if code_size < 12:
                code_size += 1
            else:
                emit(clear)
                table = {(i,): i for i in range(clear)}
                next_code = eoi + 1
                code_size = min_code_size + 1
        prefix = (idx,)
    if prefix:
        emit(table[prefix])
    emit(eoi)
    if bitcnt:
        out.append(bitbuf & 0xFF)
    return bytes(out)


def blockify(data):
    out = bytearray()
    for i in range(0, len(data), 255):
        chunk = data[i:i + 255]
        out.append(len(chunk))
        out += chunk
    out.append(0)
    return bytes(out)


# ------------------------------------------------------------------ scale ---
def downscale(w, h, rows, target_w):
    if target_w >= w:
        return w, h, rows
    k = w / float(target_w)
    tw = target_w
    th = max(1, int(round(h / k)))
    out = []
    for y in range(th):
        sy0 = int(y * k)
        sy1 = max(sy0 + 1, int((y + 1) * k))
        row = bytearray(tw * 4)
        for x in range(tw):
            sx0 = int(x * k)
            sx1 = max(sx0 + 1, int((x + 1) * k))
            r = g = b = n = 0
            for yy in range(sy0, min(sy1, h)):
                src = rows[yy]
                for xx in range(sx0, min(sx1, w)):
                    j = xx * 4
                    r += src[j]; g += src[j + 1]; b += src[j + 2]; n += 1
            j = x * 4
            row[j] = r // n; row[j + 1] = g // n; row[j + 2] = b // n; row[j + 3] = 255
        out.append(row)
    return tw, th, out


# ------------------------------------------------------------------ write ---
def write_gif(path, frames, palette, delay_cs, transparent):
    w, h = frames[0][0], frames[0][1]
    bits = 1
    while (1 << bits) < len(palette) + 1:
        bits += 1
    size = 1 << bits

    table = bytearray()
    for c in palette:
        table += bytes(c)
    table += bytes(3 * (size - len(palette)))

    lut = {}

    def nearest(c):
        v = lut.get(c)
        if v is None:
            best, bi = 1 << 30, 0
            for i, p in enumerate(palette):
                d = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2
                if d < best:
                    best, bi = d, i
            lut[c] = v = bi
        return v

    out = bytearray(b"GIF89a")
    out += struct.pack("<HH", w, h)
    out.append(0xF0 | (bits - 1))          # global table, 8-bit colour
    out += bytes([0, 0])
    out += table
    out += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00"   # loop forever

    prev = None
    for fi, (fw, fh, rows) in enumerate(frames):
        idx = bytearray(w * h)
        k = 0
        for y in range(h):
            row = rows[y]
            prow = prev[y] if prev is not None else None
            for x in range(w):
                j = x * 4
                c = (row[j], row[j + 1], row[j + 2])
                if prow is not None and prow[j] == row[j] \
                   and prow[j + 1] == row[j + 1] and prow[j + 2] == row[j + 2]:
                    idx[k] = transparent
                else:
                    idx[k] = nearest(c)
                k += 1

        out += b"\x21\xF9\x04"
        out.append(0x05 if fi else 0x01)   # disposal 1, transparency on
        out += struct.pack("<H", delay_cs)
        out.append(transparent)
        out.append(0)

        out += b"\x2C" + struct.pack("<HHHH", 0, 0, w, h) + b"\x00"
        mcs = max(2, bits)
        out.append(mcs)
        out += blockify(lzw_encode(idx, mcs))
        prev = rows

    out += b"\x3B"
    open(path, "wb").write(bytes(out))
    return len(out)


def main():
    out_path, prefix = sys.argv[1], sys.argv[2]
    fps, width, stride = 15, 720, 1
    for a in sys.argv[3:]:
        if a.startswith("--fps="):    fps = int(a.split("=")[1])
        if a.startswith("--width="):  width = int(a.split("=")[1])
        if a.startswith("--stride="): stride = int(a.split("=")[1])

    files = sorted(glob.glob(prefix + "_*.png"))[::stride]
    if not files:
        print("no frames matching " + prefix + "_*.png")
        return 1
    print("reading %d frames..." % len(files))

    frames = []
    for i, f in enumerate(files):
        w, h, rows = png.read(f)
        frames.append(downscale(w, h, rows, width))
        if (i + 1) % 10 == 0:
            print("  scaled %d/%d" % (i + 1, len(files)))

    print("building palette...")
    palette = build_palette(frames, want=255)
    print("  %d colours" % len(palette))

    print("encoding...")
    n = write_gif(out_path, frames, palette, max(2, round(100.0 / fps)), len(palette))
    print("wrote %s  (%d frames, %dx%d, %.1f KB)"
          % (out_path, len(frames), frames[0][0], frames[0][1], n / 1024.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
