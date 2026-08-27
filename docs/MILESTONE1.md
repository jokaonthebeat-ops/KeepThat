# Milestone 1 — what is real and what is standing in

The handoff asks for "the complete UI and interaction shell with realistic
placeholder data", with DSP and export held back until the interface is
approved. This is the honest accounting of that line, so nobody has to guess
which parts of a running build are finished.

## Real

**The rolling buffer.** `RollingBuffer` in `capture/CaptureModel.h` is a genuine
lock-free circular buffer. The audio thread writes with at most two `memcpy`s
and a release store; there is no allocation, no locking and no logging on that
path. It is sized to eight minutes at the host's sample rate, and it is sized
to the window the *user* asked for rather than the power-of-two allocation
behind it — reporting the padded capacity is what made an early build show
"MAX 11:39" for an eight-minute buffer.

`readLast()` exists and works, and re-checks the write index afterwards so a
reader that gets lapped reports the overrun instead of returning a torn buffer.
Nothing calls it yet; extraction is milestone 2.

**Metering and the live waveform.** `MeterState` and `WaveTrail` are written
from the audio thread with atomics and read from the message thread. Peak, RMS
and peak-hold are real, with fast-attack / slow-release ballistics. The LIVE
INPUT waveform is the actual signal scrolling past. With nothing routed in they
sit at the floor and the panel says NO SIGNAL, rather than animating something
invented.

**Host tempo.** Picked up from the playhead when the DAW supplies it, and shown
in the BPM readout. Falls back to the session value otherwise.

**The whole interface.** Every control is drawn live from vector primitives —
no stock JUCE widgets, and no reference PNG blitted in. Hover, pressed,
selected and disabled states are implemented throughout. Interactions that work
end to end: capture-length selection, all six recovery toggles, the fade dials,
the destination grid, the six macro knobs (drag, shift-drag for fine,
double-click to default, wheel), the trim handles on the capture timeline, the
playhead, TRIM committing a trimmed range, the recent-keeps browser with
selection, favourite, delete and scrolling, preset stepping, and the source
selector.

**State persistence.** `getStateInformation` / `setStateInformation` save and
restore the interface's settings through a host session.

## Standing in

**Capture extraction.** Pressing KEEP LAST is wired end to end through the
interface — a new keep appears, is selected, and becomes the capture preview —
but it does not yet pull audio out of the ring. That is deliberate. Faking it
would make a build that cannot yet recover audio look finished, which is the
one thing that must not happen with a recovery tool.

**Phrase detection.** The PHRASE DETECTED card shows a fixed verdict (4.0 bars,
92 % confidence). The amplitude-and-silence-gap heuristic is milestone 2.

**Waveform thumbnails.** The recent keeps, the capture preview and the phrase
card draw generated envelopes from `PlaceholderData::fillEnvelope`. They are
built to look like recorded material — bar-level dynamics, transients on the
beat, an exponential tail — because white noise reads as a broken display. They
are deterministic for a given seed, which is what makes `make uishot`
reproducible.

**Export.** The destination grid selects and persists a destination. Nothing is
written to disk and nothing is dragged out yet.

**Key detection.** The KEY readout is a fixed value. Nothing in the handoff asks
for key detection, so this may simply want removing at some point — worth a
decision rather than an implementation.

## Where the placeholders connect

Every stand-in sits behind an accessor the real engine will fill, so milestone 2
is a swap rather than a rewrite:

| stand-in | replaced by |
| --- | --- |
| `PlaceholderData::populate` | the recent-keeps list, loaded from session storage |
| `SessionState::previewLo/Hi` | a thumbnail built off the thread from `readLast()` |
| `SessionState::phrase` | the phrase detector's verdict |
| `ContentComponent::keepLastPressed` | `readLast()` → trim → fade → thumbnail → keep |
| `ContentComponent::loadKeep` | reading the stored clip's thumbnail |

## Known differences from the approved mockup

Measured with `tools/png.py diff`. None of these are unresolved bugs; they are
the places where a live render and a painted mockup cannot agree, recorded so
they are not rediscovered later as defects.

- **Typeface.** The mockup uses a condensed technical grotesque that does not
  ship on macOS. The build resolves Inter → SF Pro Display → Helvetica Neue and
  applies a horizontal squeeze on the largest faces (`hudTime`, `keepLast`,
  `logo`) to get near the reference's proportions. Glyph-level differences of a
  few pixels remain everywhere there is text, and that is most of the residual
  diff.

- **Live readouts.** PEAK, RMS and the buffer clock show what the plugin
  actually measured, so they will not match the mockup's `-1.2 dB` / `-14.3 dB`
  / `04:27` unless the same signal is fed for the same duration.

- **Two panel rectangles were grown.** `layout_1491x1055.json` gives the HUD
  panel as `{442,113,630,447}`, which clips the "-2:00" scale label above it and
  the second row of capture-length buttons below it — both of which are inside
  the approved art. The panel paints no surface of its own, so it was grown to
  `{442,84,630,501}` to contain them. Noted in `Theme.h`.

- **Ring scale labels.** The mockup shows `-4:00` / `-2:00` / `0:00 NOW` around
  a ring while its centre reads `MAX 8:00`. Those cannot both describe the same
  window, so the ring was built to span the history that actually exists and
  label it rounded down to the minute — which reproduces the mockup's own
  numbers for a 4:27 buffer. Buffer fullness is reported by the outer tick ring
  instead, which lights in proportion to available/max.
