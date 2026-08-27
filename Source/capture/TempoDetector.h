/*
    TempoDetector.h - estimating BPM when the host will not say.

    In a DAW the host's tempo is authoritative and this never runs. It exists
    for the standalone app and for hosts that report nothing, where without it
    a bar-based recovery has no idea how long a bar is and every capture comes
    back with no bar count.

    Spectral flux onsets, then autocorrelation:

      1. FFT frames at ~93 Hz, summing only the POSITIVE magnitude changes -
         energy appearing, not energy decaying, which is what an onset is -
         and dividing by the frame's own total magnitude, so flux is a
         PROPORTION of what is there rather than an absolute number.
      2. That normalisation is not cosmetic. A window sliding over a steady
         sine produces a small but perfectly periodic flux ripple, because the
         hop and the sine frequency beat against each other. Measured
         absolutely, that artifact autocorrelates beautifully and a held pad
         comes back as a confident 115 BPM. Measured as a proportion of the
         frame's energy, it is the fraction of a percent it actually is.
      3. The envelope is then mean-removed so a busy passage does not sit on a
         high baseline and drown its own peaks.
      4. Autocorrelate over lags covering 60-200 BPM.
      5. Weight the result toward the middle of that range, because half and
         double tempo correlate almost as well as the true one and a listener
         resolves that ambiguity by preferring a moderate pulse.

    Confidence needs both an absolute floor and a relative one: how strong the
    winning correlation is in its own right, and how far it stands above its
    neighbours. Music with no steady pulse scores low on the first and the
    caller shows nothing rather than a number that would be invented.
*/

#pragma once
#include <JuceHeader.h>

namespace keepthat
{

struct TempoResult
{
    bool detected = false;
    double bpm = 0.0;
    float confidence = 0.0f;         // 0..1
};

class TempoDetector
{
public:
    /** Below this the estimate is not reported: a capture with no steady pulse
        should show "--" rather than a plausible-looking wrong number. */
    static constexpr float minimumConfidence = 0.30f;

    static constexpr double minBpm = 60.0;
    static constexpr double maxBpm = 200.0;

    static TempoResult detect (const juce::AudioBuffer<float>& audio, double sampleRate);

    /** The onset envelope, exposed so the test harness can check the onset
        stage separately from the autocorrelation. Values are normalised flux
        in roughly 0..1 and are NOT mean-removed - `detect` does that itself,
        because it needs the raw dynamic range to tell onsets from ripple. */
    static void buildOnsetEnvelope (const juce::AudioBuffer<float>& audio,
                                    double sampleRate,
                                    std::vector<float>& envelope,
                                    double& frameRate);

    /** Prints the metrical candidates with their comb and prior scores, for
        tools/probe. This is how a half-time reading gets told apart from a
        wrong one. */
    static void debugDump (const juce::AudioBuffer<float>& audio, double sampleRate);
};

} // namespace keepthat
