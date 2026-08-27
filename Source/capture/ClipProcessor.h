/*
    ClipProcessor.h - everything done to a recovered clip after it leaves the
    ring buffer. All of it runs on a worker thread (THREADING_EXPORT_SPEC:
    "Worker pool: phrase detection, zero-cross search, fades, normalization,
    waveform thumbnails, WAV writing").

    These are free functions over an AudioBuffer rather than a class, because
    none of them need state and that makes them trivially testable - which
    matters, since a recovery tool that quietly mangles the one take somebody
    wanted back is worse than one that fails loudly. tools/DspTest.cpp checks
    each of them against hand-computed expectations.
*/

#pragma once
#include <JuceHeader.h>

namespace keepthat::clip
{

/** Where a trim landed, in samples relative to the buffer it was found in. */
struct TrimResult
{
    int start = 0;
    int end = 0;                       // exclusive
    bool movedForZeroCrossing = false;
    int length() const noexcept { return juce::jmax (0, end - start); }
};

/** Peak magnitude across every channel. */
float peakMagnitude (const juce::AudioBuffer<float>&) noexcept;

/** RMS across every channel. */
float rmsLevel (const juce::AudioBuffer<float>&) noexcept;

/** Nearest sample index at or before `from` where channel 0 crosses zero
    going in the same direction, searched no further than `maxSearch`.

    Returns `from` unchanged when nothing suitable is close enough: snapping a
    long way to find a crossing would shift the musical start, which is worse
    than the click it avoids. */
int nearestZeroCrossing (const juce::AudioBuffer<float>&, int from,
                         int maxSearch, bool searchForward) noexcept;

/** Trims silence from both ends.

    `threshold` is a linear magnitude; `amount` (0..1) scales how aggressive
    the search is, mapping onto how much of a run of quiet samples has to pass
    before the trim commits. Never returns an empty range - if the whole clip
    is below threshold the original bounds come back, because handing the user
    nothing is not a useful answer. */
TrimResult trimSilence (const juce::AudioBuffer<float>&, float threshold,
                        float amount, double sampleRate) noexcept;

/** Snaps both edges of `trim` to zero crossings, within ~5 ms. */
void snapToZeroCrossings (const juce::AudioBuffer<float>&, TrimResult&,
                          double sampleRate) noexcept;

/** Applies an equal-power fade in and out, in place, over `fadeMs` at each
    end. Clamped so the two fades can never overlap. */
void applyFades (juce::AudioBuffer<float>&, float fadeMs, double sampleRate) noexcept;

/** Scales the buffer so its peak sits at `targetDb`. No-op on silence. */
void normalise (juce::AudioBuffer<float>&, float targetDb) noexcept;

/** Min/max envelope for a waveform display, `bins` wide.

    `lo` and `hi` are resized to `bins`. Reading straight from the buffer keeps
    this honest: the display shows the audio that was actually captured. */
void buildThumbnail (const juce::AudioBuffer<float>&, int bins,
                     std::vector<float>& lo, std::vector<float>& hi);

/** Copies `[start, end)` out of `src` into `dest`. */
void extractRange (const juce::AudioBuffer<float>& src, int start, int end,
                   juce::AudioBuffer<float>& dest);

} // namespace keepthat::clip
