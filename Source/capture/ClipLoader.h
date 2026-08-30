/*
    ClipLoader.h - reading a saved capture back off disk, off the message
    thread.

    Clips restored from a previous session are metadata only: a session with a
    hundred keeps must not drag a hundred audio buffers into memory on load.
    But that means selecting one has to read a WAV, and doing that on the
    message thread stalls the interface for as long as the disk takes - which
    on a cold external drive is long enough to look broken.

    So a selection posts a request here and the interface carries on. The clip
    arrives a moment later and the panels refresh. While it is in flight the
    preview says so, rather than showing an empty waveform that looks like a
    capture which failed.

    The queue also holds a small look-ahead: selecting one card usually means
    the neighbours are about to be auditioned too, and loading them while the
    user is listening costs nothing.
*/

#pragma once
#include <JuceHeader.h>
#include "CaptureModel.h"
#include "KeyDetector.h"

namespace keepthat
{

class ClipLoader : private juce::Thread,
                   private juce::AsyncUpdater
{
public:
    ClipLoader();
    ~ClipLoader() override;

    /** MESSAGE THREAD. Asks for `file` to be read. `token` comes back with the
        result so the caller can match it to whatever it was selecting - an
        index would go stale if the list changed underneath. */
    void request (juce::int64 token, const juce::File& file, bool urgent);

    /** True while `token` is still being read. */
    bool isLoading (juce::int64 token) const;

    /** Everything the worker learned about the clip while it had the audio
        in hand anyway. Computed on the LOADER thread, because a key detection
        is a few hundred FFTs and doing that on the message thread once per
        restored card is a visible stutter. */
    struct LoadedClip
    {
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        double sampleRate = 0.0;
        std::vector<float> thumbLo, thumbHi;
        float peakDb = -100.0f;
        KeyResult key;
    };

    /** MESSAGE THREAD. */
    std::function<void (juce::int64 token, LoadedClip)> onLoaded;

    /** MESSAGE THREAD, when a file could not be read. */
    std::function<void (juce::int64 token, juce::String reason)> onFailed;

    /** Drops everything not yet started - used when the session is replaced. */
    void cancelPending();

    /** Test hook: runs completed callbacks on the calling thread. */
    void drainCompletedForTesting() { handleUpdateNowIfNeeded(); }

private:
    struct Request { juce::int64 token; juce::File file; };
    struct Loaded
    {
        juce::int64 token;
        LoadedClip clip;
        juce::String failure;
    };

    void run() override;
    void handleAsyncUpdate() override;

    mutable juce::CriticalSection queueLock;
    std::deque<Request> queue;
    juce::int64 inFlight = 0;

    juce::CriticalSection resultLock;
    std::vector<Loaded> results;

    juce::WaitableEvent work;
    juce::AudioFormatManager formats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipLoader)
};

} // namespace keepthat
