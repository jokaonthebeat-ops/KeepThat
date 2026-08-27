# Art export request — KEEP THAT!

What to export from the layered source the approved mockup was rendered from,
so the plugin can use the real artwork instead of a hand-drawn approximation.

**Canvas for every export: exactly 1491 × 1055**, PNG-24 with alpha, no
background matte. Keep every layer in its mockup position — do not crop or
trim to content, because position is what makes each file drop straight in. A
2× set (2982 × 2110) as well would be ideal for retina; if that is extra work,
1× is enough to start and 2× can follow.

Drop finished files into `~/Documents/KeepThat/Assets/`. Filenames below are
what the plugin will look for, so please use them exactly.

---

## 1. `shell.png` — the one that matters most

If only one file gets exported, make it this one.

**Include:** the background and its texture/vignette, every panel surface with
its borders, bevels, corner radii and inner shadows, the sunken wells (waveform
beds, meter beds, readout cells), the KEEP LAST housing with its perforated
dot columns and brackets, the bottom strip's cell dividers, the header's
hairline, the footer's gold rule.

**Turn OFF:** all text of any kind, all readout values, all waveforms, meter
segments, the ring (arcs, ticks, dashes, inner disc), knob caps and pointers,
toggle switches, the recent-keeps card contents, the playhead and trim handles,
selection highlights and any glow that belongs to a lit control.

The test: it should look like the plugin with the power off.

## 2. `keep_last.png` — the focal point

The KEEP LAST slab on its own, transparent background, **with no text on it**
and no outer glow baked in. Just the slab: dark face, bevel, edge outline.

If it is easy, also `keep_last_hover.png` and `keep_last_down.png`. If not,
one state is fine — I will derive hover and pressed from it.

## 3. Ring layers — this is the one that needs splitting

The ring has to animate, so it cannot come as one flat image. Two options,
either is fine:

**Preferred — three files:**
- `ring_static.png` — the tick ring, the dashed segment ring and the inner
  perforated disc. Everything that does not change.
- `ring_arc_red.png` — the red arcs ONLY, drawn as a **full 360° sweep** at
  their correct radii, on transparency. I clip it to whatever angle the buffer
  state calls for.
- `ring_arc_cyan.png` — same, in cyan.

**Simpler alternative — two files:**
- `ring_static.png` as above.
- `ring_arcs.png` — the arcs exactly as they appear in the mockup. The ring
  then stops reflecting buffer fill, which loses something real, so the
  three-file version is worth the extra step if it is available.

## 4. Controls — nice to have, in priority order

- `knob.png` — one knob at its mockup size: cap, rim, recess, **no pointer and
  no red indicator arc** (both get drawn live so they can move). If a filmstrip
  is easier than a single frame, 61 or 128 frames vertically stacked also works
  — say which and I will read it that way.
- `toggle_on.png` and `toggle_off.png` — the pill switches, transparent.
- `logo.png` — the brand mark and "KEEP THAT!" wordmark with the subtitle,
  transparent, at mockup size. This one would fix the typeface mismatch in the
  header outright.
- `header_icons.png` or individual SVGs — SAVE, SETTINGS, HELP, UNDO, REDO,
  POWER. SVG is better than PNG here if the source has it.

## 5. Fonts

The single largest remaining difference is typography: the mockup's face is a
condensed technical grotesque that does not ship on macOS, so the build is
falling back to Inter / SF Pro Display with a horizontal squeeze. **The name of
the font used in the mockup** would resolve most of the residual text drift —
and if it is licensed for embedding, the font file itself would resolve all of
it.

---

## What happens when these land

Assets are loaded from the bundle's `Contents/Resources/Assets`, and any file
that fails to load is **reported by name** rather than silently falling back —
`make uishot` prints the failures and exits non-zero. That matters: a
wrong-looking interface is far more often a load problem than an art problem,
and this makes the difference obvious instead of a guess.

Nothing already built gets thrown away. The live drawing stays as the fallback
path for anything not supplied, so a partial export is genuinely useful — send
`shell.png` alone and the interface improves immediately.
