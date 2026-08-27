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
        // Centred at 132, not 120. This plug-in is aimed at hip-hop, trap and
        // R&B, where 130-160 is ordinary; a 120 centre actively pulled a 150
        // BPM beat down onto its 100 BPM sub-pulse. The width is unchanged, so
        // half/double disambiguation still works either side of it.
        const double octaveBias = std::exp (-0.5 * std::pow (std::log2 (bpm / 132.0) / 0.85, 2.0));
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

    // ---- choose the metrical level ---------------------------------------
    // Autocorrelation is systematically happy at the WRONG multiple of the
    // beat. A period of 3/2 the true one still correlates strongly, because
    // two out of every three beats land on a pulse - and the octave bias
    // above, centred on 120, then quietly prefers it: on a 150 BPM trap beat
    // this reported 100.4, which is exactly 2/3 of the truth.
    //
    // So the winning lag is only a starting point. Simple ratios of it are
    // scored with a phase-aligned comb: line a pulse train up with the onset
    // envelope and ask how much onset energy actually falls on the pulses.
    // The true beat has an onset on every pulse; a 3/2 candidate has a third
    // of its pulses sitting in the gaps, and scores worse.
    // On-pulse energy measured AGAINST what falls between the pulses. Scoring
    // the on-pulse mean alone is not enough and gets it exactly backwards: a
    // half-tempo pulse train can line itself up with only the strongest beats
    // and post a higher mean than the true tempo does. Dividing by the
    // off-pulse mean is what punishes a candidate for missing beats - the
    // beats it skips are still in the envelope, and they land off-pulse.
    auto combScore = [&onsets] (int period) -> double
    {
        const int n = (int) onsets.size();
        if (period < 2 || period * 4 > n) return 0.0;

        double best = 0.0;
        for (int phase = 0; phase < period; ++phase)
        {
            double on = 0.0, off = 0.0;
            int onN = 0, offN = 0;
            for (int i = 0; i < n; ++i)
            {
                if ((i - phase) % period == 0) { on += onsets[(size_t) i]; ++onN; }
                else                           { off += onsets[(size_t) i]; ++offN; }
            }
            if (onN < 4 || offN == 0) continue;
            const double offMean = off / offN;
            best = juce::jmax (best, (on / onN) / juce::jmax (1.0e-9, offMean));
        }
        return best;
    };

    // The comb says which metrical levels are PLAUSIBLE; the same log-normal
    // prior used above then picks which of them to report. Both are needed.
    // Without the comb, autocorrelation drifts onto 3/2 of the beat. Without
    // the prior, the comb settles on the half-time pulse - which is a real
    // pulse and not wrong, but a producer working at 150 expects "4 BARS" to
    // mean four bars at 150, not eight seconds of half-time.
    auto levelPrior = [frameRate] (int lag)
    {
        const double bpm = 60.0 * frameRate / lag;
        return std::exp (-0.5 * std::pow (std::log2 (bpm / 132.0) / 0.85, 2.0));
    };

    static const double ratios[] = { 1.0/3.0, 0.5, 2.0/3.0, 0.75, 1.0, 4.0/3.0, 1.5, 2.0, 3.0 };
    int chosenLag = bestLag;
    double chosenScore = combScore (bestLag) * levelPrior (bestLag);

    for (double r : ratios)
    {
        const int lag = (int) std::lround (bestLag * r);
        if (lag < minLag || lag > maxLag || lag == bestLag)
            continue;

        // Clearly better, not merely equal - otherwise the reported level
        // wanders between two captures of the same material.
        const double score = combScore (lag) * levelPrior (lag);
        if (score > chosenScore * 1.04)
        {
            chosenScore = score;
            chosenLag = lag;
        }
    }

    // ---- half-time notation ----------------------------------------------
    // Trap, drill and trapsoul are written at twice their felt pulse: the kick
    // and snare sit at 75 while the hats run at 150, and the producer's DAW,
    // their filename and their collaborators all say 150. Reporting 75 is not
    // wrong about the audio, but it makes "4 BARS" recover eight seconds when
    // the user expects four.
    //
    // The tell is whether anything is actually happening at the double. If the
    // eighth-note grid carries real onsets - hats - the notated tempo is the
    // double. A genuine 75 BPM R&B or boom-bap track has nothing there, scores
    // about 1.0, and is left alone.
    {
        const double pulseBpm = 60.0 * frameRate / chosenLag;
        const int doubleLag = (int) std::lround (chosenLag * 0.5);
        if (pulseBpm < 90.0 && doubleLag >= minLag
            && 60.0 * frameRate / doubleLag <= maxBpm
            && combScore (doubleLag) > 1.40)
        {
            chosenLag = doubleLag;
        }
    }

    bestLag = chosenLag;

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

void TempoDetector::debugDump (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    std::vector<float> onsets;
    double frameRate = 0.0;
    buildOnsetEnvelope (audio, sampleRate, onsets, frameRate);
    if (onsets.size() < 64) { std::printf ("    (no envelope)\n"); return; }

    double rawMean = 0.0;
    for (float v : onsets) rawMean += v;
    rawMean /= onsets.size();
    for (auto& v : onsets) v = juce::jmax (0.0f, v - (float) rawMean);

    auto comb = [&onsets] (int period) -> double
    {
        const int n = (int) onsets.size();
        if (period < 2 || period * 4 > n) return 0.0;
        double best = 0.0;
        for (int phase = 0; phase < period; ++phase)
        {
            double on = 0.0, off = 0.0; int onN = 0, offN = 0;
            for (int i = 0; i < n; ++i)
                if ((i - phase) % period == 0) { on += onsets[(size_t) i]; ++onN; }
                else                           { off += onsets[(size_t) i]; ++offN; }
            if (onN < 4 || offN == 0) continue;
            best = juce::jmax (best, (on / onN) / juce::jmax (1.0e-9, off / offN));
        }
        return best;
    };

    std::printf ("    frameRate %.2f   candidates:\n", frameRate);
    for (double bpm : { 60.0, 70.0, 75.0, 90.0, 100.0, 110.0, 120.0, 130.0,
                        140.0, 150.0, 160.0, 175.0, 200.0 })
    {
        const int lag = (int) std::lround (60.0 * frameRate / bpm);
        const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 132.0) / 0.85, 2.0));
        std::printf ("      %5.0f bpm  lag %3d  comb %6.3f  prior %.3f  ->  %6.3f\n",
                     bpm, lag, comb (lag), prior, comb (lag) * prior);
    }
}

} // namespace keepthat
