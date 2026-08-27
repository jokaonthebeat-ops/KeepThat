# KEEP THAT!
### Always-On Idea Capture

**The best take is usually the one nobody was recording.** KEEP THAT! is
already listening. When you play something worth keeping and the recorder
wasn't running, press one button and pull it back — the last bars, the last
seconds, or the last musical phrase, trimmed and ready to drop in.

---

## Nothing to arm

Put it on a track or your master bus and forget it. From the moment it loads
it is holding everything that passes through it — up to **eight minutes** of
rolling history. There is no record button. There is no "did I remember to
arm it". There is just the last eight minutes, always there.

Play. Mess about. Warm up. Take the call. When you land something, it's yours.

## Recover by musical unit, not by guesswork

- **1, 2, 4 or 8 bars** — following the host tempo, so a bar is a real bar.
- **15, 30 or 60 seconds** — when you weren't in time anyway.
- **PHRASE** — finds where the last musical idea actually *started* and takes
  that, rather than slicing across the middle of it.

## Then it cleans it up for you

Every capture comes back finished, not raw. Silence trimmed off both ends,
edges snapped to zero crossings so there is no click, fades applied, optional
normalise. That happens **as the capture is made** — not as a chore afterwards
with a pencil tool.

**AUTO TRIM**, **SILENCE DETECT**, **ZERO-CROSSING**, **FADE IN/OUT** and
**NORMALIZE** each switch off independently when you want the raw thing.

## It knows what you played

**Key detection** reads the buffer continuously — chroma analysis against
Krumhansl-Kessler profiles — so a capture arrives already labelled `C# Minor`.
**Tempo** comes from the host, and when the host has nothing to say (standalone,
or a host that reports no transport) it is estimated from onset
autocorrelation, so bar-based recovery still counts real bars.

## Keep going

Kept it? The buffer **restarts**. The clock reads time since your last idea,
not time since you opened the plug-in — so a glance tells you how much of what
you just played is still recoverable. Turn it off in Settings if you would
rather have one continuous window.

## A hundred keeps, kept

Captures stack up in a rack with their length, key and a waveform thumbnail.
Rename them. Star the good ones. Delete the rest — and **undo it**, because
deleting a keep never deletes the file. Undo and redo cover deletes, renames,
favourites and captures alike, and never touch your automation.

## Out of the plug-in, into the track

**Drag straight into your DAW.** Or write a 24-bit WAV. Or send it to a
folder, a sampler, the desktop, or a playlist that maintains its own `.m3u`.
Right-click any destination to point it wherever you keep things.

## Under the hood, for those who ask

- Lock-free rolling buffer — the audio thread never allocates, never takes a
  blocking lock, never touches the disk. **Zero added latency.**
- Captures run on a worker thread and can never be turned away by a background
  analysis; a capture displaces the scan instead.
- Clearing the buffer is O(1) at the point of use, then wipes progressively —
  no 176 MB memset dropped on your audio thread.
- 106 deterministic DSP checks that validate their own measurements before
  trusting them.
- 60 fps interface with a low-power mode that halves its CPU.
- **No network. No telemetry. No account.** It never sends anything anywhere.

---

**Formats**: VST3 + AU + Standalone, macOS 11+, universal (Intel & Apple
Silicon). Windows VST3 + Standalone.

**Price**: Free.

*From Diamond Loopz — because the take you didn't record is the one you'll
remember.*
