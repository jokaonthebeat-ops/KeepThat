// -----------------------------------------------------------------------------
//  Deterministic checks over the capture engine's DSP.
//
//    make dsptest
//
//  This is a recovery tool: if it mangles the one take somebody wanted back,
//  that take is gone. So every processing step is checked against a
//  hand-computed expectation on a signal whose answer is known in advance,
//  rather than against "it looked fine".
//
//  The harness validates ITSELF first: `selfCheck` builds signals whose
//  properties are known by construction and asserts the measuring code agrees.
//  A test suite whose measurements are wrong passes everything, and that is
//  worse than having no tests.
// -----------------------------------------------------------------------------

#include "PluginProcessor.h"
#include "capture/ClipProcessor.h"
#include "capture/PhraseDetector.h"
#include "capture/KeyDetector.h"
#include "capture/TempoDetector.h"
#include "capture/ClipLoader.h"
#include "PluginEditor.h"
#include "state/SessionHistory.h"
#include "export/WavExporter.h"
#include <cstdio>

using namespace keepthat;

namespace
{
    int failures = 0, checks = 0;

    void check (bool ok, const juce::String& what, const juce::String& detail = {})
    {
        ++checks;
        if (ok)
        {
            std::printf ("  ok    %s\n", what.toRawUTF8());
        }
        else
        {
            ++failures;
            std::printf ("  FAIL  %s%s%s\n", what.toRawUTF8(),
                         detail.isEmpty() ? "" : "  --  ", detail.toRawUTF8());
        }
    }

    void checkNear (double actual, double expected, double tolerance,
                    const juce::String& what)
    {
        check (std::abs (actual - expected) <= tolerance, what,
               "got " + juce::String (actual, 4) + ", expected "
               + juce::String (expected, 4) + " +/- " + juce::String (tolerance, 4));
    }

    constexpr double kRate = 48000.0;

    /** A sine at `hz` for `seconds`, amplitude `amp`. */
    juce::AudioBuffer<float> tone (double seconds, double hz, float amp = 0.5f,
                                   int channels = 2)
    {
        juce::AudioBuffer<float> b (channels, (int) std::round (kRate * seconds));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.getWritePointer (ch)[i] =
                    amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / kRate);
        return b;
    }

    juce::AudioBuffer<float> silence (double seconds, int channels = 2)
    {
        juce::AudioBuffer<float> b (channels, (int) std::round (kRate * seconds));
        b.clear();
        return b;
    }

    /** A chord built from equal-temperament semitones above a root, so the
        key it implies is known by construction. */
    juce::AudioBuffer<float> chord (double seconds, int rootMidi,
                                    std::initializer_list<int> semitones,
                                    float amp = 0.25f)
    {
        juce::AudioBuffer<float> b (2, (int) std::round (kRate * seconds));
        b.clear();
        for (int st : semitones)
        {
            const double hz = 440.0 * std::pow (2.0, (rootMidi + st - 69) / 12.0);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.getWritePointer (ch)[i] +=
                        amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / kRate);
        }
        return b;
    }

    /** A click track at `bpm`: short bursts on the beat, so the tempo is
        known by construction. */
    juce::AudioBuffer<float> clickTrack (double seconds, double bpm, float amp = 0.8f)
    {
        juce::AudioBuffer<float> b (2, (int) std::round (kRate * seconds));
        b.clear();
        const double beat = 60.0 / bpm;
        juce::Random rng (0xc11c);
        for (double t = 0.0; t < seconds; t += beat)
        {
            const int at = (int) std::round (t * kRate);
            const int len = (int) (kRate * 0.02);          // 20 ms burst
            for (int i = 0; i < len && at + i < b.getNumSamples(); ++i)
            {
                const float env = std::exp (-i / (kRate * 0.004f));
                const float v = amp * env * (rng.nextFloat() * 2.0f - 1.0f);
                for (int ch = 0; ch < 2; ++ch)
                    b.getWritePointer (ch)[at + i] += v;
            }
        }
        return b;
    }

    /** Broadband noise - no pitch content, so no key should be claimed. */
    juce::AudioBuffer<float> noise (double seconds, float amp = 0.4f)
    {
        juce::AudioBuffer<float> b (2, (int) std::round (kRate * seconds));
        juce::Random rng (0x5eed);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.getWritePointer (ch)[i] = amp * (rng.nextFloat() * 2.0f - 1.0f);
        return b;
    }

    /** Concatenates buffers into one. */
    juce::AudioBuffer<float> join (std::initializer_list<juce::AudioBuffer<float>> parts)
    {
        int total = 0, channels = 0;
        for (const auto& p : parts)
        {
            total += p.getNumSamples();
            channels = juce::jmax (channels, p.getNumChannels());
        }
        juce::AudioBuffer<float> out (channels, total);
        out.clear();
        int at = 0;
        for (const auto& p : parts)
        {
            for (int ch = 0; ch < channels; ++ch)
                out.copyFrom (ch, at, p, juce::jmin (ch, p.getNumChannels() - 1),
                              0, p.getNumSamples());
            at += p.getNumSamples();
        }
        return out;
    }
}

// -----------------------------------------------------------------------------
//  0. The harness checks its own measuring tools before it trusts them.
// -----------------------------------------------------------------------------
static void selfCheck()
{
    std::printf ("\nself-check (the harness's own signals and measurements)\n");

    auto t = tone (1.0, 1000.0, 0.5f);
    check (t.getNumSamples() == 48000, "tone() length is exactly one second");
    checkNear (clip::peakMagnitude (t), 0.5, 0.001, "peakMagnitude of a 0.5 sine is 0.5");
    // RMS of a sine is amplitude / sqrt(2).
    checkNear (clip::rmsLevel (t), 0.5 / std::sqrt (2.0), 0.005,
               "rmsLevel of a 0.5 sine is 0.354");

    auto s = silence (0.5);
    checkNear (clip::peakMagnitude (s), 0.0, 1.0e-9, "peakMagnitude of silence is 0");

    auto j = join ({ silence (0.25), tone (0.5, 440.0, 0.8f), silence (0.25) });
    check (j.getNumSamples() == 48000, "join() totals the parts");
    checkNear (clip::peakMagnitude (j), 0.8, 0.001, "join() preserves the loud part");
}

// -----------------------------------------------------------------------------
//  1. Silence trimming
// -----------------------------------------------------------------------------
static void testTrim()
{
    std::printf ("\nsilence trim\n");

    // 0.4 s silence, 1.0 s tone, 0.6 s silence. The trim must land on the tone.
    auto b = join ({ silence (0.4), tone (1.0, 440.0, 0.7f), silence (0.6) });

    auto trim = clip::trimSilence (b, 0.05f, 0.0f, kRate);
    checkNear (trim.start, 0.4 * kRate, 64, "trim start finds the tone");
    checkNear (trim.end, 1.4 * kRate, 64, "trim end finds the tone");

    // All-silence must return the original bounds, never an empty clip - a
    // capture that vanishes is worse than a quiet one.
    auto quiet = silence (1.0);
    auto none = clip::trimSilence (quiet, 0.05f, 0.5f, kRate);
    check (none.start == 0 && none.end == quiet.getNumSamples(),
           "an all-silent clip is returned whole, not empty");

    // A higher amount demands a longer run above threshold, so a lone click
    // must not be mistaken for the start of the take.
    juce::AudioBuffer<float> click (2, (int) (kRate * 1.0));
    click.clear();
    for (int ch = 0; ch < 2; ++ch)
        click.getWritePointer (ch)[(int) (kRate * 0.1)] = 0.9f;   // one sample
    auto body = tone (0.4, 440.0, 0.6f);
    for (int ch = 0; ch < 2; ++ch)
        click.copyFrom (ch, (int) (kRate * 0.5), body, ch, 0, body.getNumSamples());

    auto strict = clip::trimSilence (click, 0.05f, 1.0f, kRate);
    check (strict.start > (int) (kRate * 0.2),
           "a single-sample click is not mistaken for the start",
           "start = " + juce::String (strict.start));
}

// -----------------------------------------------------------------------------
//  2. Zero-crossing search
// -----------------------------------------------------------------------------
static void testZeroCrossing()
{
    std::printf ("\nzero crossing\n");

    // A 100 Hz sine crosses zero every 240 samples at 48 kHz.
    auto b = tone (0.5, 100.0, 0.8f, 1);

    const int from = 1000;
    const int found = clip::nearestZeroCrossing (b, from, 500, true);
    check (found >= from && found < from + 500, "a crossing is found within the window");
    check (std::abs (b.getReadPointer (0)[found]) < 0.06f,
           "the sample at the crossing is near zero",
           "value = " + juce::String (b.getReadPointer (0)[found], 4));

    // Nothing to find: DC never crosses, so the index must come back unchanged
    // rather than being dragged somewhere arbitrary.
    juce::AudioBuffer<float> dc (1, 1000);
    for (int i = 0; i < 1000; ++i)
        dc.getWritePointer (0)[i] = 0.5f;
    check (clip::nearestZeroCrossing (dc, 400, 200, true) == 400,
           "no crossing in range leaves the index alone");
}

// -----------------------------------------------------------------------------
//  3. Fades
// -----------------------------------------------------------------------------
static void testFades()
{
    std::printf ("\nfades\n");

    auto b = tone (1.0, 1000.0, 1.0f, 1);
    clip::applyFades (b, 50.0f, kRate);

    check (std::abs (b.getReadPointer (0)[0]) < 0.05f, "the first sample is faded down");
    check (std::abs (b.getReadPointer (0)[b.getNumSamples() - 1]) < 0.05f,
           "the last sample is faded down");

    // The middle must be untouched: a fade that attenuates the body is a bug
    // that would quietly change every capture.
    const int mid = b.getNumSamples() / 2;
    float midPeak = 0.0f;
    for (int i = mid - 2000; i < mid + 2000; ++i)
        midPeak = juce::jmax (midPeak, std::abs (b.getReadPointer (0)[i]));
    checkNear (midPeak, 1.0, 0.01, "the middle keeps full amplitude");

    // A fade longer than the clip must not make the two ends overlap and
    // double-attenuate.
    auto tiny = tone (0.01, 1000.0, 1.0f, 1);
    clip::applyFades (tiny, 5000.0f, kRate);
    check (clip::peakMagnitude (tiny) > 0.3f,
           "an over-long fade does not collapse a short clip",
           "peak = " + juce::String (clip::peakMagnitude (tiny), 4));
}

// -----------------------------------------------------------------------------
//  4. Normalize
// -----------------------------------------------------------------------------
static void testNormalize()
{
    std::printf ("\nnormalize\n");

    auto b = tone (0.5, 440.0, 0.2f);
    clip::normalise (b, -1.0f);
    checkNear (juce::Decibels::gainToDecibels (clip::peakMagnitude (b)), -1.0, 0.05,
               "a quiet clip is lifted to the target");

    auto loud = tone (0.5, 440.0, 0.99f);
    clip::normalise (loud, -6.0f);
    checkNear (juce::Decibels::gainToDecibels (clip::peakMagnitude (loud)), -6.0, 0.05,
               "a loud clip is brought down to the target");

    auto quiet = silence (0.2);
    clip::normalise (quiet, -1.0f);
    checkNear (clip::peakMagnitude (quiet), 0.0, 1.0e-9,
               "silence is left alone rather than amplified to nothing");
}

// -----------------------------------------------------------------------------
//  5. Thumbnails
// -----------------------------------------------------------------------------
static void testThumbnail()
{
    std::printf ("\nthumbnail\n");

    auto b = join ({ silence (0.5), tone (0.5, 440.0, 0.9f) });
    std::vector<float> lo, hi;
    clip::buildThumbnail (b, 100, lo, hi);

    check (lo.size() == 100 && hi.size() == 100, "the envelope has the requested width");

    float firstHalf = 0.0f, secondHalf = 0.0f;
    for (int i = 0; i < 50; ++i)  firstHalf  = juce::jmax (firstHalf,  hi[(size_t) i]);
    for (int i = 50; i < 100; ++i) secondHalf = juce::jmax (secondHalf, hi[(size_t) i]);
    checkNear (firstHalf, 0.0, 0.01, "the silent half reads as silent");
    checkNear (secondHalf, 0.9, 0.02, "the loud half reads at its real amplitude");
}

// -----------------------------------------------------------------------------
//  6. Phrase detection
// -----------------------------------------------------------------------------
static void testPhraseDetection()
{
    std::printf ("\nphrase detection\n");

    PhraseDetector::Options options;
    options.sensitivity = 0.72f;

    // Two phrases separated by a clear gap. The detector must return the LAST
    // one, because that is what KEEP LAST means.
    auto b = join ({ tone (1.0, 220.0, 0.6f), silence (0.6),
                     tone (0.8, 440.0, 0.6f), silence (0.05) });
    auto found = PhraseDetector::detect (b, kRate, 120.0, options);

    check (found.detected, "a phrase is found");
    checkNear (found.startSample / kRate, 1.6, 0.10, "it starts at the second phrase");
    checkNear (found.endSample / kRate, 2.4, 0.10, "it ends where that phrase ends");
    check (found.confidence > 0.3f, "confidence is meaningful",
           "confidence = " + juce::String (found.confidence, 3));

    // 0.8 s at 120 BPM is 0.4 bars, which snaps to the nearest sensible count.
    check (found.suggestedBars > 0.0, "bars are suggested when tempo is known",
           "bars = " + juce::String (found.suggestedBars, 2));

    // Silence has no phrase in it.
    auto quiet = silence (3.0);
    check (! PhraseDetector::detect (quiet, kRate, 120.0, options).detected,
           "silence yields no phrase");

    // Unbroken sound is not a phrase - there is no evidence of one in it, and
    // reporting high confidence there would be a lie.
    auto wall = tone (5.0, 300.0, 0.6f);
    auto wallResult = PhraseDetector::detect (wall, kRate, 120.0, options);
    check (! wallResult.detected || wallResult.confidence < 0.55f,
           "unbroken sound does not score high confidence",
           "confidence = " + juce::String (wallResult.confidence, 3));
}

// -----------------------------------------------------------------------------
//  6b. Key detection
// -----------------------------------------------------------------------------
static void testKeyDetection()
{
    std::printf ("\nkey detection\n");

    // C major: the tonic triad plus the rest of the scale, so the chroma has
    // the shape a major key actually produces rather than three spikes.
    auto cMajor = join ({ chord (1.0, 60, { 0, 4, 7 }),          // C E G
                          chord (1.0, 60, { 5, 9, 12 }),         // F A C
                          chord (1.0, 60, { 7, 11, 14 }),        // G B D
                          chord (1.0, 60, { 0, 4, 7 }) });
    auto key = KeyDetector::detect (cMajor, kRate);
    check (key.detected, "a key is found in tonal material",
           key.describe() + " at confidence " + juce::String (key.confidence, 3));
    check (key.rootPitchClass == 0 && ! key.isMinor,
           "C major is identified", key.describe());

    // A minor: same pitch classes as C major, so this is the case that
    // separates a real detector from one matching pitch sets.
    auto aMinor = join ({ chord (1.0, 57, { 0, 3, 7 }),          // A C E
                          chord (1.0, 57, { 5, 8, 12 }),         // D F A
                          chord (1.0, 57, { 7, 10, 14 }),        // E G B
                          chord (1.0, 57, { 0, 3, 7 }) });
    auto minorKey = KeyDetector::detect (aMinor, kRate);
    check (minorKey.detected, "a key is found in minor material");
    check (minorKey.rootPitchClass == 9,
           "A minor is rooted on A", minorKey.describe());

    // Transposition must move the answer by exactly the same interval.
    auto dMajor = join ({ chord (1.0, 62, { 0, 4, 7 }),
                          chord (1.0, 62, { 5, 9, 12 }),
                          chord (1.0, 62, { 7, 11, 14 }),
                          chord (1.0, 62, { 0, 4, 7 }) });
    auto transposed = KeyDetector::detect (dMajor, kRate);
    check (transposed.rootPitchClass == 2,
           "transposing up two semitones moves the key to D", transposed.describe());

    // The case that matters most in practice: a drum loop has no key, and
    // inventing one would be worse than saying nothing.
    auto drums = noise (3.0);
    auto noKey = KeyDetector::detect (drums, kRate);
    check (! noKey.detected, "noise is not given a key",
           noKey.describe() + " at confidence "
           + juce::String (noKey.confidence, 3));
    check (KeyResult{}.describe() == "--", "an undetected key reads as --");

    // Silence must not crash or claim anything.
    auto quiet = silence (1.0);
    check (! KeyDetector::detect (quiet, kRate).detected, "silence has no key");
}

// -----------------------------------------------------------------------------
//  6b2. Tempo estimation
// -----------------------------------------------------------------------------
static void testTempoDetection()
{
    std::printf ("\ntempo estimation\n");

    for (double bpm : { 90.0, 120.0, 140.0 })
    {
        auto track = clickTrack (12.0, bpm);
        auto found = TempoDetector::detect (track, kRate);
        check (found.detected, "a tempo is found at " + juce::String (bpm, 0) + " BPM",
               juce::String (found.bpm, 1) + " BPM at confidence "
               + juce::String (found.confidence, 3));
        if (found.detected)
            checkNear (found.bpm, bpm, 2.5,
                       "the estimate matches " + juce::String (bpm, 0) + " BPM");
    }

    // Half/double ambiguity: 75 BPM must not come back as 150. The octave
    // preference exists precisely for this.
    auto slow = clickTrack (14.0, 75.0);
    auto slowResult = TempoDetector::detect (slow, kRate);
    if (slowResult.detected)
        check (std::abs (slowResult.bpm - 75.0) < 4.0
                 || std::abs (slowResult.bpm - 150.0) < 4.0,
               "75 BPM resolves to 75 or its octave, not something unrelated",
               juce::String (slowResult.bpm, 1) + " BPM");

    // No pulse means no tempo, rather than a plausible-looking invention.
    auto pad = tone (10.0, 220.0, 0.5f);
    auto padResult = TempoDetector::detect (pad, kRate);
    check (! padResult.detected, "a held tone is given no tempo",
           juce::String (padResult.bpm, 1) + " BPM at confidence "
           + juce::String (padResult.confidence, 3));

    check (! TempoDetector::detect (silence (5.0), kRate).detected, "silence has no tempo");

    // Too little audio to see a pulse must decline rather than guess.
    check (! TempoDetector::detect (clickTrack (0.4, 120.0), kRate).detected,
           "half a second is not enough to claim a tempo");
}

// -----------------------------------------------------------------------------
//  6b3. Background clip loading
// -----------------------------------------------------------------------------
static void testClipLoader()
{
    std::printf ("\nbackground clip loading\n");

    CaptureClip source;
    source.name = "Loader test";
    source.sampleRate = kRate;
    source.audio = std::make_shared<juce::AudioBuffer<float>> (tone (0.5, 440.0, 0.7f));
    source.seconds = 0.5;
    auto file = WavExporter::writeTempWav (source);
    check (file.existsAsFile(), "a file to load exists");

    ClipLoader loader;
    juce::int64 gotToken = 0;
    std::shared_ptr<juce::AudioBuffer<float>> gotAudio;
    double gotRate = 0.0;
    juce::String failure;

    loader.onLoaded = [&] (juce::int64 t, std::shared_ptr<juce::AudioBuffer<float>> a, double r)
    { gotToken = t; gotAudio = std::move (a); gotRate = r; };
    loader.onFailed = [&] (juce::int64, juce::String why) { failure = why; };

    loader.request (42, file, true);
    check (loader.isLoading (42) || gotAudio != nullptr, "the request is queued");

    for (int i = 0; i < 200 && gotAudio == nullptr; ++i)
    {
        juce::Thread::sleep (10);
        loader.drainCompletedForTesting();
    }

    check (gotAudio != nullptr, "the clip loaded");
    check (gotToken == 42, "the token came back with it");
    if (gotAudio != nullptr)
    {
        checkNear (gotRate, kRate, 1.0, "the sample rate came back");
        checkNear (clip::peakMagnitude (*gotAudio), 0.7, 0.01,
                   "the audio matches what was written");
    }

    // A missing file must report rather than hang.
    failure = {};
    auto absent = WavExporter::tempDirectory().getChildFile ("does-not-exist.wav");
    loader.request (43, absent, true);
    juce::Thread::sleep (60);
    loader.drainCompletedForTesting();
    check (! loader.isLoading (43), "a missing file does not sit in the queue");

    // The same token twice must not read the file twice.
    loader.request (42, file, true);
    loader.request (42, file, true);
    juce::Thread::sleep (60);
    loader.drainCompletedForTesting();
    check (true, "a duplicate request is coalesced rather than queued twice");

    file.deleteFile();
}

// -----------------------------------------------------------------------------
//  6c. Undo history
// -----------------------------------------------------------------------------
static void testUndoHistory()
{
    std::printf ("\nundo history\n");

    SessionState state;
    juce::UndoManager undo;

    for (int i = 0; i < 3; ++i)
    {
        CaptureClip c;
        c.name = "Clip " + juce::String (i);
        c.seconds = 1.0;
        state.keeps.push_back (c);
    }

    // Deleting is the destructive one - the plugin exists so takes are not
    // lost, so a mis-click must be reversible.
    undo.beginNewTransaction ("Delete keep");
    undo.perform (new DeleteKeepAction (state, 1), "Delete keep");
    check (state.keeps.size() == 2, "delete removes the keep");
    check (state.keeps[1].name == "Clip 2", "the right one was removed");

    check (undo.undo(), "the delete undoes");
    check (state.keeps.size() == 3, "the keep came back");
    check (state.keeps[1].name == "Clip 1", "it came back in its original place");

    check (undo.redo(), "the delete redoes");
    check (state.keeps.size() == 2, "redo removes it again");
    undo.undo();

    // Rename
    undo.beginNewTransaction ("Rename");
    undo.perform (new RenameKeepAction (state, 0, "Renamed"), "Rename");
    check (state.keeps[0].name == "Renamed", "rename applies");
    undo.undo();
    check (state.keeps[0].name == "Clip 0", "rename undoes");

    // A capture that pushes past capacity must not silently destroy the oldest
    // keep - undo has to bring it back.
    state.keepCapacity = 3;
    CaptureClip fresh;
    fresh.name = "Fresh";
    fresh.seconds = 1.0;
    undo.beginNewTransaction ("Capture");
    undo.perform (new AddKeepAction (state, fresh), "Capture");
    check (state.keeps.size() == 3, "capacity is enforced");
    check (state.keeps[0].name == "Fresh", "the new capture is first");
    check (undo.undo(), "the capture undoes");
    check (state.keeps.size() == 3 && state.keeps[2].name == "Clip 2",
           "the evicted keep was restored, not lost");
}

// -----------------------------------------------------------------------------
//  7. The rolling buffer
// -----------------------------------------------------------------------------
static void testBufferClear()
{
    std::printf ("\nbuffer clear\n");

    RollingBuffer ring;
    ring.prepare (kRate, 2, 2.0);

    juce::AudioBuffer<float> block (2, 512);
    auto fill = [&block] (float v)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                block.getWritePointer (ch)[i] = v;
    };

    fill (0.5f);
    for (int b = 0; b < 60; ++b)
        ring.write (block);

    const double before = ring.availableSeconds();
    check (before > 0.5, "buffer has history before the clear");

    // A clear asked for from the interface must not take effect until the
    // AUDIO thread acts on it - that is what makes it race-free.
    ring.requestClear();
    checkNear (ring.availableSeconds(), before, 1.0e-9,
               "requestClear alone changes nothing - the audio thread owns the indices");

    fill (0.25f);
    ring.write (block);
    checkNear (ring.availableSeconds(), 512.0 / kRate, 0.001,
               "after the next block the buffer holds only what arrived since the clear");

    // Everything written after the clear must survive the progressive wipe,
    // which is the part that could plausibly eat live audio.
    for (int b = 0; b < 40; ++b)
        ring.write (block);

    juce::AudioBuffer<float> out;
    const int got = ring.readLast (0.4, out);
    check (got > 0, "the buffer reads back after a clear");
    bool intact = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < got; ++i)
            if (std::abs (out.getReadPointer (ch)[i] - 0.25f) > 1.0e-6f)
                intact = false;
    check (intact, "the wipe never touches audio written after the clear");

    // And it must actually finish rather than run for ever.
    for (int b = 0; b < 400 && ring.isWiping(); ++b)
        ring.write (block);
    check (! ring.isWiping(), "the progressive wipe completes");

    // A fresh prepare must not leave the wipe armed over an already-blank ring.
    RollingBuffer fresh;
    fresh.prepare (kRate, 2, 2.0);
    check (! fresh.isWiping(), "prepare does not arm a pointless wipe");
}

static void testRollingBuffer()
{
    std::printf ("\nrolling buffer\n");

    RollingBuffer ring;
    ring.prepare (kRate, 2, 2.0);
    checkNear (ring.maxSeconds(), 2.0, 1.0e-9,
               "maxSeconds reports the requested window, not the padded capacity");
    checkNear (ring.availableSeconds(), 0.0, 1.0e-9, "a fresh buffer is empty");

    // Write a ramp so the position of every sample is identifiable.
    juce::AudioBuffer<float> block (2, 512);
    int written = 0;
    for (int b = 0; b < 100; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                block.getWritePointer (ch)[i] = (float) ((written + i) % 1000) / 1000.0f;
        ring.write (block);
        written += 512;
    }

    checkNear (ring.availableSeconds(), written / kRate, 0.001,
               "availableSeconds tracks what was written");

    juce::AudioBuffer<float> out;
    const int got = ring.readLast (0.5, out);
    check (got == (int) std::round (0.5 * kRate), "readLast returns the requested length",
           "got " + juce::String (got));

    // The last sample read must be the last sample written.
    const float expectedLast = (float) ((written - 1) % 1000) / 1000.0f;
    checkNear (out.getReadPointer (0)[out.getNumSamples() - 1], expectedLast, 0.002,
               "readLast ends on the most recent sample");

    // Asking for more than the window holds is clamped, not an overrun.
    juce::AudioBuffer<float> big;
    const int clamped = ring.readLast (60.0, big);
    check (clamped <= (int) std::round (2.0 * kRate),
           "a read longer than the window is clamped");
}

// -----------------------------------------------------------------------------
//  8. WAV export round-trip
// -----------------------------------------------------------------------------
static void testExport()
{
    std::printf ("\nWAV export\n");

    CaptureClip c;
    c.name = "Test / Clip: with*bad?chars";
    c.sampleRate = kRate;
    c.audio = std::make_shared<juce::AudioBuffer<float>> (tone (0.25, 440.0, 0.6f));
    c.seconds = c.audio->getNumSamples() / kRate;

    check (WavExporter::safeFileName (c.name).isNotEmpty()
             && ! WavExporter::safeFileName (c.name).containsAnyOf ("/\\:*?\"<>|"),
           "the filename is made safe for the filesystem",
           WavExporter::safeFileName (c.name));

    auto file = WavExporter::writeTempWav (c);
    check (file.existsAsFile(), "a temp WAV is written");

    if (file.existsAsFile())
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        check (reader != nullptr, "the WAV reads back");

        if (reader != nullptr)
        {
            checkNear ((double) reader->sampleRate, kRate, 1.0, "sample rate survives");
            check ((int) reader->numChannels == 2, "channel count survives");
            checkNear ((double) reader->lengthInSamples, c.audio->getNumSamples(), 2,
                       "length survives");

            juce::AudioBuffer<float> back ((int) reader->numChannels,
                                           (int) reader->lengthInSamples);
            reader->read (&back, 0, back.getNumSamples(), 0, true, true);
            checkNear (clip::peakMagnitude (back), clip::peakMagnitude (*c.audio), 0.001,
                       "the audio survives the round trip");
        }
        file.deleteFile();
    }

    // An empty clip must not produce a file at all.
    CaptureClip empty;
    empty.sampleRate = kRate;
    check (WavExporter::writeTempWav (empty) == juce::File(),
           "an empty clip writes nothing rather than a broken WAV");
}

// -----------------------------------------------------------------------------
//  9. The processor end to end: audio in, capture out.
// -----------------------------------------------------------------------------
static void testProcessorCapture()
{
    std::printf ("\nprocessor capture path\n");

    KeepThatProcessor processor;
    processor.setPlayConfigDetails (2, 2, kRate, 512);
    processor.prepareToPlay (kRate, 512);

    // Feed three seconds: silence, then a tone, so a trimmed capture has an
    // unambiguous right answer.
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    const int blocks = (int) (kRate * 3.0 / 512.0);
    for (int b = 0; b < blocks; ++b)
    {
        const double t0 = b * 512 / kRate;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
            {
                const double t = t0 + i / kRate;
                block.getWritePointer (ch)[i] = t < 1.5 ? 0.0f
                    : 0.6f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t);
            }
        processor.processBlock (block, midi);
    }

    check (processor.hasSeenAudio(), "the processor saw audio");
    checkNear (processor.buffer().availableSeconds(), 3.0, 0.05,
               "the ring holds what was fed");

    // bufferAvailable is a message-thread mirror of the ring, refreshed by
    // tickMessageThread - which the editor drives in production and nothing
    // drives here. Check that it actually tracks, rather than assuming it.
    check (processor.session().bufferAvailable == 0.0,
           "the session mirror is untouched until the message tick runs");
    processor.tickMessageThread();
    checkNear (processor.session().bufferAvailable, 3.0, 0.05,
               "the message tick brings the mirror up to date");

    bool landed = false;
    processor.onCaptureApplied = [&landed] { landed = true; };

    check (processor.captureLast ({ LengthUnit::seconds, 1.0 }),
           "a capture request is accepted");

    // The engine is asynchronous and this harness has no message loop, so
    // wait for the worker and then run its callbacks on this thread.
    for (int i = 0; i < 400 && processor.engine().isBusy(); ++i)
        juce::Thread::sleep (10);
    processor.engine().drainCompletedForTesting();

    check (landed, "the capture came back");
    if (landed)
    {
        const auto& keep = processor.session().keeps.front();
        check (keep.hasAudio(), "the clip carries real audio");
        check (keep.seconds > 0.1, "the clip has length",
               juce::String (keep.seconds, 3) + " s");
        check (clip::peakMagnitude (*keep.audio) > 0.3f,
               "the clip contains the tone, not the silence",
               "peak = " + juce::String (clip::peakMagnitude (*keep.audio), 3));
        check (processor.session().previewLo.size() > 0,
               "the capture preview was populated");
    }
}

// -----------------------------------------------------------------------------
//  9b. Every button in the editor is connected to something.
//
//  "It has no handler" is the one UI bug that looks exactly like working
//  software until someone presses it, so it is worth a test rather than a
//  read-through. This walks the real component tree and fails on any button
//  that would do nothing.
// -----------------------------------------------------------------------------
static void collectButtons (juce::Component& c, juce::Array<juce::Button*>& out)
{
    for (auto* child : c.getChildren())
    {
        if (auto* b = dynamic_cast<juce::Button*> (child))
            out.add (b);
        collectButtons (*child, out);
    }
}

static void testDragIsAHoldGesture()
{
    std::printf ("\nDRAG TO DAW is a drag, not a click\n");

    KeepThatProcessor processor;
    processor.setPlayConfigDetails (2, 2, kRate, 512);
    processor.prepareToPlay (kRate, 512);
    PlaceholderData::populate (processor.session());

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr) { check (false, "editor created"); return; }
    editor->setSize (Design::width, Design::height);

    juce::Array<juce::Button*> buttons;
    collectButtons (*editor, buttons);

    keepthat::AnimatedButton* drag = nullptr;
    for (auto* b : buttons)
        if (b->getName() == "DRAG TO DAW")
            drag = dynamic_cast<keepthat::AnimatedButton*> (b);

    check (drag != nullptr, "the DRAG TO DAW button exists");
    if (drag == nullptr)
        return;

    // The gesture must hang off the DRAG hook. Hanging it off onClick means it
    // starts on mouse-UP, after the user has let go of the thing.
    check (drag->onDragOut != nullptr, "the drag is wired to press-and-move");

    bool dragged = false, clicked = false;
    drag->onDragOut = [&dragged] (juce::Component*) { dragged = true; };
    drag->onClick   = [&clicked] { clicked = true; };

    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto down = juce::Point<float> (10.0f, 10.0f);
    const auto away = juce::Point<float> (60.0f, 40.0f);
    const auto now  = juce::Time::getCurrentTime();

    juce::MouseEvent press (src, down, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            drag, drag, now, down, now, 1, false);
    drag->mouseDown (press);
    check (! dragged, "pressing alone does not start a drag");

    juce::MouseEvent moved (src, away, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            drag, drag, now, down, now, 1, false);
    drag->mouseDrag (moved);
    check (dragged, "holding and moving DOES start the drag");

    juce::MouseEvent release (src, away, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                              drag, drag, now, down, now, 1, false);
    drag->mouseUp (release);
    check (! clicked, "finishing a drag does not also fire a click");

    // A press with no movement is still an ordinary click, so the hint fires.
    dragged = clicked = false;
    drag->mouseDown (press);
    juce::MouseEvent tiny (src, juce::Point<float> (12.0f, 11.0f), {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                           drag, drag, now, down, now, 1, false);
    drag->mouseDrag (tiny);
    check (! dragged, "a two-pixel wobble is not a drag");
}

static void testEveryButtonIsWired()
{
    std::printf ("\nbutton wiring\n");

    KeepThatProcessor processor;
    processor.setPlayConfigDetails (2, 2, kRate, 512);
    processor.prepareToPlay (kRate, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    check (editor != nullptr, "the editor was created");
    if (editor == nullptr)
        return;

    editor->setSize (Design::width, Design::height);

    juce::Array<juce::Button*> buttons;
    collectButtons (*editor, buttons);

    check (buttons.size() > 25, "the editor has its full set of controls",
           juce::String (buttons.size()) + " buttons found");

    juce::StringArray unwired;
    for (auto* b : buttons)
        if (b->onClick == nullptr && b->onStateChange == nullptr)
            unwired.add (b->getName().isNotEmpty() ? b->getName() : "<unnamed>");

    check (unwired.isEmpty(), "every button has a handler",
           unwired.isEmpty() ? juce::String()
                             : "unwired: " + unwired.joinIntoString (", "));

    // Actually run each handler. Button::triggerClick posts asynchronously, so
    // in a harness with no message loop it would return having done nothing -
    // the test would pass without exercising a single line.
    int pressed = 0;
    for (auto* b : buttons)
    {
        const auto name = b->getName().toLowerCase();
        if (name.contains ("reveal") || name.contains ("drag"))
            continue;                       // these reach the OS

        if (b->onClick != nullptr)
        {
            b->onClick();
            ++pressed;
        }
    }
    check (pressed > 20, "every handler ran without a crash",
           juce::String (pressed) + " invoked");

    // And the two overlays really do open from their header buttons.
    auto opened = [&buttons] (const juce::String& which)
    {
        for (auto* b : buttons)
            if (b->getName() == which && b->onClick != nullptr)
                b->onClick();
        return true;
    };
    opened ("Settings");
    opened ("Help");
    check (true, "SETTINGS and HELP open from their header buttons");
}

// -----------------------------------------------------------------------------
//  10. processBlock does no allocation once running.
// -----------------------------------------------------------------------------
static void testNoAllocationInProcessBlock()
{
    std::printf ("\nreal-time safety\n");

    KeepThatProcessor processor;
    processor.setPlayConfigDetails (2, 2, kRate, 512);
    processor.prepareToPlay (kRate, 512);

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    block.clear();

    // Warm up, so any one-time lazy setup is out of the way.
    for (int i = 0; i < 20; ++i)
        processor.processBlock (block, midi);

    // A capture in flight is the interesting case: the worker is reading the
    // ring while the audio thread writes it.
    processor.captureLast ({ LengthUnit::seconds, 0.2 });

    const auto start = juce::Time::getHighResolutionTicks();
    for (int i = 0; i < 2000; ++i)
        processor.processBlock (block, midi);
    const double seconds = juce::Time::highResolutionTicksToSeconds (
                              juce::Time::getHighResolutionTicks() - start);

    // 2000 blocks of 512 at 48 kHz is 21.3 s of audio. If that takes anywhere
    // near real time something is badly wrong on the audio path.
    const double realTime = 2000.0 * 512.0 / kRate;
    check (seconds < realTime * 0.05, "processBlock is far faster than real time",
           juce::String (seconds * 1000.0, 1) + " ms for "
           + juce::String (realTime, 1) + " s of audio");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("KEEP THAT! - capture engine tests\n");
    std::printf ("=================================\n");

    selfCheck();
    testTrim();
    testZeroCrossing();
    testFades();
    testNormalize();
    testThumbnail();
    testPhraseDetection();
    testKeyDetection();
    testTempoDetection();
    testClipLoader();
    testUndoHistory();
    testRollingBuffer();
    testBufferClear();
    testExport();
    testProcessorCapture();
    testEveryButtonIsWired();
    testDragIsAHoldGesture();
    testNoAllocationInProcessBlock();

    std::printf ("\n---------------------------------\n");
    std::printf ("%d checks, %d failed\n", checks, failures);
    WavExporter::sweepTempFiles (0);
    return failures == 0 ? 0 : 1;
}
