#include "PhraseDetector.h"
#include <algorithm>

namespace keepthat
{

namespace
{
    constexpr double hopSeconds = 0.010;      // 10 ms envelope resolution

    /** The quietest decile of the envelope, which is a far more robust noise
        floor than the minimum: one digital-silence frame at the start of a
        capture would otherwise put the floor at zero and make everything
        after it read as active. */
    float estimateFloor (std::vector<float> sorted)
    {
        if (sorted.empty())
            return 0.0f;
        std::sort (sorted.begin(), sorted.end());
        // Not juce::jmax<size_t> - juce_dsp declares a jmax for SIMDRegister
        // that an explicit size_t argument resolves to instead.
        const size_t decile = sorted.size() >= 10 ? sorted.size() / 10 : 1;
        double sum = 0.0;
        for (size_t i = 0; i < decile; ++i)
            sum += sorted[i];
        return (float) (sum / decile);
    }

    /** Bar counts a producer would actually want back. Snapping to these
        rather than reporting 3.7 bars is the difference between a suggestion
        and a measurement. */
    double snapToMusicalBars (double bars)
    {
        static const double sensible[] = { 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0 };
        double best = sensible[0], bestErr = std::abs (bars - best);
        for (double candidate : sensible)
        {
            const double err = std::abs (bars - candidate);
            if (err < bestErr) { best = candidate; bestErr = err; }
        }
        return best;
    }
}

void PhraseDetector::buildEnvelope (const juce::AudioBuffer<float>& audio,
                                    double sampleRate, double hop,
                                    std::vector<float>& envelope)
{
    envelope.clear();
    const int n = audio.getNumSamples();
    const int channels = audio.getNumChannels();
    if (n == 0 || channels == 0 || sampleRate <= 0.0)
        return;

    const int hopSamples = juce::jmax (1, (int) std::round (sampleRate * hop));
    envelope.reserve ((size_t) (n / hopSamples + 1));

    for (int start = 0; start < n; start += hopSamples)
    {
        const int len = juce::jmin (hopSamples, n - start);
        double sum = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* d = audio.getReadPointer (ch) + start;
            for (int i = 0; i < len; ++i)
                sum += (double) d[i] * d[i];
        }
        envelope.push_back ((float) std::sqrt (sum / (len * channels)));
    }
}

PhraseResult PhraseDetector::detect (const juce::AudioBuffer<float>& audio,
                                     double sampleRate, double bpm,
                                     const Options& options)
{
    PhraseResult result;

    std::vector<float> env;
    buildEnvelope (audio, sampleRate, hopSeconds, env);
    if (env.size() < 4)
        return result;

    const int hopSamples = juce::jmax (1, (int) std::round (sampleRate * hopSeconds));
    const float floorLevel = estimateFloor (env);
    const float peak = *std::max_element (env.begin(), env.end());

    if (peak <= 1.0e-5f)
        return result;                    // the whole span is silence

    // Sensitivity slides the gate between "just above the floor" (catches
    // quiet playing, and quiet noise with it) and "well above it" (only
    // confident material). The floor itself is always the baseline.
    const float reach = juce::jmax (peak - floorLevel, 1.0e-6f);
    const float gate = floorLevel + reach * juce::jmap (options.sensitivity,
                                                        0.0f, 1.0f, 0.32f, 0.06f);

    const int minGapFrames = juce::jmax (1, (int) std::round (options.minGapSeconds / hopSeconds));
    const int minPhraseFrames = juce::jmax (1, (int) std::round (options.minPhraseSeconds / hopSeconds));

    // Walk back from the end: the last thing played is what KEEP LAST is for.
    int end = (int) env.size() - 1;
    while (end >= 0 && env[(size_t) end] <= gate)
        --end;

    if (end < 0)
        return result;                    // nothing above the gate at all

    int start = end;
    int gapRun = 0;
    int firstOfRun = end;
    while (start >= 0)
    {
        if (env[(size_t) start] > gate)
        {
            gapRun = 0;
            firstOfRun = start;
        }
        else if (++gapRun >= minGapFrames)
        {
            break;                        // a real boundary
        }
        --start;
    }
    start = firstOfRun;

    const int frames = end - start + 1;
    if (frames < minPhraseFrames)
        return result;

    result.detected = true;
    result.startSample = juce::jlimit (0, audio.getNumSamples(), start * hopSamples);
    result.endSample   = juce::jlimit (result.startSample, audio.getNumSamples(),
                                       (end + 1) * hopSamples);

    // --- confidence -------------------------------------------------------
    // Two independent pieces of evidence, multiplied so a phrase has to
    // satisfy both: how far it stands above the floor, and how clearly its
    // edges are bounded by silence. Unbroken sound scores low on the second,
    // which is correct - there is no phrase visible in it.
    double inside = 0.0;
    for (int i = start; i <= end; ++i)
        inside += env[(size_t) i];
    inside /= juce::jmax (1, frames);

    const float headroom = juce::jlimit (0.0f, 1.0f,
                                         (float) ((inside - floorLevel) / reach) * 1.6f);

    const bool boundedLeft  = start == 0 || env[(size_t) (start - 1)] <= gate;
    const bool boundedRight = end + 1 >= (int) env.size() || env[(size_t) (end + 1)] <= gate;
    const float coverage = (float) frames / (float) env.size();
    // A phrase filling the whole span is not a detected phrase, it is the span.
    const float boundedness = ((boundedLeft ? 0.5f : 0.2f) + (boundedRight ? 0.5f : 0.2f))
                            * juce::jlimit (0.35f, 1.0f, 1.25f - coverage);

    result.confidence = juce::jlimit (0.0f, 1.0f, headroom * boundedness * 1.35f);

    if (bpm > 0.0)
    {
        const double seconds = result.length() / sampleRate;
        const double barSeconds = 4.0 * 60.0 / bpm;      // 4/4 until the host says otherwise
        result.suggestedBars = snapToMusicalBars (seconds / barSeconds);
    }

    return result;
}

} // namespace keepthat
