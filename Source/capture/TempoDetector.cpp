#include "TempoDetector.h"
#include <algorithm>

namespace keepthat
{

namespace
{
    constexpr int fftOrder = 10;              // 1024 samples ~ 21 ms at 48 kHz
    constexpr int fftSize  = 1 << fftOrder;
    constexpr int hopSize  = fftSize / 2;     // ~93 frames per second
}

void TempoDetector::buildOnsetEnvelope (const juce::AudioBuffer<float>& audio,
                                        double sampleRate,
                                        std::vector<float>& envelope,
                                        double& frameRate)
{
    envelope.clear();
    frameRate = sampleRate / hopSize;

    const int n = audio.getNumSamples();
    if (n < fftSize * 2 || sampleRate <= 0.0 || audio.getNumChannels() == 0)
        return;

    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> frame ((size_t) fftSize * 2, 0.0f);
    std::vector<float> previous ((size_t) (fftSize / 2), 0.0f);
    bool havePrevious = false;

    for (int start = 0; start + fftSize <= n; start += hopSize)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const float* d = audio.getReadPointer (ch) + start;
            for (int i = 0; i < fftSize; ++i)
                frame[(size_t) i] += d[i] / audio.getNumChannels();
        }

        window.multiplyWithWindowingTable (frame.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (frame.data());

        float flux = 0.0f, total = 0.0f;
        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const float magnitude = frame[(size_t) bin];
            total += magnitude;
            if (havePrevious)
            {
                // Half-wave rectified: only energy APPEARING counts. Counting
                // decay too would put an onset at the end of every note.
                const float rise = magnitude - previous[(size_t) bin];
                if (rise > 0.0f)
                    flux += rise;
            }
            previous[(size_t) bin] = magnitude;
        }

        if (havePrevious)
        {
            // Flux as a PROPORTION of the frame's energy. See the note in the
            // header: absolute flux makes a steady sine's windowing ripple
            // look like a drum pattern.
            envelope.push_back (total > 1.0e-9f ? flux / total : 0.0f);
        }
        havePrevious = true;
    }
}

TempoResult TempoDetector::detect (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    TempoResult result;

    std::vector<float> onsets;
    double frameRate = 0.0;
    buildOnsetEnvelope (audio, sampleRate, onsets, frameRate);

    if (onsets.size() < 64 || frameRate <= 0.0)
        return result;

    const int minLag = juce::jmax (2, (int) std::floor (frameRate * 60.0 / maxBpm));
    const int maxLag = (int) std::ceil (frameRate * 60.0 / minBpm);
    if (maxLag >= (int) onsets.size() / 2)
        return result;                     // too little audio to see a pulse

    // Is there any real onset activity, or just windowing ripple? Normalised
    // flux from an actual transient is a large fraction of the frame; ripple
    // from a steady tone is a fraction of a percent. The gap between the peak
    // and the average is what separates them.
    const float loudest = *std::max_element (onsets.begin(), onsets.end());
    double rawMean = 0.0;
    for (float v : onsets)
        rawMean += v;
    rawMean /= onsets.size();

    if (loudest - (float) rawMean < 0.03f)
        return result;                     // no transients, so no pulse

    int strongOnsets = 0;
    for (float v : onsets)
        if (v > (float) rawMean + (loudest - (float) rawMean) * 0.35f)
            ++strongOnsets;

    // Four beats is the least that can establish a pulse rather than a couple
    // of unrelated events.
    if (strongOnsets < 4)
        return result;

    // Mean-remove now, so a busy passage does not sit on a high baseline and
    // drown its own peaks in the correlation.
    for (auto& v : onsets)
        v = juce::jmax (0.0f, v - (float) rawMean);

    // Normalising by the zero-lag term makes correlations comparable across
    // material of different loudness.
    double zeroLag = 0.0;
    for (float v : onsets)
        zeroLag += (double) v * v;
    if (zeroLag <= 1.0e-9)
        return result;

    std::vector<double> correlation ((size_t) (maxLag + 1), 0.0);
    double best = 0.0, sum = 0.0;
    int bestLag = 0;
    int counted = 0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double acc = 0.0;
        const int limit = (int) onsets.size() - lag;
        for (int i = 0; i < limit; ++i)
            acc += (double) onsets[(size_t) i] * onsets[(size_t) (i + lag)];

        double score = acc / zeroLag;

        // Half and double tempo correlate nearly as well as the true pulse.
        // A log-normal preference around 120 BPM is how listeners resolve that
        // ambiguity, and it is what stops a 90 BPM loop reading as 45.
        const double bpm = 60.0 * frameRate / lag;
        const double octaveBias = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.85, 2.0));
        score *= octaveBias;

        correlation[(size_t) lag] = score;
        sum += score;
        ++counted;

        if (score > best)
        {
            best = score;
            bestLag = lag;
        }
    }

    if (bestLag == 0 || counted == 0)
        return result;

    const double mean = sum / counted;
    result.bpm = 60.0 * frameRate / bestLag;

    // An absolute floor as well as a relative one. `best` is the correlation
    // as a fraction of the envelope's own energy, so it is meaningful on its
    // own terms; the prominence over the mean then says how much this lag
    // stands out from its neighbours. Requiring both is what stops a ratio
    // computed over near-zero numbers from claiming certainty.
    if (best < 0.10)
        return result;

    const double prominence = mean > 1.0e-12
                                ? juce::jlimit (0.0, 1.0, (best / mean - 1.0) * 0.30)
                                : 0.0;
    result.confidence = (float) juce::jlimit (0.0, 1.0,
                            best * (0.5 + 0.5 * prominence));
    result.detected = result.confidence >= minimumConfidence;
    return result;
}

} // namespace keepthat
