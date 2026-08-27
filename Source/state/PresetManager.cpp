#include "PresetManager.h"
#include "../params/Parameters.h"

namespace keepthat
{

namespace
{
    constexpr const char* extension = ".keepthat";

    /** The factory set. These are the presets the approved interface names,
        with parameter values that actually suit each job rather than the same
        defaults under five titles. */
    struct Factory
    {
        const char* name;
        float bufferMinutes, sensitivity, autoTrim, previewMix, fadeMs;
        bool normalize, silenceDetect;
        int  lengthIndex;                 // index into captureLengths()
    };

    const Factory factory[] = {
        // Long buffer, moderate trim: the default working state.
        { "Hook Recovery Session", 8.0f, 0.72f, 0.85f, 0.50f, 10.0f, false, true,  2 },
        // Vocals: gentle trim so breaths survive, longer fades, normalised.
        { "Vocal Catch",           8.0f, 0.60f, 0.45f, 0.60f, 25.0f, true,  true,  5 },
        // Beat switches are short and loud - tight trim, minimal fade.
        { "Beat Switch Rescue",    4.0f, 0.80f, 0.95f, 0.50f,  4.0f, false, true,  1 },
        // A long take you might want minutes back from; trim off, no normalise.
        { "Long Take Safety",      8.0f, 0.50f, 0.10f, 0.50f, 15.0f, false, false, 6 },
        // A live room is noisy, so the gate has to sit higher.
        { "Live Room Capture",     6.0f, 0.90f, 0.70f, 0.40f, 20.0f, false, true,  3 },
    };
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& apvts, SessionState& state)
    : parameters (apvts), session (state)
{
    ensureFactoryPresets();
    rescan();
}

juce::File PresetManager::presetsFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Diamond Loopz")
                   .getChildFile ("KEEP THAT!")
                   .getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

void PresetManager::rescan()
{
    presetNames.clear();
    for (const auto& entry : juce::RangedDirectoryIterator (presetsFolder(), false,
                                                            juce::String ("*") + extension))
        presetNames.add (entry.getFile().getFileNameWithoutExtension());

    presetNames.sort (true);
    current = presetNames.indexOf (session.presetName);
}

void PresetManager::ensureFactoryPresets()
{
    auto folder = presetsFolder();
    if (folder.getNumberOfChildFiles (juce::File::findFiles,
                                      juce::String ("*") + extension) > 0)
        return;

    using namespace params;
    for (const auto& f : factory)
    {
        juce::ValueTree tree ("KEEPTHATPRESET");
        tree.setProperty ("version", stateVersion, nullptr);

        juce::ValueTree p ("PARAMETERS");
        p.setProperty (id::bufferLengthMinutes, f.bufferMinutes, nullptr);
        p.setProperty (id::sensitivity,         f.sensitivity,   nullptr);
        p.setProperty (id::autoTrimAmount,      f.autoTrim,      nullptr);
        p.setProperty (id::previewMix,          f.previewMix,    nullptr);
        p.setProperty (id::fadeMilliseconds,    f.fadeMs,        nullptr);
        p.setProperty (id::outputGainDb,        0.0f,            nullptr);
        p.setProperty (id::normalizeEnabled,    f.normalize,     nullptr);
        p.setProperty (id::normalizeTargetDb,   -1.0f,           nullptr);
        p.setProperty (id::zeroCrossingEnabled, true,            nullptr);
        p.setProperty (id::silenceDetectEnabled,f.silenceDetect, nullptr);
        p.setProperty (id::dragExportEnabled,   true,            nullptr);
        p.setProperty (id::autoTrimEnabled,     f.autoTrim > 0.15f, nullptr);
        p.setProperty (id::fadeEnabled,         f.fadeMs > 0.0f, nullptr);
        p.setProperty (id::mute,                false,           nullptr);
        tree.appendChild (p, nullptr);

        juce::ValueTree ui ("UI");
        ui.setProperty ("selectedLength", juce::jmin (3, f.lengthIndex), nullptr);
        ui.setProperty ("selectedSeconds", juce::jmax (4, f.lengthIndex), nullptr);
        ui.setProperty ("showBarsBeats", true, nullptr);
        tree.appendChild (ui, nullptr);

        if (auto xml = std::unique_ptr<juce::XmlElement> (tree.createXml()))
            xml->writeTo (presetsFolder().getChildFile (juce::String (f.name) + extension));
    }
}

juce::ValueTree PresetManager::captureState() const
{
    juce::ValueTree tree ("KEEPTHATPRESET");
    tree.setProperty ("version", params::stateVersion, nullptr);

    juce::ValueTree p ("PARAMETERS");
    for (auto* parameter : parameters.processor.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            p.setProperty (juce::Identifier (withId->paramID),
                           withId->convertFrom0to1 (withId->getValue()), nullptr);
    tree.appendChild (p, nullptr);

    juce::ValueTree ui ("UI");
    ui.setProperty ("selectedLength", session.selectedLength, nullptr);
    ui.setProperty ("selectedSeconds", session.selectedSeconds, nullptr);
    ui.setProperty ("showBarsBeats", session.showBarsBeats, nullptr);
    ui.setProperty ("destination", (int) session.destination, nullptr);
    ui.setProperty ("source", session.sourceName, nullptr);
    for (int i = 0; i < 5; ++i)
        if (session.destinationFolder[i] != juce::File())
            ui.setProperty ("destFolder" + juce::String (i),
                            session.destinationFolder[i].getFullPathName(), nullptr);
    tree.appendChild (ui, nullptr);
    return tree;
}

void PresetManager::applyState (const juce::ValueTree& tree)
{
    auto p = tree.getChildWithName ("PARAMETERS");
    if (p.isValid())
    {
        for (int i = 0; i < p.getNumProperties(); ++i)
        {
            const auto name = p.getPropertyName (i);
            if (auto* parameter = parameters.getParameter (name.toString()))
            {
                const float value = (float) p.getProperty (name);
                // setValueNotifyingHost keeps the host and the UI in step; the
                // parameter attachments then move the controls.
                parameter->setValueNotifyingHost (
                    dynamic_cast<juce::RangedAudioParameter*> (parameter)->convertTo0to1 (value));
            }
        }
    }

    auto ui = tree.getChildWithName ("UI");
    if (! ui.isValid())
        return;

    session.selectedLength  = ui.getProperty ("selectedLength", session.selectedLength);
    session.selectedSeconds = ui.getProperty ("selectedSeconds", session.selectedSeconds);
    session.showBarsBeats   = ui.getProperty ("showBarsBeats", session.showBarsBeats);
    if (ui.hasProperty ("destination"))
        session.destination = (Destination) (int) ui.getProperty ("destination");
    if (ui.hasProperty ("source"))
        session.sourceName = ui.getProperty ("source").toString();

    for (int i = 0; i < 5; ++i)
    {
        const auto path = ui.getProperty ("destFolder" + juce::String (i)).toString();
        if (path.isNotEmpty())
            session.destinationFolder[i] = juce::File (path);
    }
}

juce::File PresetManager::save (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return {};

    auto file = presetsFolder().getChildFile (clean + extension);
    auto xml = std::unique_ptr<juce::XmlElement> (captureState().createXml());
    if (xml == nullptr || ! xml->writeTo (file))
        return {};

    session.presetName = clean;
    rescan();
    return file;
}

bool PresetManager::load (const juce::String& name)
{
    auto file = presetsFolder().getChildFile (name + extension);
    if (! file.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;

    applyState (juce::ValueTree::fromXml (*xml));
    session.presetName = name;
    current = presetNames.indexOf (name);
    return true;
}

bool PresetManager::step (int delta)
{
    if (presetNames.isEmpty())
        return false;

    // An unsaved state has no index, so the first step lands on the first
    // preset rather than jumping to whatever index -1 + 1 happens to be.
    const int from = current >= 0 ? current : (delta > 0 ? -1 : 0);
    const int next = (from + delta + presetNames.size()) % presetNames.size();
    return load (presetNames[next]);
}

} // namespace keepthat
