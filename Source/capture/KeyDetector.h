/*
    KeyDetector.h - musical key from captured audio.

    Chroma plus Krumhansl-Schmuckler profile correlation, which is the standard
    approach and, importantly, one whose failures are understandable: it is
    correlating an observed pitch-class distribution against how strongly each
    scale degree is used in major and minor keys.

      1. Downmix to mono and take overlapping FFT frames.
      2. Fold each frame's spectrum into twelve pitch classes, weighting bins
         by magnitude and ignoring anything outside a musical range.
      3. Correlate the summed chroma against the major and minor profiles at
         all twelve rotations - 24 candidates.
      4. The best correlation wins, and the margin over the runner-up becomes
         the confidence.

    Confidence is reported, not hidden, and the caller is expected to show "--"
    below a threshold. Half the material a producer throws at this will be a
    drum loop with no key in it at all, and inventing one would be worse than
    saying nothing.

    Worker thread only - it allocates an FFT.
*/

#pragma once
#include <JuceHeader.h>

namespace keepthat
{

struct KeyResult
{
    bool detected = false;
    int  rootPitchClass = 0;        // 0 = C
    bool isMinor = false;
    float confidence = 0.0f;        // 0..1, from the margin over the runner-up

    /** "C# Minor", or "--" when nothing convincing was found. */
    juce::String describe() const;
};

class KeyDetector
{
public:
    /** Below this the result is reported as undetected: a drum loop correlates
        weakly with every key, and naming one would be invention. */
    static constexpr float minimumConfidence = 0.28f;

    static KeyResult detect (const juce::AudioBuffer<float>& audio, double sampleRate);

    /** The twelve-bin pitch-class profile, exposed so the test harness can
        check the chroma stage separately from the correlation stage. */
    static void buildChroma (const juce::AudioBuffer<float>& audio, double sampleRate,
                             std::array<float, 12>& chroma);
};

} // namespace keepthat
