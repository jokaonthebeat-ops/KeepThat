/*
    CaptureEngine.h - the recovery path, from a KEEP LAST press to a finished
    clip.

    The threading contract, straight from THREADING_EXPORT_SPEC.md:

        audio thread    ring-buffer write only
        message thread  UI, and posting a request
        worker          snapshot, phrase detection, zero-cross, fades,
                        normalize, thumbnail, WAV writing

    So `requestCapture` does almost nothing: it copies the current parameters
    and the host's tempo/PPQ, and hands a job to the worker. The worker takes
    the snapshot out of the ring (a lock-free read that can be lapped and
    retried, never a lock), does the work, and posts the finished clip back to
    the message thread.

    A request while one is already running is dropped rather than queued. Two
    KEEP LAST presses a frame apart mean one capture, not two - and dropping is
    honest, whereas queueing would hand back a clip whose "last N bars" ended
    somewhere the user never asked for.
*/

#pragma once
#include <JuceHeader.h>
#include "CaptureModel.h"
#include "PhraseDetector.h"
#include "KeyDetector.h"
#include "TempoDetector.h"
#include "../params/Parameters.h"

namespace keepthat
{

/** Where the host's timeline was when the user pressed the button. */
struct HostTiming
{
    double bpm = 0.0;                 // 0 = host has no tempo
    double ppqPosition = 0.0;
    int    timeSigNumerator = 4;
    int    timeSigDenominator = 4;
    bool   valid = false;

    /** Seconds in one bar, or 0 when the host gave us nothing. */
    double barSeconds() const noexcept
    {
        if (! valid || bpm <= 0.0)
            return 0.0;
        return (60.0 / bpm) * timeSigNumerator * (4.0 / timeSigDenominator);
    }
};

class CaptureEngine : private juce::Thread,
                      private juce::AsyncUpdater
{
public:
    CaptureEngine();
    ~CaptureEngine() override;

    void prepare (double sampleRate, int channels);

    /** MESSAGE THREAD. Asks for the last `length` of audio out of `ring`.
        Returns false if a capture is already in flight. */
    bool requestCapture (const RollingBuffer& ring, CaptureLength length,
                         const HostTiming& timing, const params::Settings& settings);

    /** True while a CAPTURE is pending or running - the UI shows this. A
        background phrase scan does not count, because it must never make the
        interface look busy or block the user. */
    bool isBusy() const noexcept { return captureBusy.load (std::memory_order_relaxed); }

    /** MESSAGE THREAD, called when a capture completes. */
    std::function<void (CaptureClip)> onCaptureReady;

    /** MESSAGE THREAD, called when a capture could not be made. The string is
        for the footer - it says what went wrong in the user's terms. */
    std::function<void (juce::String)> onCaptureFailed;

    /** Runs phrase detection over the last `seconds` without producing a clip,
        so the PHRASE DETECTED card can stay current. Cheap enough to call on a
        timer; skipped while a capture is running. */
    bool requestPhraseScan (const RollingBuffer& ring, double seconds,
                            const HostTiming& timing, const params::Settings& settings);

    std::function<void (PhraseVerdict)> onPhraseReady;

    /** Fired with the key of the recently-buffered audio, alongside the phrase
        scan that produced it. */
    std::function<void (KeyResult)> onKeyReady;

    /** Fired with an estimated tempo, only when the host supplied none. */
    std::function<void (TempoResult)> onTempoReady;

    /** Runs any finished job's callbacks on the CALLING thread instead of
        waiting for the message loop. Only for the test harness, which has no
        message loop to pump - production always goes through AsyncUpdater. */
    void drainCompletedForTesting() { handleUpdateNowIfNeeded(); }

private:
    struct Job
    {
        juce::AudioBuffer<float> audio;
        CaptureLength length;
        HostTiming timing;
        params::Settings settings;
        double sampleRate = 48000.0;
        bool phraseScanOnly = false;
    };

    void run() override;
    void handleAsyncUpdate() override;

    /** Turns raw captured audio into a finished clip. Worker thread. */
    CaptureClip process (Job&);
    PhraseVerdict scanPhrase (Job&);

    /** Reads the last `seconds` out of the ring, retrying a lapped read. */
    static bool snapshot (const RollingBuffer&, double seconds,
                          juce::AudioBuffer<float>& dest);

    juce::CriticalSection jobLock;
    std::unique_ptr<Job> pending;

    juce::CriticalSection resultLock;
    std::vector<CaptureClip> readyClips;
    std::vector<PhraseVerdict> readyPhrases;
    std::vector<KeyResult> readyKeys;
    std::vector<TempoResult> readyTempos;
    juce::StringArray failures;

    juce::WaitableEvent work;

    // Two flags, not one. A KEEP LAST press must never lose to a background
    // phrase scan, so a capture can displace a pending scan; only another
    // capture can turn one away.
    std::atomic<bool> captureBusy { false };
    std::atomic<bool> scanBusy { false };

    double preparedRate = 48000.0;
    int preparedChannels = 2;
    int captureCounter = 0;
    int scanCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureEngine)
};

} // namespace keepthat
