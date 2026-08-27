# Demo video

| File | Shape | Length | Size |
| --- | --- | --- | --- |
| `KeepThat-Demo.mp4` | 1280×906, 60 fps | 5.0 s | 4.9 MB |
| `KeepThat-Demo.gif` | 720×509, 15 fps | 5.0 s | 579 KB |

H.264 at 12 Mbit, no audio. Both are the same take.

## Why it is short

The product is one gesture. The whole pitch is "you didn't record it, and now
you have it" — which takes about two seconds to show and does not improve by
being stretched. The loop is built to be watched three times without noticing.

## Structure

| At | Frame | What is happening |
| --- | --- | --- |
| 0:00 | 0 | 1:40 already buffered. Meters moving, marker on the tick ring |
| 0:01.5 | 90 | **KEEP LAST pressed** |
| 0:02 | ~120 | Capture lands: 4-bar waveform, `Keep 1`, counter reads 1/100 |
| 0:05 | 299 | Loops back |

The capture is real. `uishot` invokes the actual button handler, the capture
engine runs on its worker thread, and the frame loop drains it the moment it
finishes — so the number of frames the plug-in spends "busy" in the film is
the number of frames it really takes.

## A longer film, if one is wanted

Not rendered. This is the shot list it would follow:

| At | Act | What it shows |
| --- | --- | --- |
| 0:00 | Logo | Mark, wordmark, "Always-On Idea Capture" |
| 0:04 | The problem | Empty state. Nothing captured yet. Buffer climbing |
| 0:10 | One button | KEEP LAST. The clip lands. Nothing was armed |
| 0:18 | Pick your unit | 1/2/4/8 BARS, 15/30/60 SEC, PHRASE — each selected |
| 0:28 | PHRASE | The card, the confidence, the capture that follows the idea |
| 0:36 | It cleans up | AUTO TRIM / SILENCE / ZERO-CROSS / FADE toggled live |
| 0:46 | It knows the key | Key and BPM readouts filling in |
| 0:52 | Restart | KEEP, and the buffer clock drops to 0:00 |
| 0:58 | The rack | Eight keeps, rename, star, delete, undo |
| 1:08 | Out | Drag to DAW, SAVE WAV, destinations |
| 1:16 | Price card | Free. VST3 / AU / Standalone. Mac + Windows |

`uishot`'s `frames=` and `keepat=` flags already drive everything above; the
missing piece is a caption/act layer over the frames.
