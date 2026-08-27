#include "KeyDetector.h"

namespace keepthat
{

namespace
{
    constexpr int fftOrder = 12;              // 4096 samples ~ 85 ms at 48 kHz
    constexpr int fftSize  = 1 << fftOrder;

    // Krumhansl-Kessler key profiles: how strongly each scale degree is used.
    constexpr float majorProfile[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
                                         2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr float minorProfile[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
                                         2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F",
                                  "F#", "G", "G#", "A", "A#", "B" };

    /** How close the relative minor has to be before it is preferred over a
        major winner. See the note in `detect`. */
    constexpr float relativeKeyMargin = 0.16f;

    /** Pearson correlation between a chroma vector and a profile, with the
        profile rotated to `root`. */
    float correlate (const std::array<float, 12>& chroma, const float* profile, int root)
    {
        double chromaMean = 0.0, profileMean = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            chromaMean += chroma[(size_t) i];
            profileMean += profile[i];
        }
        chromaMean /= 12.0;
        profileMean /= 12.0;

        double num = 0.0, chromaVar = 0.0, profileVar = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double c = chroma[(size_t) ((i + root) % 12)] - chromaMean;
            const double p = profile[i] - profileMean;
            num += c * p;
            chromaVar += c * c;
            profileVar += p * p;
        }

        const double denom = std::sqrt (chromaVar * profileVar);
        return denom > 1.0e-12 ? (float) (num / denom) : 0.0f;
    }

    /** The bass chroma: which pitch class the LOW END sits on, frame by frame.

        This is what separates a key from its relative - C# major and A# minor
        contain exactly the same seven pitch classes, so no amount of profile
        correlation over the full spectrum can reliably tell them apart. What
        does tell them apart is the bass: in this music the 808 sits on the
        tonic, and the tonic of A# minor is A#.

        The FFT's bin spacing (11.7 Hz at 4096/48k) is far coarser than a
        semitone down here, so the peak is interpolated parabolically - that
        recovers the frequency to well inside a semitone, which is all this
        needs.
    */
    void buildBassChroma (const juce::AudioBuffer<float>& audio, double sampleRate,
                          std::array<float, 12>& bass)
    {
        bass.fill (0.0f);
        const int n = audio.getNumSamples();
        if (n < fftSize || sampleRate <= 0.0 || audio.getNumChannels() == 0)
            return;

        juce::dsp::FFT fft (fftOrder);
        juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                    juce::dsp::WindowingFunction<float>::hann);
        std::vector<float> frame ((size_t) fftSize * 2, 0.0f);

        const int hop = fftSize / 2;
        const int frames = juce::jmax (1, (n - fftSize) / hop + 1);
        const int lo = juce::jmax (2, (int) std::floor (38.0 * fftSize / sampleRate));
        const int hi = juce::jmin (fftSize / 2 - 2, (int) std::ceil (185.0 * fftSize / sampleRate));

        for (int f = 0; f < frames; ++f)
        {
            const int start = f * hop;
            if (start + fftSize > n) break;

            std::fill (frame.begin(), frame.end(), 0.0f);
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                const float* d = audio.getReadPointer (ch) + start;
                for (int i = 0; i < fftSize; ++i)
                    frame[(size_t) i] += d[i] / audio.getNumChannels();
            }
            window.multiplyWithWindowingTable (frame.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (frame.data());

            int peak = -1;
            float peakMag = 0.0f;
            for (int bin = lo; bin <= hi; ++bin)
                if (frame[(size_t) bin] > peakMag) { peakMag = frame[(size_t) bin]; peak = bin; }

            if (peak < 1 || peakMag <= 0.0f)
                continue;

            const float a = frame[(size_t) (peak - 1)], b = peakMag, c = frame[(size_t) (peak + 1)];
            const float denom = a - 2.0f * b + c;
            const float delta = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
            const double hz = (peak + juce::jlimit (-0.5f, 0.5f, delta)) * sampleRate / fftSize;
            if (hz < 30.0) continue;

            const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
            bass[(size_t) pc] += peakMag;
        }

        float total = 0.0f;
        for (float v : bass) total += v;
        if (total > 0.0f)
            for (auto& v : bass) v /= total;
    }
}

juce::String KeyResult::describe() const
{
    if (! detected)
        return "--";
    return juce::String (noteNames[((rootPitchClass % 12) + 12) % 12])
         + (isMinor ? " Minor" : " Major");
}

void KeyDetector::buildChroma (const juce::AudioBuffer<float>& audio, double sampleRate,
                               std::array<float, 12>& chroma)
{
    chroma.fill (0.0f);

    const int n = audio.getNumSamples();
    if (n < fftSize || sampleRate <= 0.0 || audio.getNumChannels() == 0)
        return;

    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> frame ((size_t) fftSize * 2, 0.0f);

    // Hop by half a frame so nothing lands in a window's dead zone.
    const int hop = fftSize / 2;
    const int frames = juce::jmax (1, (n - fftSize) / hop + 1);

    // Only the range where pitch is actually legible. Below ~65 Hz the FFT bin
    // spacing is too coarse to resolve a semitone; above ~2 kHz almost all the
    // energy is harmonics, which smear the profile.
    const double minHz = 65.0, maxHz = 2000.0;

    for (int f = 0; f < frames; ++f)
    {
        const int start = f * hop;
        if (start + fftSize > n)
            break;

        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const float* d = audio.getReadPointer (ch) + start;
            for (int i = 0; i < fftSize; ++i)
                frame[(size_t) i] += d[i] / audio.getNumChannels();
        }

        window.multiplyWithWindowingTable (frame.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (frame.data());

        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const double hz = bin * sampleRate / fftSize;
            if (hz < minHz || hz > maxHz)
                continue;

            const float magnitude = frame[(size_t) bin];
            if (magnitude <= 0.0f)
                continue;

            // MIDI note number, then fold to a pitch class.
            const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const int pitchClass = ((int) std::lround (midi) % 12 + 12) % 12;
            chroma[(size_t) pitchClass] += magnitude;
        }
    }

    // Normalise so a loud capture and a quiet one correlate identically.
    float total = 0.0f;
    for (float v : chroma)
        total += v;
    if (total > 0.0f)
        for (auto& v : chroma)
            v /= total;
}

KeyResult KeyDetector::detect (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    KeyResult result;

    std::array<float, 12> chroma {};
    buildChroma (audio, sampleRate, chroma);

    float total = 0.0f;
    for (float v : chroma)
        total += v;
    if (total <= 0.0f)
        return result;                         // silence, or nothing in range

    float best = -2.0f, runnerUp = -2.0f;
    int bestRoot = 0;
    bool bestMinor = false;

    for (int root = 0; root < 12; ++root)
    {
        for (int minor = 0; minor < 2; ++minor)
        {
            const float score = correlate (chroma, minor ? minorProfile : majorProfile, root);
            if (score > best)
            {
                runnerUp = best;
                best = score;
                bestRoot = root;
                bestMinor = minor != 0;
            }
            else if (score > runnerUp)
            {
                runnerUp = score;
            }
        }
    }

    // Confidence leads on how well the winning profile fits, with the margin
    // over the runner-up as a modifier rather than a multiplier.
    //
    // Leading on the margin was wrong: a key's nearest rival is its relative
    // major or minor, which shares all seven pitch classes, so the margin is
    // small even for unambiguously tonal material - C major scored a confident
    // fit and was then rejected for not beating A minor by enough. Fit is the
    // evidence that there IS a key; the margin only says how sure we are which
    // one. Noise still scores near zero because a flat chroma correlates with
    // no profile at all.
    const float strength = juce::jlimit (0.0f, 1.0f, best);
    const float margin = juce::jlimit (0.0f, 1.0f, (best - runnerUp) * 8.0f);
    result.confidence = juce::jlimit (0.0f, 1.0f, strength * (0.55f + 0.45f * margin));

    // ---- the relative-key rule -------------------------------------------
    // A major key and its relative minor contain exactly the same seven pitch
    // classes, so no profile correlation over a chroma can separate them on
    // evidence - only on which degree the profile weights hardest. Broadband
    // chroma quietly favours the major reading, because a bass note's third
    // harmonic is its fifth: on this project's reference track the 808 sits on
    // F# and inflates C#, which is exactly the tonic of the wrong answer.
    //
    // So when the winner is a MAJOR key and its own relative minor is the
    // runner-up by a small margin, take the minor. This plug-in is aimed at
    // hip-hop, trap and R&B, where minor is overwhelmingly the norm, and a
    // producer who wrote a track in A# minor is not helped by being told it is
    // C# major. The margin gate keeps unambiguous major material major - the
    // suite's C major and D major progressions clear it comfortably.
    if (! bestMinor)
    {
        const int relativeMinor = (bestRoot + 9) % 12;
        const float relScore = correlate (chroma, minorProfile, relativeMinor);
        if (best - relScore < relativeKeyMargin)
        {
            bestRoot = relativeMinor;
            bestMinor = true;
        }
    }

    result.rootPitchClass = bestRoot;
    result.isMinor = bestMinor;
    result.detected = result.confidence >= minimumConfidence;
    return result;
}

void KeyDetector::debugDump (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    std::array<float, 12> chroma {}, bass {};
    buildChroma (audio, sampleRate, chroma);
    buildBassChroma (audio, sampleRate, bass);

    std::printf ("    chroma:");
    for (int i = 0; i < 12; ++i) std::printf (" %s%.3f", noteNames[i], chroma[(size_t) i]);
    std::printf ("\n    bass:  ");
    for (int i = 0; i < 12; ++i) std::printf (" %s%.3f", noteNames[i], bass[(size_t) i]);

    int bassRoot = -1; float bassShare = 0.0f;
    for (int i = 0; i < 12; ++i)
        if (bass[(size_t) i] > bassShare) { bassShare = bass[(size_t) i]; bassRoot = i; }
    std::printf ("\n    bass root: %s (%.3f share)\n",
                 bassRoot >= 0 ? noteNames[bassRoot] : "-", bassShare);

    struct Cand { float score; int root; bool minor; };
    std::vector<Cand> cands;
    for (int root = 0; root < 12; ++root)
        for (int minor = 0; minor < 2; ++minor)
            cands.push_back ({ correlate (chroma, minor ? minorProfile : majorProfile, root),
                               root, minor != 0 });
    std::sort (cands.begin(), cands.end(),
               [] (const Cand& a, const Cand& b) { return a.score > b.score; });
    std::printf ("    top raw correlations:\n");
    for (int i = 0; i < 5; ++i)
        std::printf ("      %-3s %-5s %.4f\n", noteNames[cands[(size_t) i].root],
                     cands[(size_t) i].minor ? "min" : "maj", cands[(size_t) i].score);
}

} // namespace keepthat
