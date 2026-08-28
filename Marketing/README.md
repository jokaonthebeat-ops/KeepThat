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
| `video/KeepThat-Demo.mp4` | 93 s demo film, 1920×1080, 30 fps, H.264 + AAC |
| `video/KeepThat-Reel.mp4` | The same film vertical, 1080×1920, for reels |
| `video/KeepThat-Demo.gif` | The same take as a looping GIF, 579 KB, for READMEs |
| `video-stills/` | One frame per act, pulled from the film with `tools/grab` |
| `video-stills/reel/` | The same moments from the vertical cut |
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

## The film

93 seconds, ten acts - see `video/README.md` for the shot list. Shot against
**High Mileage** (Joka Beatz, 150 BPM, A# minor); the KEY and BPM on screen are
the plug-in's own analysis of that track.

## Regenerating

```bash
make uishot                                  # rebuild the render harness
build/tools/uishot Marketing/screenshots/keepthat-hero-2237.png 2237x1583 demo settle=220
```

> **Careful:** the macOS filesystem is case-insensitive, so `Marketing/` and
> `marketing/` are the same folder. Never `rm -rf` one while building the
> other.
