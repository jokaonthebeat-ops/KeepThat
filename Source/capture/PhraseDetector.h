/*
    PhraseDetector.h - finds the most recent musical phrase in a captured span.

    The spec calls for "phrase detection using amplitude + silence-gap
    heuristics", which is what this is - deliberately not a machine-learning
    model, because the job is to find where somebody stopped playing, and a
    silence gap is exactly that signal.

    How it works:

      1. The span is reduced to a short-term energy envelope (10 ms hops).
      2. A noise floor is estimated from the quietest decile of that envelope,
         so a noisy room does not read as continuous playing.
      3. Runs above the floor are "active"; gaps below it that last longer than
         `minGapSeconds` are phrase boundaries.
      4. The phrase returned is the LAST active run - the user pressed KEEP
         LAST because of what they just played, not what they played a minute
         ago - extended left across any gaps shorter than the boundary.
      5. Its length is then reported in bars at the host tempo, snapped to a
         musically sensible count.

    Confidence is a real number, not decoration: it combines how far the phrase
    sits above the floor with how clean its boundaries are. A wall of unbroken
    sound scores low because there is no evidence of a phrase in it.

    Runs on a worker thread. No allocation beyond the envelope vector.
*/

#pragma once
#include <JuceHeader.h>

namespace keepthat
{

struct PhraseResult
{
    bool detected = false;
    int  startSample = 0;
    int  endSample = 0;                 // exclusive
    float confidence = 0.0f;            // 0..1
    double suggestedBars = 0.0;         // 0 when tempo is unknown

    int length() const noexcept { return juce::jmax (0, endSample - startSample); }
};

class PhraseDetector
{
public:
    struct Options
    {
        double minPhraseSeconds = 0.35;   // shorter than this is a stray hit
        double minGapSeconds    = 0.22;   // silence this long ends a phrase
        float  sensitivity      = 0.72f;  // the panel's SENSITIVITY knob
    };

    /** Analyses `audio` and returns the last phrase in it. `bpm` may be 0 when
        the host has no tempo, in which case suggestedBars stays 0. */
    static PhraseResult detect (const juce::AudioBuffer<float>& audio,
                                double sampleRate, double bpm,
                                const Options& options);

    /** The envelope the detector works from, exposed for the test harness so
        a failure can be inspected rather than guessed at. */
    static void buildEnvelope (const juce::AudioBuffer<float>& audio,
                               double sampleRate, double hopSeconds,
                               std::vector<float>& envelope);
};

} // namespace keepthat
