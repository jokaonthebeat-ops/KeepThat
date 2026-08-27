#!/usr/bin/env python3
"""
makeicon.py - draws KEEP THAT!'s application icon.

There is no PIL or numpy on this machine, so the icon is rendered from signed
distance fields straight into an RGBA buffer with png.py. Distance fields give
clean antialiasing at any size without supersampling, which matters because
this same 1024 px master is downscaled to 16 px for the Finder list.

The mark is the plugin's own HUD: a red/cyan arc pair broken at the bottom,
around a rewind glyph - go back and keep what just went past.
"""
import math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import png

N = 1024
SIDE = 824.0                 # Apple's icon grid: the shape sits inside a margin
HALF = SIDE / 2.0
SQ_N = 5.0                   # superellipse exponent - the macOS "squircle"

R_ARC   = 292.0              # outer arc centre-line radius
T_ARC   = 46.0               # its thickness
R_TICK  = 224.0
SPAN    = 2.60               # matches the plugin: ~7:00 round the top to ~5:00
GAP_TOP = 0.045              # hairline where red meets cyan at twelve o'clock

GW, GH  = 84.0, 96.0         # rewind triangle: width, half-height
ROUND   = 11.0               # corner rounding, applied as an SDF offset
GLEN    = math.hypot(GW, GH)
GLYPH_X = (-52.0, 52.0)      # centres of the two triangles

RED   = (255,  46,  46)
CYAN  = ( 40, 200, 255)
WHITE = (255, 255, 255)


def smooth(edge0, edge1, x):
    if edge1 == edge0:
        return 0.0 if x < edge0 else 1.0
    t = (x - edge0) / (edge1 - edge0)
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return t * t * (3.0 - 2.0 * t)


def over(dst, src, a):
    """src over dst with alpha a, both straight RGB tuples."""
    return tuple(dst[i] + (src[i] - dst[i]) * a for i in range(3))


def main(out):
    rows = [bytearray(N * 4) for _ in range(N)]
    cx = cy = N / 2.0

    for py in range(N):
        row = rows[py]
        y = py + 0.5 - cy
        for px in range(N):
            x = px + 0.5 - cx

            # ---- the squircle body -------------------------------------
            sq = (abs(x) ** SQ_N + abs(y) ** SQ_N) ** (1.0 / SQ_N)
            body = smooth(HALF + 1.0, HALF - 1.0, sq)

            # A soft contact shadow just below the shape, so the icon sits on
            # a surface rather than floating.
            shy = (abs(x) ** SQ_N + abs(y - 10.0) ** SQ_N) ** (1.0 / SQ_N)
            shadow = smooth(HALF + 26.0, HALF - 4.0, shy) * 0.30 * (1.0 - body)

            if body <= 0.0 and shadow <= 0.0:
                continue

            # ---- ground: a vertical gradient lifted at the top ----------
            t = (y + HALF) / SIDE
            t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
            col = (0x16 + (0x05 - 0x16) * t,
                   0x1b + (0x07 - 0x1b) * t,
                   0x24 + (0x0b - 0x24) * t)

            lift = math.exp(-((x * x + (y + 250.0) ** 2) / (2 * 300.0 ** 2)))
            col = over(col, (60, 78, 104), 0.30 * lift)

            r = math.hypot(x, y)
            ang = math.atan2(x, -y)                 # 0 = top, positive clockwise

            # ---- inner disc, so the glyph has its own ground ------------
            col = over(col, (8, 10, 14), 0.85 * smooth(196.0, 178.0, r))

            # ---- tick ring ---------------------------------------------
            if abs(ang) <= SPAN and abs(r - R_TICK) < 16.0:
                k = (ang + SPAN) / (2 * SPAN) * 56.0
                d = abs(k - round(k))
                tick = smooth(0.34, 0.18, d) * smooth(15.0, 9.0, abs(r - R_TICK))
                shade = CYAN if ang > 0 else RED
                col = over(col, shade, 0.42 * tick)

            # ---- the arc pair ------------------------------------------
            lit = 0.0
            if abs(ang) <= SPAN + 0.12:
                ends = smooth(SPAN + 0.02, SPAN - 0.06, abs(ang))
                mid = smooth(GAP_TOP - 0.02, GAP_TOP + 0.03, abs(ang))
                lit = ends * mid

            if lit > 0.0:
                band = abs(r - R_ARC)
                shade = CYAN if ang > 0 else RED

                # outer glow, then the arc core, then a white-hot filament
                glow = math.exp(-((band / 46.0) ** 2)) * lit
                col = over(col, shade, 0.42 * glow)

                core = smooth(T_ARC * 0.5 + 1.2, T_ARC * 0.5 - 1.2, band) * lit
                col = over(col, shade, core)

                fil = smooth(7.0, 3.0, band) * lit
                col = over(col, WHITE, 0.92 * fil)

            # ---- rewind glyph: two left-pointing triangles --------------
            # Each is the intersection of three half-planes, taken as the
            # largest edge distance, then offset outward by ROUND so the
            # corners come back rounded rather than needle-sharp.
            g = 0.0
            for ox in GLYPH_X:
                gx, gy = x - ox, y
                d = max(gx - GW * 0.5,                                # back edge
                        (-GH * (gx + GW * 0.5) - GW * gy) / GLEN,     # upper
                        (-GH * (gx + GW * 0.5) + GW * gy) / GLEN)     # lower
                g = max(g, smooth(1.5, -1.5, d - ROUND))

            if g > 0.0:
                col = over(col, (255, 244, 244), g)

            # ---- top specular on the squircle rim ----------------------
            rim = smooth(HALF - 3.0, HALF - 0.5, sq) * body
            edge_lift = max(0.0, -y) / HALF
            col = over(col, (150, 175, 205), 0.55 * rim * (0.25 + 0.75 * edge_lift))

            a = body
            if shadow > 0.0 and a < 1.0:
                col = over((0, 0, 0), col, a) if a > 0 else (0, 0, 0)
                a = a + shadow * (1.0 - a)

            i = px * 4
            row[i]     = max(0, min(255, int(col[0] + 0.5)))
            row[i + 1] = max(0, min(255, int(col[1] + 0.5)))
            row[i + 2] = max(0, min(255, int(col[2] + 0.5)))
            row[i + 3] = max(0, min(255, int(a * 255.0 + 0.5)))

    png.write(out, N, N, rows)
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/icon_1024.png")
