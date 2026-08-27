/*
    PreviewPlayer.h - auditioning a recovered clip through the plugin's output.

    The hard part is handing a buffer from the message thread to the audio
    thread without either allocating on the audio thread or freeing there.

    The pattern used here: both threads share the pointer behind a SpinLock,
    but the audio thread only ever `tryEnter`s it. If the message thread
    happens to hold it at that instant the audio thread simply keeps playing
    whatever it already has for one block - it never waits. And the audio
    thread never drops the last reference to a buffer: when it takes a new
    clip it hands the old one back into `retired`, and the message thread frees
    it later. So no deallocation happens on the audio thread either.

    Playback is sample-accurate and one-shot: it plays from the trim start to
    the trim end and stops. There is no resampling - a clip always plays at the
    rate it was captured at, and if the host has since changed rate the clip is
    refused rather than played at the wrong pitch.
*/

#pragma once
#include <JuceHeader.h>

namespace keepthat
{

class PreviewPlayer
{
public:
    using Buffer = std::shared_ptr<juce::AudioBuffer<float>>;

    void prepare (double sampleRate) noexcept
    {
        hostRate = sampleRate;
        stop();
    }

    /** MESSAGE THREAD. Stages `clip` for playback between the two normalised
        positions. Takes effect on the next audio block. */
    void play (Buffer clip, double clipRate, float from, float to)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        staged = std::move (clip);
        stagedRate = clipRate;
        stagedFrom = juce::jlimit (0.0f, 1.0f, from);
        stagedTo   = juce::jlimit (stagedFrom, 1.0f, to);
        stagedValid = true;
        wantsPlay.store (true, std::memory_order_release);
    }

    /** MESSAGE THREAD. */
    void stop() noexcept { wantsPlay.store (false, std::memory_order_release); }

    /** MESSAGE THREAD, on a timer. Frees anything the audio thread handed back. */
    void collectRetired()
    {
        Buffer dead;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            dead = std::move (retired);
            retired.reset();
        }
        // `dead` releases here, on this thread, which is the whole point.
    }

    /** Normalised playhead, or -1 when not playing. Read by the UI. */
    float playheadPosition() const noexcept
    { return playhead.load (std::memory_order_relaxed); }

    bool isPlaying() const noexcept
    { return playing.load (std::memory_order_relaxed); }

    /** AUDIO THREAD. Mixes the preview into `out` at `mix` (0 = dry only).
        Never allocates, never blocks, never frees. */
    void process (juce::AudioBuffer<float>& out, float mix) noexcept
    {
        // Pick up a staged clip if the lock is free this instant. If it is
        // not, keep playing what we have and try again next block.
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (tl.isLocked() && stagedValid)
        {
            retired = std::move (current);        // message thread frees it
            current = std::move (staged);
            staged.reset();
            stagedValid = false;

            // Refuse a rate mismatch rather than pitch-shift the take.
            rateMatches = std::abs (stagedRate - hostRate) < 1.0;
            const int n = current != nullptr ? current->getNumSamples() : 0;
            position = (int) (stagedFrom * n);
            limit    = (int) (stagedTo   * n);
        }

        const bool wants = wantsPlay.load (std::memory_order_acquire);
        if (! wants || current == nullptr || ! rateMatches
            || position >= limit || mix <= 0.0001f)
        {
            playing.store (false, std::memory_order_relaxed);
            playhead.store (-1.0f, std::memory_order_relaxed);
            return;
        }

        const int channels = juce::jmin (out.getNumChannels(), current->getNumChannels());
        const int wanted = juce::jmin (out.getNumSamples(), limit - position);

        for (int ch = 0; ch < channels; ++ch)
            out.addFrom (ch, 0, *current, ch, position, wanted, mix);

        // Mono clip into a stereo bus: feed both sides rather than one.
        if (current->getNumChannels() == 1 && out.getNumChannels() > 1)
            for (int ch = 1; ch < out.getNumChannels(); ++ch)
                out.addFrom (ch, 0, *current, 0, position, wanted, mix);

        position += wanted;

        const int total = current->getNumSamples();
        playing.store (true, std::memory_order_relaxed);
        playhead.store (total > 0 ? (float) position / (float) total : -1.0f,
                        std::memory_order_relaxed);

        if (position >= limit)
            wantsPlay.store (false, std::memory_order_release);
    }

private:
    juce::SpinLock lock;

    // Audio thread owns these between blocks.
    Buffer current;
    int position = 0, limit = 0;
    bool rateMatches = true;

    // Shared under `lock`.
    Buffer staged, retired;
    double stagedRate = 0.0;
    float stagedFrom = 0.0f, stagedTo = 1.0f;
    bool stagedValid = false;

    double hostRate = 48000.0;
    std::atomic<bool> wantsPlay { false }, playing { false };
    std::atomic<float> playhead { -1.0f };
};

} // namespace keepthat
