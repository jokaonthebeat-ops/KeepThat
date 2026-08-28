# Demo film

| File | Shape | Length | Size |
| --- | --- | --- | --- |
| `KeepThat-Demo.mp4` | 1920x1080, 16:9 | 93 s | 72 MB |
| `KeepThat-Demo.gif` | 720x509, 15 fps | 5 s | 579 KB |

H.264 at 12 Mbit, 30 fps, with AAC audio.

Source: **High Mileage** - Joka Beatz, 150 BPM, A# minor. The film reads the
track from 12 s in, and the muxed audio starts at the same offset, so the music
you hear is the audio the plug-in was analysing when it made each capture. The
KEY and BPM readouts on screen are that analysis, not a caption.

## Acts

| At | Act | What it shows |
| --- | --- | --- |
| 0:00 | Logo opener | Mark, wordmark, the thesis line |
| 0:05 | Already listening | The full interface, empty, buffer filling |
| 0:11 | KEEP LAST | The first capture lands - four bars, trimmed, key-labelled |
| 0:18 | Choose your unit | Every length button cycled: bars, seconds, PHRASE |
| 0:27 | Fill the rack | Seven more captures - all eight slots, each a different waveform |
| 0:45 | Trim | The handles dragged across the capture, then committed |
| 0:56 | Rename | Typed in place on the card |
| 1:05 | Recovery tools | NORMALIZE, AUTO TRIM and SILENCE DETECT toggled live |
| 1:14 | Out of the plug-in | SAVE WAV, then DRAG TO DAW |
| 1:20 | The clock restarts | A capture, and the buffer clock drops to 0:00 |
| 1:26 | Closer | Formats and price |

Everything is the real plug-in. `uishot`-style button handlers are invoked
directly, the capture engine runs on its worker thread, and the frame loop
drains it the moment it finishes - so the time the plug-in spends working on
screen is the time it really takes.

**DRAG TO DAW is shown, not performed.** A real OS drag cannot be driven from a
headless renderer, so that beat displays the control and says what it does.
Demo WAVs are written to a temp folder, never the user's captures folder.

## Rendering it

```bash
make film
build/tools/film out.mp4 fps=30 seconds=93 audio=beat.wav audiostart=12 still=40
afconvert -f m4af -d aac -b 192000 beat.wav beat.m4a
build/tools/mux Marketing/video/KeepThat-Demo.mp4 out.mp4 beat.m4a start=12
build/tools/grab Marketing/video/KeepThat-Demo.mp4 Marketing/video-stills 2 8 14 22 32 40 48 58 68 78 88
```

About seven minutes for the film, eight seconds for the mux.

**Why video and audio are separate steps.** An AVAssetWriter with a video and
an audio input throttles whichever one is behind: appending all the frames and
then the audio wedges the video input in a sleep loop for ever, and
hand-interleaving them stalled too. One input never stalls, so the renderer
writes video only and `mux` composites the audio on afterwards by passthrough -
no re-encode, which is why that step takes seconds.
