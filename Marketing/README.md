# KEEP THAT! — marketing assets

Everything here is generated from the real plugin. The screenshots and every
frame of the demo are the actual editor rendering actual state: the waveform
in the capture preview is a real capture, made by a real KEEP LAST press
during the render, and the meters, buffer clock and sweep marker are all
reading live audio. Nothing here is a mockup.

## What's here

| File | What it is |
| --- | --- |
| `SALES-PAGE.md` | Long-form copy: the product page script |
| `STORE-LISTINGS.txt` | Store form fields, plain text, no markdown |
| `screenshots/*-2237.png` | Full-resolution masters (2237×1583, the editor's 150% size) |
| `screenshots/*-1491.png` | Masters at the design size (1491×1055) |
| `screenshots/web/*.jpg` | Quality-84 JPEGs at 1600 px for web use |
| `video/KeepThat-Demo.mp4` | 5 s demo, 1280×906, 60 fps, H.264 |
| `video/KeepThat-Demo.gif` | The same take as a looping GIF, 579 KB, for READMEs |
| `video-stills/` | One frame per beat of the demo, for checking without scrubbing |
| `logo/` | App icon at 1024 and 512, the `.icns`, and the wordmark |

## The screenshots

| File | State |
| --- | --- |
| `keepthat-hero` | Populated session — eight keeps, phrase detected, 4:27 buffered |
| `keepthat-empty` | What a customer sees on first open: no keeps, readouts at `--` |
| `keepthat-captured` | Immediately after a real KEEP LAST — one keep, waveform loaded |
| `keepthat-settings` | Settings: buffer restart, low power, reduce motion, folders |
| `keepthat-help` | The built-in reference |

Masters are at **2237×1583**, which is the editor's own 150% size — the
largest it renders natively. They are not upscaled.

## The demo

| At | What it shows |
| --- | --- |
| 0:00 | Buffer filling, meters live, sweep marker moving on the tick ring |
| 0:01.5 | Still empty — "Nothing captured yet", "No keeps yet" |
| 0:02 | **KEEP LAST pressed** |
| 0:02+ | 4-bar waveform lands in CAPTURE PREVIEW, "Keep 1" appears in the rack |
| 0:05 | Loops |

Rendered with `build/tools/uishot <out> def fill=100 frames=300 keepat=90`,
which drives the real capture engine and presses the real button. Encoded by
`tools/mkvideo.m` (AVFoundation) and `tools/gif.py` — there is no ffmpeg on
the build machine and neither tool needs one.

## Regenerating

```bash
make uishot                                  # rebuild the render harness
build/tools/uishot Marketing/screenshots/keepthat-hero-2237.png 2237x1583 demo settle=220
```

> **Careful:** the macOS filesystem is case-insensitive, so `Marketing/` and
> `marketing/` are the same folder. Never `rm -rf` one while building the
> other.
