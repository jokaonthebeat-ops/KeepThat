/*
    CaptureModel.h - the data the interface runs on.

    Milestone 1 is the UI and interaction shell, but the parts that have to be
    real-time safe are built for real from the start, because retrofitting that
    is how you get clicks in someone's session:

      * RollingBuffer is a genuine lock-free circular buffer. The audio thread
        only ever writes, and only ever with memcpy + a relaxed store.
      * MeterState and WaveTrail are written from the audio thread with atomics
        and read from the message thread. No locks, no allocation, no logging.

    What is still placeholder in milestone 1 is everything downstream of the
    buffer: the recent-keeps list, the phrase-detection verdict and the capture
    preview waveform are generated (see PlaceholderData), because the extraction
    and detection engine is the milestone-2 job. Those all sit behind the same
    accessors the real engine will fill, so connecting it is a swap, not a
    rewrite.
*/

#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>

namespace keepthat
{

// -----------------------------------------------------------------------------
//  Capture length selection
// -----------------------------------------------------------------------------
enum class LengthUnit { bars, seconds, phrase };

struct CaptureLength
{
    LengthUnit unit = LengthUnit::bars;
    double     amount = 4.0;              // bars, or seconds

    juce::String label() const
    {
        switch (unit)
        {
            case LengthUnit::bars:
                return juce::String ((int) amount) + (amount == 1.0 ? " BAR" : " BARS");
            case LengthUnit::seconds:
                return juce::String ((int) amount) + " SEC";
            case LengthUnit::phrase:
            default:
                return "PHRASE";
        }
    }

    // Seconds this selection covers at the given tempo. A bar is assumed 4/4
    // until the host supplies a real time signature.
    double seconds (double bpm) const
    {
        switch (unit)
        {
            case LengthUnit::bars:    return amount * 4.0 * 60.0 / juce::jmax (20.0, bpm);
            case LengthUnit::seconds: return amount;
            case LengthUnit::phrase:
            default:                  return 0.0;   // filled by phrase detection
        }
    }
};

// The eight selector buttons, in the approved order.
inline const std::array<CaptureLength, 8>& captureLengths()
{
    static const std::array<CaptureLength, 8> table {{
        { LengthUnit::bars,    1.0 }, { LengthUnit::bars,    2.0 },
        { LengthUnit::bars,    4.0 }, { LengthUnit::bars,    8.0 },
        { LengthUnit::seconds, 15.0 }, { LengthUnit::seconds, 30.0 },
        { LengthUnit::seconds, 60.0 }, { LengthUnit::phrase,  0.0 }
    }};
    return table;
}

// -----------------------------------------------------------------------------
//  RollingBuffer - lock-free circular capture.
//
//  Single producer (audio thread) / single consumer (message thread). The
//  write index is the only shared mutable state; a release store publishes
//  the samples written before it, and the reader's acquire load sees them.
//
//  The reader can be overrun by the writer while it copies - that is inherent
//  to a lock-free ring and is the right trade here, because the alternative is
//  blocking the audio thread. readLast() re-checks the write index afterwards
//  and reports the overrun rather than handing back a torn buffer.
// -----------------------------------------------------------------------------
class RollingBuffer
{
public:
    void prepare (double sampleRateIn, int channelsIn, double maxSeconds)
    {
        sampleRate = sampleRateIn;
        channels   = juce::jlimit (1, 2, channelsIn);
        requested  = maxSeconds;
        capacity   = juce::nextPowerOfTwo ((int) std::ceil (sampleRateIn * maxSeconds));
        storage.setSize (channels, capacity, false, true, false);
        storage.clear();
        writePos.store (0, std::memory_order_relaxed);
        filled.store (0, std::memory_order_relaxed);
    }

    void reset()
    {
        storage.clear();
        writePos.store (0, std::memory_order_relaxed);
        filled.store (0, std::memory_order_relaxed);
    }

    // AUDIO THREAD. No allocation, no locks.
    void write (const juce::AudioBuffer<float>& in) noexcept
    {
        if (capacity == 0)
            return;

        const int n = in.getNumSamples();
        const int mask = capacity - 1;
        int pos = writePos.load (std::memory_order_relaxed);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* src = in.getReadPointer (juce::jmin (ch, in.getNumChannels() - 1));
            float* dst = storage.getWritePointer (ch);
            int p = pos;

            // Two memcpys at most: the ring wraps at a power-of-two boundary.
            const int first = juce::jmin (n, capacity - p);
            std::memcpy (dst + p, src, sizeof (float) * (size_t) first);
            if (first < n)
                std::memcpy (dst, src + first, sizeof (float) * (size_t) (n - first));
            juce::ignoreUnused (mask, p);
        }

        pos = (pos + n) & mask;
        writePos.store (pos, std::memory_order_release);

        const int wasFilled = filled.load (std::memory_order_relaxed);
        if (wasFilled < capacity)
            filled.store (juce::jmin (capacity, wasFilled + n), std::memory_order_relaxed);
    }

    double availableSeconds() const noexcept
    {
        if (sampleRate <= 0.0)
            return 0.0;
        return juce::jmin (requested, filled.load (std::memory_order_relaxed) / sampleRate);
    }

    /** The window the user asked for. `capacity` is rounded up to a power of
        two so the ring can mask instead of divide; reporting that padded value
        would put a buffer length on screen that nobody selected. */
    double maxSeconds() const noexcept { return requested; }

    int getSampleRate() const noexcept { return (int) sampleRate; }

    // MESSAGE THREAD. Returns the number of samples copied, or 0 if the writer
    // lapped the reader mid-copy (caller retries; it never blocks the audio).
    int readLast (double seconds, juce::AudioBuffer<float>& dest) const
    {
        if (capacity == 0 || seconds <= 0.0)
            return 0;

        const int want = juce::jmin ((int) std::round (seconds * sampleRate),
                                     filled.load (std::memory_order_relaxed));
        if (want <= 0)
            return 0;

        const int startMark = writePos.load (std::memory_order_acquire);
        const int mask = capacity - 1;
        dest.setSize (channels, want, false, false, true);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* src = storage.getReadPointer (ch);
            float* dst = dest.getWritePointer (ch);
            int p = (startMark - want) & mask;
            const int first = juce::jmin (want, capacity - p);
            std::memcpy (dst, src + p, sizeof (float) * (size_t) first);
            if (first < want)
                std::memcpy (dst + first, src, sizeof (float) * (size_t) (want - first));
        }

        // Did the writer lap us while we copied? If so the tail is torn.
        const int endMark = writePos.load (std::memory_order_acquire);
        const int advanced = (endMark - startMark) & mask;
        return advanced > (capacity - want) ? 0 : want;
    }

private:
    juce::AudioBuffer<float> storage;
    double sampleRate = 0.0, requested = 0.0;
    int channels = 2, capacity = 0;
    std::atomic<int> writePos { 0 };
    std::atomic<int> filled   { 0 };
};

// -----------------------------------------------------------------------------
//  MeterState - peak + RMS with peak hold, written from the audio thread.
// -----------------------------------------------------------------------------
struct MeterState
{
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> rmsL  { 0.0f }, rmsR  { 0.0f };
    std::atomic<float> holdL { 0.0f }, holdR { 0.0f };

    // AUDIO THREAD.
    void push (const juce::AudioBuffer<float>& in) noexcept
    {
        for (int ch = 0; ch < juce::jmin (2, in.getNumChannels()); ++ch)
        {
            const float pk  = in.getMagnitude (ch, 0, in.getNumSamples());
            const float rms = in.getRMSLevel  (ch, 0, in.getNumSamples());
            auto& peak = ch == 0 ? peakL : peakR;
            auto& mean = ch == 0 ? rmsL  : rmsR;
            auto& hold = ch == 0 ? holdL : holdR;

            // Fast attack, slow release - a meter that reads honestly.
            const float prev = peak.load (std::memory_order_relaxed);
            peak.store (pk > prev ? pk : prev * 0.82f + pk * 0.18f,
                        std::memory_order_relaxed);
            mean.store (mean.load (std::memory_order_relaxed) * 0.7f + rms * 0.3f,
                        std::memory_order_relaxed);
            const float h = hold.load (std::memory_order_relaxed);
            hold.store (pk > h ? pk : h * 0.995f, std::memory_order_relaxed);
        }
    }
};

// -----------------------------------------------------------------------------
//  WaveTrail - the scrolling min/max envelope behind the LIVE INPUT display.
//
//  The audio thread folds each block into one bin and advances an index; the
//  UI reads the whole ring every frame. A stale or half-written bin costs one
//  pixel of a moving waveform, which is a fair price for never locking.
// -----------------------------------------------------------------------------
template <int Bins>
struct WaveTrail
{
    static constexpr int size = Bins;
    std::array<std::atomic<float>, Bins> lo {}, hi {};
    std::atomic<int> head { 0 };

    // AUDIO THREAD.
    void push (const juce::AudioBuffer<float>& in) noexcept
    {
        juce::Range<float> r { 0.0f, 0.0f };
        for (int ch = 0; ch < juce::jmin (2, in.getNumChannels()); ++ch)
            r = r.getUnionWith (in.findMinMax (ch, 0, in.getNumSamples()));

        const int h = head.load (std::memory_order_relaxed);
        lo[(size_t) h].store (r.getStart(), std::memory_order_relaxed);
        hi[(size_t) h].store (r.getEnd(),   std::memory_order_relaxed);
        head.store ((h + 1) % Bins, std::memory_order_release);
    }

    // MESSAGE THREAD. Oldest-first into `out`, which must hold `Bins` pairs.
    void readOrdered (float* outLo, float* outHi) const
    {
        const int h = head.load (std::memory_order_acquire);
        for (int i = 0; i < Bins; ++i)
        {
            const size_t idx = (size_t) ((h + i) % Bins);
            outLo[i] = lo[idx].load (std::memory_order_relaxed);
            outHi[i] = hi[idx].load (std::memory_order_relaxed);
        }
    }
};

// -----------------------------------------------------------------------------
//  A stored capture. Milestone 1 fills these from PlaceholderData; the real
//  engine will fill the same fields from an extracted region of the buffer.
// -----------------------------------------------------------------------------
struct CaptureClip
{
    juce::String name;
    double  seconds = 0.0;
    bool    favourite = false;
    bool    isFromLiveBuffer = false;      // true once the engine made it
    std::vector<float> thumbLo, thumbHi;   // normalised envelope, -1..1

    /** The recovered audio. Shared because the preview player reads it on the
        audio thread while the message thread still owns the clip; the pointer
        is swapped under a lock, never the samples. Null for a clip restored
        from a previous session whose WAV has not been re-read. */
    std::shared_ptr<juce::AudioBuffer<float>> audio;
    double sampleRate = 0.0;
    float  peakDb = -100.0f;

    juce::String key { "--" };             // detected at capture time
    double detectedBars = 0.0;             // 0 when the host had no tempo
    float  confidence = 0.0f;              // phrase mode only
    bool   trimmedToZeroCrossing = false;

    /** Where it lives on disk, once saved or exported. */
    juce::File file;
    juce::int64 createdAtMs = 0;

    /** Stable identity for this clip, independent of its position in the list.
        A background load started for index 3 must not land on whatever is at
        index 3 by the time it finishes - the list can be reordered, deleted
        from, or undone while the disk is busy. */
    juce::int64 id = 0;

    bool hasAudio() const noexcept
    { return audio != nullptr && audio->getNumSamples() > 0; }

    juce::String durationText() const
    {
        const int total = (int) std::round (seconds);
        return juce::String (total / 60).paddedLeft ('0', 2) + ":"
             + juce::String (total % 60).paddedLeft ('0', 2);
    }
};

// -----------------------------------------------------------------------------
//  The recovery-tool toggles and the destination grid.
// -----------------------------------------------------------------------------
enum class RecoveryTool { autoTrim, silenceDetect, zeroCrossing, fade, normalize, dragExport };
enum class Destination  { dawDrag, sampler, playlist, folder, desktop };

struct PhraseVerdict
{
    bool   detected = false;
    double suggestedBars = 0.0;
    float  confidence = 0.0f;              // 0..1
    double startSeconds = 0.0, endSeconds = 0.0;
    std::vector<float> thumbLo, thumbHi;
};

// -----------------------------------------------------------------------------
//  SessionState - everything the editor shows that is not a live meter.
//
//  Message-thread only. The processor owns it; the editor reads and mutates it
//  in response to clicks.
// -----------------------------------------------------------------------------
struct SessionState
{
    juce::String presetName { "Hook Recovery Session" };
    juce::String sourceName { "Input 1 (Audio Interface)" };

    bool   armed = true;                   // "Always Listening"

    // A fresh instance has captured nothing and measured nothing. These read
    // as empty until real audio arrives, rather than showing the mockup's
    // numbers - a capture tool claiming 4:27 of history it does not have is
    // the one lie this product cannot afford.
    double bufferAvailable = 0.0;
    double bufferMax = 480.0;              // 8:00

    double bpm = 0.0;                      // 0 = the host has not said
    juce::String key { "--" };
    float  peakDb = -100.0f, rmsDb = -100.0f;
    bool   stereo = true;
    juce::String inputQuality { "--" };

    int selectedLength = 2;                // "4 BARS"
    int selectedSeconds = 5;               // "30 SEC" - the second row's pick

    PhraseVerdict phrase;

    // Capture preview
    std::vector<float> previewLo, previewHi;
    double previewStart = 0.0, previewEnd = 0.0;
    float  trimLeft = 0.0f, trimRight = 1.0f;   // normalised handles
    float  playhead = -1.0f;                    // <0 = not playing
    bool   showBarsBeats = true;

    Destination destination = Destination::dawDrag;

    /** Where each destination writes. Defaults are sensible, but the user can
        point any of them anywhere and it persists - which is what makes
        SAMPLER and PLAYLIST real targets rather than labels implying an
        integration that does not exist. Empty = use the built-in default. */
    juce::File destinationFolder[5];

    /** PLAYLIST additionally maintains an .m3u of everything sent to it, so
        the button does what its name says. */
    bool writePlaylistFile = true;

    /** Interface performance, from the SETTINGS panel.
        ANIMATION_SPEC's preferred 60 fps is the default, with its 30 fps
        fallback a switch away. Measured on the standalone, the two cost about
        40 % and 22 % of a core - the difference is CoreAnimation compositing
        a 1491 x 1055 layer, not the drawing - so low power is worth reaching
        for on an older machine or a heavy session. */
    bool lowPowerMode = false;
    bool reduceMotion = false;

    std::vector<CaptureClip> keeps;
    int selectedKeep = 0;
    int keepCapacity = 100;

    /** The footer's status line - what just happened, in the user's terms. */
    juce::String lastMessage;

    /** Points the capture preview at `clip`, building its envelope from the
        real audio when the clip has any. */
    void loadPreviewFrom (const CaptureClip& clip);

    /** True while the selected clip's audio is still being read from disk. */
    bool previewLoading = false;

    /** Finds a clip by its stable id. */
    CaptureClip* findById (juce::int64 id)
    {
        for (auto& c : keeps)
            if (c.id == id)
                return &c;
        return nullptr;
    }

    /** Ids are handed out from here so they are unique within a session. */
    juce::int64 nextClipId = 1;

    // The macro knobs and every recovery-tool switch are APVTS parameters,
    // not session fields - see params/Parameters.h. They used to live here,
    // and holding a second copy of a value the host can automate is how the
    // interface and the engine end up disagreeing.

    juce::String availableText() const  { return timeText (bufferAvailable); }
    juce::String maxText() const        { return timeText (bufferMax); }

    static juce::String timeText (double seconds)
    {
        const int t = (int) std::floor (seconds);
        return juce::String (t / 60).paddedLeft ('0', 2) + ":"
             + juce::String (t % 60).paddedLeft ('0', 2);
    }

    // -04:27.000 style, as the capture preview and phrase card show it.
    static juce::String stampText (double seconds)
    {
        const bool neg = seconds < 0.0;
        const double a = std::abs (seconds);
        const int m = (int) (a / 60.0);
        const int s = (int) std::fmod (a, 60.0);
        const int ms = (int) std::round (std::fmod (a, 1.0) * 1000.0);
        return juce::String (neg ? "-" : "")
             + juce::String (m).paddedLeft ('0', 2) + ":"
             + juce::String (s).paddedLeft ('0', 2) + "."
             + juce::String (ms).paddedLeft ('0', 3);
    }
};

// -----------------------------------------------------------------------------
//  PlaceholderData - the milestone-1 stand-in for the capture engine.
//
//  Deterministic: the same seed every time, so `make uishot` renders a shot
//  that can be compared with the approved mockup pixel for pixel.
// -----------------------------------------------------------------------------
namespace PlaceholderData
{
    // A musically plausible envelope: bar-level dynamics, transients on the
    // beat, and a decaying tail - not white noise, which reads as broken.
    void fillEnvelope (std::vector<float>& lo, std::vector<float>& hi,
                       int bins, juce::uint32 seed, float density = 1.0f);

    /** Fills the session with the approved mockup's demo state. NOT called in
        normal operation - a fresh instance starts empty, because showing eight
        captures that were never captured is a lie. `make uishot ARGS="... demo"`
        calls it for the reference and marketing shots. */
    void populate (SessionState&);
}

} // namespace keepthat
