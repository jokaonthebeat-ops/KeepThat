# Milestone 2 — the capture engine

Milestone 1 was the interface with placeholder data. This is the engine behind
it: KEEP LAST now pulls real audio out of the rolling buffer, processes it off
the audio thread, and hands back a clip you can play, trim, save and drag.

## The threading contract

Straight from `13_JUCE_HANDOFF/THREADING_EXPORT_SPEC.md`, and enforced in code:

| thread | what it may do |
| --- | --- |
| audio | ring write, meters, preview mix, output gain. No allocation, no locks, no disk |
| message | UI, posting requests, freeing what the audio thread handed back |
| worker | snapshot, phrase detection, zero-cross, trim, fades, normalize, thumbnails, WAV writing |

Two places this needed real care:

**The snapshot is taken on the press, not when the worker gets to it.**
`CaptureEngine::requestCapture` reads the ring on the message thread and hands
the audio to the worker. So "the last four bars" always means the four bars
before the button went down, even if the worker is busy finishing something
else.

**The preview player never frees on the audio thread.** Both threads share the
clip pointer behind a `SpinLock` that the audio thread only ever `tryEnter`s —
if the message thread holds it at that instant, playback continues with what it
has for one block rather than waiting. And when the audio thread takes a new
clip it hands the old one back into a `retired` slot; the message thread frees
it on the next tick. See `capture/PreviewPlayer.h`.

## What each part does

- **`capture/CaptureEngine`** — the recovery path. Bar lengths use the host's
  tempo and time signature when it has one and fall back to a stated BPM when
  it does not. PHRASE pulls a 20-second window and lets the detector decide.
- **`capture/PhraseDetector`** — amplitude and silence-gap heuristics over a
  10 ms energy envelope, with the noise floor estimated from the envelope's
  quietest decile. Returns the *last* active run, because that is what KEEP
  LAST means. Confidence multiplies "how far above the floor" by "how cleanly
  bounded", so a wall of unbroken sound scores low — there is no phrase visible
  in it, and saying otherwise would be a lie.
- **`capture/ClipProcessor`** — silence trim, zero-crossing snap, equal-power
  fades, normalize, thumbnails. Free functions over a buffer, so they are
  trivially testable.
- **`export/WavExporter`** — 24-bit WAV writing through a staging file that is
  renamed on success, so a failure never leaves a half-written file where a
  good one was. Drag-out writes the file *before* starting the OS drag.
- **`params/Parameters`** — the twelve parameters from
  `APVTS_PARAMETER_PLAN.md`, plus `autoTrimEnabled` and `fadeEnabled` (the
  panel has switches for both and they have to automate and persist like the
  rest). `ui/ParameterLink.h` binds them to the custom controls with
  `juce::ParameterAttachment`, including gesture begin/end so automation
  recording is not a burst of unrelated writes.

## A fresh instance starts empty

The recent-keeps list, the capture preview and the buffer clock all start at
nothing, and the readouts show `--` until real audio arrives. A capture tool
that opens claiming 4:27 of history it does not have is the one lie this
product cannot afford. `make uishot ARGS="out.png def demo"` populates the
approved mockup's state for reference and marketing shots.

The HUD ring reports buffer fill against its own labelled span — the art says
`-4:00` at nine o'clock, so four minutes fills it. At 45 seconds most of the
ring is dark; at 4:27 it is fully lit, which is the approved state exactly.

## Tests

`make dsptest` — 94 checks, all passing. It runs as part of `make test`.

The harness **validates itself first**: `selfCheck` builds signals whose
properties are known by construction (a 0.5 sine has peak 0.5 and RMS 0.354)
and asserts the measuring code agrees. A suite whose measurements are wrong
passes everything, which is worse than having no suite.

Four real bugs it caught, all of which would have shipped:

- **Silence trim never worked on real audio.** The run-length test was made on
  raw samples, and audio crosses zero constantly — a 440 Hz sine dips under any
  sensible threshold twice per cycle, so the required run never accumulated and
  the trim silently returned the whole clip. It now measures an envelope.
- **A background phrase scan could block a KEEP LAST press.** The engine had a
  single busy flag, so the once-a-second scan that keeps the PHRASE card
  current could turn away the user's press. Captures and scans now have
  separate flags, and a capture displaces a pending scan.
- **Key detection rejected every key it correctly identified** — see below.
- **A held tone was given a confident tempo** — see below.

## The three gaps, now closed

**Key detection.** `capture/KeyDetector` builds a chroma from overlapping 4096
FFT frames (65 Hz – 2 kHz, where pitch is actually legible) and correlates it
against the Krumhansl-Kessler major and minor profiles at all twelve
rotations. It runs alongside the phrase scan, which already wants the same
recent audio, and every capture carries the key it was recorded in.

Confidence leads on how well the winning profile *fits*, with the margin over
the runner-up as a modifier. Leading on the margin was wrong and the tests
caught it: a key's nearest rival is its relative major or minor, which shares
all seven pitch classes, so C major scored a confident fit and was then
rejected for not beating A minor by enough. Below `minimumConfidence` the
readout stays `--`, because half the material thrown at this is a drum loop
with no key in it and inventing one is worse than saying nothing.

**Destinations are real targets now.** Right-click SAMPLER (or PLAYLIST,
FOLDER, DESKTOP) and pick the folder it writes to; it persists with the
session. Point SAMPLER at your sampler's own library folder and captures land
there — which is the honest form of "sampler integration" for a plugin that
cannot know which sampler you use. PLAYLIST additionally maintains a real
`.m3u`, so the button does what its name says. Hovering a button shows where
it actually writes.

**Undo/Redo work.** A single `juce::UndoManager` covers session actions —
delete, rename, favourite, capture — and the header buttons dim when there is
nothing to undo. Parameters are deliberately *not* in the history: hooking an
UndoManager to the APVTS turns every automation frame into an undo step and
fills the history with moves the user never made.

Deleting a keep is the case that matters. It no longer deletes the file, only
the entry, so undo brings the clip back intact — losing takes is the one thing
this plugin exists to prevent. A capture that pushes past the capacity limit
also remembers the keep it evicted, so undo restores that too.

## Background loading, and tempo without a host

**Clips load off the message thread.** `capture/ClipLoader` reads a restored
capture's WAV on a low-priority worker, so selecting a card never stalls the
interface — on a cold external drive that pause was long enough to look
broken. While a read is in flight the preview says "Reading capture…" rather
than showing an empty waveform that looks like a capture which failed, and
PLAY / SAVE WAV / DRAG queue behind the load instead of reporting "nothing to
play". Selecting a card also warms its neighbours, since the next audition is
usually one of them.

Results come back against a **stable clip id**, not a list index. A load
started for index 3 must not land on whatever is at index 3 by the time the
disk finishes — the list can be reordered, deleted from, or undone while a
read is in flight.

**Tempo estimation** fills in when the host says nothing, which is the
standalone app and any host that reports no transport. Spectral-flux onsets,
then autocorrelation over 60–200 BPM with a log-normal preference around 120,
because half and double tempo correlate almost as well as the true pulse and
that bias is how a listener resolves it. In a DAW this never runs at all — the
host's tempo is authoritative and a second opinion would just be noise.

The interesting failure was a held tone coming back as a confident 115 BPM. A
window sliding over a steady sine produces a small but *perfectly periodic*
flux ripple, because the hop and the sine frequency beat against each other,
and measured absolutely that artifact autocorrelates beautifully. Flux is now
measured as a proportion of each frame's own energy, which puts the ripple back
at the fraction of a percent it actually is, and detection additionally
requires real dynamic range between the envelope's peak and its mean.

## Layout and controls pass

The layout now fills the canvas. `layout_1491x1055.json` leaves four dead
areas - 90 px under LIVE INPUT, 30 under RECOVERY TOOLS, and an L-shaped hole
right of RECENT KEEPS and under EXPORT - so those panels were extended to a
consistent 8 px gutter and their contents re-spaced rather than left floating
at the top of a taller box. EXPORT now runs the full height from the capture
row to the bottom strip, which also gives its five buttons real size.

Three art-fit bugs went with it, all found by measuring against the reference
rather than by eye:

- **Knobs were drawn ~94 px where the approved art is ~60**, because the
  filmstrip was expanded by a sixth on top of a cell-filling size. They ran
  into the cell titles and the range labels.
- **The HUD ring was 8.8 % too small and 40 px too low.** The slice and the
  reference are the same artwork, so three landmarks pin it exactly: the
  leftmost and rightmost lit pixels give the scale, the topmost gives the
  offset.
- **Every action and destination label was printed twice**, because v1.4's
  slices have their text baked in and a caption was being drawn as well.

**Every button now does something, and there is a test that says so.**
`testEveryButtonIsWired` walks the real component tree, fails on any button
with no handler, and then invokes each handler. That last part matters:
`Button::triggerClick()` posts asynchronously, so in a harness with no message
loop an earlier version of this test passed without executing a single line.

Filling the gaps meant finishing four controls that had been decorative:
SAVE and the preset arrows now save and browse real presets on disk (a factory
set of five is written on first run, with values that actually suit each job);
SETTINGS opens a panel with low-power mode, reduce motion and the playlist
option; HELP opens a reference for every control; and a card's play icon plays
that card. Both overlays are in-editor panels, not modal dialogs - hosts
present those inconsistently and some never show them at all.

## Still open

Nothing blocking. Worth considering later: a `.wav` written by an older version
carries no key or bar count, since those are computed at capture time — a
background re-analysis on load would fill them in.
