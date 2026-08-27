/*
    PresetManager.h - what the header's preset bar and SAVE button actually do.

    A preset stores the twelve parameters plus the interface choices that go
    with them - the selected capture length, the export destination and the
    folder it points at, the ruler mode, the input source.

    It deliberately does NOT store captures. The recent-keeps list is a record
    of what the user played, not a setting, and loading "Vocal Catch" should
    not replace the takes they are working with.

    Presets are plain XML on disk so they can be shared, backed up and
    inspected. The factory set is written out on first run if the folder is
    empty, so the preset arrows are never browsing nothing.
*/

#pragma once
#include <JuceHeader.h>
#include "../capture/CaptureModel.h"

namespace keepthat
{

class PresetManager
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& apvts, SessionState& state);

    static juce::File presetsFolder();

    /** Presets on disk, sorted by name. Refreshed by `rescan`. */
    const juce::StringArray& names() const noexcept { return presetNames; }
    void rescan();

    /** Index of the loaded preset in `names`, or -1 when the current settings
        do not correspond to a saved one. */
    int currentIndex() const noexcept { return current; }

    /** Saves the current settings under `name`, overwriting if it exists.
        Returns the file, or an empty File on failure. */
    juce::File save (const juce::String& name);

    bool load (const juce::String& name);

    /** Steps to the next or previous preset and loads it. */
    bool step (int delta);

    /** Writes the factory presets if the folder is empty. */
    void ensureFactoryPresets();

private:
    juce::ValueTree captureState() const;
    void applyState (const juce::ValueTree&);

    juce::AudioProcessorValueTreeState& parameters;
    SessionState& session;
    juce::StringArray presetNames;
    int current = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace keepthat
