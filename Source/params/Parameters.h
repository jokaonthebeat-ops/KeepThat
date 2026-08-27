/*
    Parameters.h - the APVTS layout.

    Exactly the twelve parameters in 13_JUCE_HANDOFF/APVTS_PARAMETER_PLAN.md,
    with the ranges and defaults it specifies. These are the user-facing
    controls, so they are real parameters: a host can automate them and they
    save with the session for free.

    Everything else the interface holds - the preset name, the recent-keeps
    list, the current capture, the trim handles - is NOT a parameter. It lives
    in SessionState and is persisted separately in a versioned ValueTree,
    because none of it is a continuous value a host should be automating.
*/

#pragma once
#include <JuceHeader.h>

namespace keepthat::params
{

// Parameter IDs. String literals appear once, here.
namespace id
{
    inline constexpr const char* bufferLengthMinutes = "bufferLengthMinutes";
    inline constexpr const char* sensitivity         = "sensitivity";
    inline constexpr const char* autoTrimAmount      = "autoTrimAmount";
    inline constexpr const char* previewMix          = "previewMix";
    inline constexpr const char* fadeMilliseconds    = "fadeMilliseconds";
    inline constexpr const char* outputGainDb        = "outputGainDb";
    inline constexpr const char* normalizeEnabled    = "normalizeEnabled";
    inline constexpr const char* normalizeTargetDb   = "normalizeTargetDb";
    inline constexpr const char* zeroCrossingEnabled = "zeroCrossingEnabled";
    inline constexpr const char* silenceDetectEnabled= "silenceDetectEnabled";
    inline constexpr const char* dragExportEnabled   = "dragExportEnabled";
    inline constexpr const char* mute                = "mute";
    inline constexpr const char* autoTrimEnabled     = "autoTrimEnabled";
    inline constexpr const char* fadeEnabled         = "fadeEnabled";
}

/** Version hint for the APVTS state, so a future layout change can migrate. */
inline constexpr int stateVersion = 1;

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    constexpr int v = 1;               // ParameterID version hint

    auto seconds = [] (float value, int)
    {
        const int total = (int) std::round (value * 60.0f);
        return String (total / 60) + ":" + String (total % 60).paddedLeft ('0', 2);
    };
    auto percent = [] (float value, int)
    { return String (roundToInt (value * 100.0f)) + " %"; };
    auto millis  = [] (float value, int)
    { return String (roundToInt (value)) + " ms"; };
    auto decibels = [] (float value, int)
    { return value <= -59.95f ? String ("-inf") : String (value, 1) + " dB"; };

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::bufferLengthMinutes, v }, "Buffer Length",
        NormalisableRange<float> (1.0f, 8.0f, 0.5f), 8.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (seconds)
                                       .withLabel ("min")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::sensitivity, v }, "Sensitivity",
        NormalisableRange<float> (0.0f, 1.0f), 0.72f,
        AudioParameterFloatAttributes().withStringFromValueFunction (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::autoTrimAmount, v }, "Auto Trim",
        NormalisableRange<float> (0.0f, 1.0f), 0.85f,
        AudioParameterFloatAttributes().withStringFromValueFunction (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::previewMix, v }, "Preview Mix",
        NormalisableRange<float> (0.0f, 1.0f), 0.50f,
        AudioParameterFloatAttributes().withStringFromValueFunction (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::fadeMilliseconds, v }, "Fade",
        NormalisableRange<float> (0.0f, 200.0f, 1.0f), 10.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (millis)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::outputGainDb, v }, "Output",
        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (decibels)));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::normalizeEnabled, v }, "Normalize", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { id::normalizeTargetDb, v }, "Normalize Target",
        NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (decibels)));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::zeroCrossingEnabled, v }, "Zero Crossing", true));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::silenceDetectEnabled, v }, "Silence Detect", true));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::dragExportEnabled, v }, "Drag Export", true));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::mute, v }, "Mute", false));

    // Not in the plan's list, but the interface has switches for both and they
    // have to persist and automate like the rest of the recovery tools.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::autoTrimEnabled, v }, "Auto Trim Enabled", true));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { id::fadeEnabled, v }, "Fade Enabled", true));

    return layout;
}

/** A snapshot of every parameter, taken on the message thread and handed to a
    worker so the render never reads atomics mid-job. */
struct Settings
{
    float bufferLengthMinutes = 8.0f;
    float sensitivity = 0.72f;
    float autoTrimAmount = 0.85f;
    float previewMix = 0.5f;
    float fadeMilliseconds = 10.0f;
    float outputGainDb = 0.0f;
    float normalizeTargetDb = -1.0f;
    bool  normalizeEnabled = false;
    bool  zeroCrossingEnabled = true;
    bool  silenceDetectEnabled = true;
    bool  dragExportEnabled = true;
    bool  autoTrimEnabled = true;
    bool  fadeEnabled = true;
    bool  mute = false;

    static Settings from (const juce::AudioProcessorValueTreeState& apvts)
    {
        auto f = [&apvts] (const char* i)
        { return apvts.getRawParameterValue (i)->load (std::memory_order_relaxed); };

        Settings s;
        s.bufferLengthMinutes  = f (id::bufferLengthMinutes);
        s.sensitivity          = f (id::sensitivity);
        s.autoTrimAmount       = f (id::autoTrimAmount);
        s.previewMix           = f (id::previewMix);
        s.fadeMilliseconds     = f (id::fadeMilliseconds);
        s.outputGainDb         = f (id::outputGainDb);
        s.normalizeTargetDb    = f (id::normalizeTargetDb);
        s.normalizeEnabled     = f (id::normalizeEnabled)     > 0.5f;
        s.zeroCrossingEnabled  = f (id::zeroCrossingEnabled)  > 0.5f;
        s.silenceDetectEnabled = f (id::silenceDetectEnabled) > 0.5f;
        s.dragExportEnabled    = f (id::dragExportEnabled)    > 0.5f;
        s.autoTrimEnabled      = f (id::autoTrimEnabled)      > 0.5f;
        s.fadeEnabled          = f (id::fadeEnabled)          > 0.5f;
        s.mute                 = f (id::mute)                 > 0.5f;
        return s;
    }
};

} // namespace keepthat::params
