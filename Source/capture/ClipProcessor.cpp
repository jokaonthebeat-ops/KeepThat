#include "ClipProcessor.h"

namespace keepthat::clip
{

float peakMagnitude (const juce::AudioBuffer<float>& b) noexcept
{
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        peak = juce::jmax (peak, b.getMagnitude (ch, 0, b.getNumSamples()));
    return peak;
}

float rmsLevel (const juce::AudioBuffer<float>& b) noexcept
{
    if (b.getNumChannels() == 0 || b.getNumSamples() == 0)
        return 0.0f;

    double sum = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float r = b.getRMSLevel (ch, 0, b.getNumSamples());
        sum += (double) r * r;
    }
    return (float) std::sqrt (sum / b.getNumChannels());
}

int nearestZeroCrossing (const juce::AudioBuffer<float>& b, int from,
                         int maxSearch, bool searchForward) noexcept
{
    if (b.getNumChannels() == 0 || b.getNumSamples() < 2)
        return from;

    const float* data = b.getReadPointer (0);
    const int n = b.getNumSamples();
    from = juce::jlimit (0, n - 1, from);

    const int step = searchForward ? 1 : -1;
    for (int i = 0; i < maxSearch; ++i)
    {
        const int at = from + i * step;
        if (at <= 0 || at >= n)
            break;

        // A crossing proper: the sign actually changes between neighbours.
        const float a = data[at - 1], c = data[at];
        if ((a <= 0.0f && c > 0.0f) || (a >= 0.0f && c < 0.0f))
            return at;
    }
    return from;
}

TrimResult trimSilence (const juce::AudioBuffer<float>& b, float threshold,
                        float amount, double sampleRate) noexcept
{
    TrimResult result;
    result.start = 0;
    result.end = b.getNumSamples();

    if (b.getNumChannels() == 0 || b.getNumSamples() == 0)
        return result;

    const int n = b.getNumSamples();

    // The run test HAS to be made on an envelope, not on raw samples. Audio
    // crosses zero constantly - a 440 Hz sine dips under any sensible
    // threshold twice per cycle - so requiring N consecutive samples above it
    // never succeeds on real material, and the trim silently returns the whole
    // clip. tools/DspTest.cpp catches exactly that.
    constexpr double hopSeconds = 0.002;
    const int hop = juce::jmax (1, (int) std::round (sampleRate * hopSeconds));
    const int hops = (n + hop - 1) / hop;

    auto hopPeak = [&b, hop, n] (int index)
    {
        const int from = index * hop;
        const int to = juce::jmin (n, from + hop);
        float m = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* d = b.getReadPointer (ch);
            for (int i = from; i < to; ++i)
                m = juce::jmax (m, std::abs (d[i]));
        }
        return m;
    };

    // How long a stretch has to stay above threshold before it counts as the
    // real start: one hop at amount 0 (trim only dead silence), 30 ms at
    // amount 1 (walk past clicks and room tone).
    const int runNeeded = juce::jmax (1, (int) std::round (0.030 * amount / hopSeconds));

    int firstLoud = -1, run = 0;
    for (int i = 0; i < hops; ++i)
    {
        if (hopPeak (i) > threshold)
        {
            if (run == 0) firstLoud = i;
            if (++run >= runNeeded) break;
        }
        else { run = 0; firstLoud = -1; }
    }

    int lastLoud = -1;
    run = 0;
    for (int i = hops - 1; i >= 0; --i)
    {
        if (hopPeak (i) > threshold)
        {
            if (run == 0) lastLoud = i;
            if (++run >= runNeeded) break;
        }
        else { run = 0; lastLoud = -1; }
    }

    // Nothing above threshold anywhere - hand back the whole clip rather than
    // an empty one. A silent-looking capture the user can still audition beats
    // a capture that vanished.
    if (firstLoud < 0 || lastLoud < 0 || lastLoud < firstLoud)
        return result;

    result.start = juce::jlimit (0, n, firstLoud * hop);
    result.end   = juce::jlimit (result.start, n, (lastLoud + 1) * hop);
    return result;
}

void snapToZeroCrossings (const juce::AudioBuffer<float>& b, TrimResult& trim,
                          double sampleRate) noexcept
{
    const int window = juce::jmax (1, (int) std::round (sampleRate * 0.005));  // 5 ms

    const int newStart = nearestZeroCrossing (b, trim.start, window, true);
    const int newEnd   = nearestZeroCrossing (b, juce::jmax (0, trim.end - 1), window, false);

    // Only take the snap if it leaves a sane range behind.
    if (newEnd > newStart + 1)
    {
        trim.movedForZeroCrossing = (newStart != trim.start) || (newEnd != trim.end - 1);
        trim.start = newStart;
        trim.end = newEnd + 1;
    }
}

void applyFades (juce::AudioBuffer<float>& b, float fadeMs, double sampleRate) noexcept
{
    const int n = b.getNumSamples();
    if (n < 4 || fadeMs <= 0.0f)
        return;

    // Neither fade may eat more than half the clip, or they would cross over
    // and the middle would be attenuated twice.
    int fade = (int) std::round (sampleRate * fadeMs / 1000.0);
    fade = juce::jlimit (1, n / 2, fade);

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        float* d = b.getWritePointer (ch);
        for (int i = 0; i < fade; ++i)
        {
            // Equal power, so a fade across a steady tone holds its loudness.
            const float t = (float) (i + 1) / (float) fade;
            const float g = std::sin (t * juce::MathConstants<float>::halfPi);
            d[i] *= g;
            d[n - 1 - i] *= g;
        }
    }
}

void normalise (juce::AudioBuffer<float>& b, float targetDb) noexcept
{
    const float peak = peakMagnitude (b);
    if (peak <= 1.0e-6f)
        return;                                   // silence has no peak to move

    const float target = juce::Decibels::decibelsToGain (targetDb);
    b.applyGain (target / peak);
}

void buildThumbnail (const juce::AudioBuffer<float>& b, int bins,
                     std::vector<float>& lo, std::vector<float>& hi)
{
    bins = juce::jmax (1, bins);
    lo.assign ((size_t) bins, 0.0f);
    hi.assign ((size_t) bins, 0.0f);

    const int n = b.getNumSamples();
    if (n == 0 || b.getNumChannels() == 0)
        return;

    for (int i = 0; i < bins; ++i)
    {
        const int from = (int) ((int64_t) i * n / bins);
        const int to   = juce::jmax (from + 1, (int) ((int64_t) (i + 1) * n / bins));

        juce::Range<float> range { 0.0f, 0.0f };
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            range = range.getUnionWith (juce::FloatVectorOperations::findMinAndMax (
                        b.getReadPointer (ch) + from, juce::jmin (to, n) - from));

        lo[(size_t) i] = range.getStart();
        hi[(size_t) i] = range.getEnd();
    }
}

void extractRange (const juce::AudioBuffer<float>& src, int start, int end,
                   juce::AudioBuffer<float>& dest)
{
    start = juce::jlimit (0, src.getNumSamples(), start);
    end   = juce::jlimit (start, src.getNumSamples(), end);
    const int len = end - start;

    dest.setSize (src.getNumChannels(), juce::jmax (0, len), false, true, true);
    for (int ch = 0; ch < src.getNumChannels(); ++ch)
        dest.copyFrom (ch, 0, src, ch, start, len);
}

} // namespace keepthat::clip
