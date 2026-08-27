#include "CaptureModel.h"
#include "ClipProcessor.h"

namespace keepthat
{

void SessionState::loadPreviewFrom (const CaptureClip& clip)
{
    previewEnd = 0.0;
    previewStart = -clip.seconds;
    trimLeft = 0.0f;
    trimRight = 1.0f;
    playhead = -1.0f;

    if (clip.hasAudio())
    {
        // The preview is drawn ~1000 px wide, so it gets its own higher
        // resolution envelope rather than stretching the card thumbnail.
        clip::buildThumbnail (*clip.audio, 900, previewLo, previewHi);
    }
    else
    {
        previewLo = clip.thumbLo;
        previewHi = clip.thumbHi;
    }
}

} // namespace keepthat

namespace keepthat::PlaceholderData
{

// -----------------------------------------------------------------------------
//  A believable audio envelope.
//
//  Noise alone reads as a broken display, so this layers what a recorded bar
//  actually looks like: a slow bar-level swell, a transient on every beat with
//  an exponential tail, and a little grain on top. Deterministic for a given
//  seed, which is what makes `make uishot` comparable frame to frame.
// -----------------------------------------------------------------------------
void fillEnvelope (std::vector<float>& lo, std::vector<float>& hi,
                   int bins, juce::uint32 seed, float density)
{
    lo.assign ((size_t) bins, 0.0f);
    hi.assign ((size_t) bins, 0.0f);

    juce::Random rng ((juce::int64) seed);
    const float beats = juce::jmax (4.0f, 16.0f * density);
    const float binsPerBeat = bins / beats;

    // Per-bar loudness so the waveform breathes instead of sitting flat.
    float barGain[8];
    for (auto& g : barGain)
        g = 0.62f + rng.nextFloat() * 0.38f;

    for (int i = 0; i < bins; ++i)
    {
        const float beat = i / binsPerBeat;
        const int   bar  = ((int) (beat / 4.0f)) % 8;
        const float intoBeat = std::fmod (beat, 1.0f);

        // Transient + decay, strongest on the downbeat.
        const bool downbeat = ((int) beat % 4) == 0;
        const float hit = std::exp (-intoBeat * (downbeat ? 5.0f : 8.5f))
                        * (downbeat ? 1.0f : 0.66f);

        // Sustained body under the hits.
        const float body = 0.30f + 0.16f * std::sin (beat * 0.7f)
                                 + 0.10f * std::sin (beat * 2.3f + 1.1f);

        float amp = barGain[bar] * (body + hit * 0.62f);
        amp *= 0.86f + rng.nextFloat() * 0.28f;          // grain
        amp = juce::jlimit (0.03f, 1.0f, amp);

        // Slightly asymmetric, the way real programme material is.
        hi[(size_t) i] =  amp;
        lo[(size_t) i] = -amp * (0.82f + rng.nextFloat() * 0.2f);
    }

    // Soften the very ends so a clip does not appear to start mid-transient.
    const int fade = juce::jmax (1, bins / 48);
    for (int i = 0; i < fade; ++i)
    {
        const float g = i / (float) fade;
        hi[(size_t) i] *= g;                 lo[(size_t) i] *= g;
        hi[(size_t) (bins - 1 - i)] *= g;    lo[(size_t) (bins - 1 - i)] *= g;
    }
}

// -----------------------------------------------------------------------------
//  The approved mockup's state, so milestone 1 renders what was signed off.
// -----------------------------------------------------------------------------
void populate (SessionState& s)
{
    // The approved mockup's readouts. Only demo mode sets these; a real
    // instance measures them or shows "--".
    s.bpm = 124.0;
    s.key = "C# Minor";
    s.peakDb = -1.2f;
    s.rmsDb = -14.3f;
    s.inputQuality = "GOOD";
    s.bufferAvailable = 267.0;

    fillEnvelope (s.previewLo, s.previewHi, 900, 0x4b505448u, 1.0f);

    s.phrase.detected = true;
    s.phrase.suggestedBars = 4.0;
    s.phrase.confidence = 0.92f;
    s.phrase.startSeconds = -267.0;
    s.phrase.endSeconds = 0.0;
    fillEnvelope (s.phrase.thumbLo, s.phrase.thumbHi, 150, 0x50485241u, 0.75f);

    struct Seed { const char* name; double seconds; bool favourite; };
    static const Seed seeds[] = {
        { "Hook Idea",       267.0, true  },
        { "Melody Accident", 135.0, false },
        { "Ad-Lib",           42.0, false },
        { "Piano Run",       188.0, false },
        { "Vocal Phrase",     96.0, false },
        { "Beat Switch",     170.0, false },
        { "Guitar Riff",      82.0, false },
        { "Late Night Idea", 221.0, false },
    };

    s.keeps.clear();
    juce::uint32 seed = 0x1001u;
    for (const auto& sd : seeds)
    {
        CaptureClip clip;
        clip.name = sd.name;
        clip.seconds = sd.seconds;
        clip.favourite = sd.favourite;
        fillEnvelope (clip.thumbLo, clip.thumbHi, 128, seed, 0.9f);
        seed += 0x3ff1u;
        s.keeps.push_back (std::move (clip));
    }
    s.selectedKeep = 0;
}

} // namespace keepthat::PlaceholderData
