/*
    KeepThatProcessor - the always-on capture engine's host side.

    Audio passes through; KEEP THAT! is a recorder, not a processor. On the
    audio thread it does five things, all allocation-free and lock-free:

        write the block into the rolling buffer
        fold it into the input meters and the scrolling trail
        mix in the preview player, if a clip is auditioning
        apply output gain and mute
        fold the result into the output meters

    Everything else - extraction, phrase detection, trimming, fades, thumbnails,
    WAV writing - happens on CaptureEngine's worker thread.
*/

#pragma once
#include <JuceHeader.h>
#include "capture/CaptureModel.h"
#include "capture/CaptureEngine.h"
#include "capture/PreviewPlayer.h"
#include "capture/ClipLoader.h"
#include "params/Parameters.h"
#include "state/SessionHistory.h"

namespace keepthat
{

class KeepThatProcessor : public juce::AudioProcessor
{
public:
    KeepThatProcessor();
    ~KeepThatProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- what the editor reads -------------------------------------------
    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts; }
    SessionState& session() noexcept              { return state; }
    const RollingBuffer& buffer() const noexcept  { return ring; }
    const MeterState& inputMeters() const noexcept  { return meters; }
    const MeterState& outputMeters() const noexcept { return outMeters; }
    WaveTrail<256>& trail() noexcept              { return liveTrail; }
    CaptureEngine& engine() noexcept              { return captureEngine; }
    PreviewPlayer& preview() noexcept             { return previewPlayer; }
    ClipLoader& loader() noexcept                 { return clipLoader; }

    /** MESSAGE THREAD. Ensures `clip` has its audio, reading it from disk in
        the background if it does not. Returns true if the audio is already
        there; false means a load is in flight and the caller should show it. */
    bool ensureAudio (CaptureClip& clip, bool urgent = true);

    /** Fired when a background clip load completes. */
    std::function<void (juce::int64 clipId)> onClipLoaded;

    /** Host tempo/PPQ as of the last block, for bar-based recovery. */
    HostTiming hostTiming() const noexcept;

    double currentBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }
    bool   hostTempoValid() const noexcept { return hostBpmValid.load (std::memory_order_relaxed); }

    /** True once any audio has arrived - the difference between "armed and
        quiet" and "nothing is routed here", which the UI must not conflate. */
    bool hasSeenAudio() const noexcept { return sawAudio.load (std::memory_order_relaxed); }

    /** MESSAGE THREAD. The KEEP LAST action. Returns false if a capture is
        already running or the buffer has nothing in it yet. */
    bool captureLast (CaptureLength);

    /** MESSAGE THREAD. Re-runs phrase detection over the recent buffer. */
    void scanForPhrase();

    /** A snapshot of every parameter. */
    params::Settings settings() const { return params::Settings::from (apvts); }

    /** Session-action history: deleting, renaming and capturing. Parameters
        are deliberately NOT in here - see state/SessionHistory.h. */
    juce::UndoManager& history() noexcept { return undoManager; }

    /** Runs `action`, recording it as one undoable step. */
    bool perform (juce::UndoableAction* action, const juce::String& name);

    /** Message-thread housekeeping, driven from the editor's clock: frees
        buffers the audio thread handed back, and refreshes the buffer clock. */
    void tickMessageThread();

    /** Fired on the message thread when a capture lands, so the editor can
        refresh the panels that show it. */
    std::function<void()> onCaptureApplied;

private:
    void applyCapture (CaptureClip);

    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undoManager { 30 * 1024 * 1024, 40 };   // 30 MB of audio, 40 steps
    SessionState state;
    RollingBuffer ring;
    MeterState meters, outMeters;
    WaveTrail<256> liveTrail;
    CaptureEngine captureEngine;
    PreviewPlayer previewPlayer;
    ClipLoader clipLoader;

    std::atomic<double> hostBpm { 120.0 };
    std::atomic<double> hostPpq { 0.0 };
    std::atomic<int> hostTimeSigNum { 4 }, hostTimeSigDen { 4 };
    std::atomic<bool> hostBpmValid { false };
    std::atomic<bool> sawAudio { false };

    // Smoothed so an output-gain move does not step the signal.
    juce::SmoothedValue<float> outputGain { 1.0f };
    double preparedRate = 48000.0;
    juce::int64 lastPhraseScanMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeepThatProcessor)
};

} // namespace keepthat
