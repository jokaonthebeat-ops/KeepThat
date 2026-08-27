/*
    ParameterLink.h - attaching this project's own controls to APVTS.

    JUCE's SliderAttachment / ButtonAttachment only bind to juce::Slider and
    juce::Button. None of the controls here are those - they are custom-painted
    components, because the QA criteria reject visible stock widgets - so the
    binding is done with juce::ParameterAttachment, which is the general form:
    it takes a RangedAudioParameter and a callback, and handles gesture
    begin/end, host automation and undo for us.

    Both directions matter. A host automating OUTPUT must move the knob on
    screen, and dragging the knob must write to the parameter with proper
    gestures around it, or automation recording produces nonsense.
*/

#pragma once
#include <JuceHeader.h>
#include "Widgets.h"
#include "../params/Parameters.h"

namespace keepthat
{

/** Binds a RotaryBase (which works in 0..1) to a ranged parameter. */
class KnobLink
{
public:
    KnobLink (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
              RotaryBase& knobToUse)
        : knob (knobToUse),
          parameter (dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (paramID)))
    {
        jassert (parameter != nullptr);
        if (parameter == nullptr)
            return;

        attachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float newValue)
            {
                // Parameter -> knob. Guarded, or the knob's own callback would
                // write straight back and fight the host.
                const juce::ScopedValueSetter<bool> guard (ignoreCallback, true);
                knob.setNormalised (parameter->convertTo0to1 (newValue),
                                    juce::dontSendNotification);
            },
            nullptr);

        knob.onDragStart = [this] { if (attachment) attachment->beginGesture(); };
        knob.onDragEnd   = [this] { if (attachment) attachment->endGesture(); };
        knob.onValueChange = [this] (float normalised)
        {
            if (ignoreCallback || attachment == nullptr)
                return;
            attachment->setValueAsPartOfGesture (parameter->convertFrom0to1 (normalised));
        };

        attachment->sendInitialUpdate();
    }

    /** The parameter's value in its own units, for the knob's readout. */
    float value() const
    {
        return parameter != nullptr
                 ? parameter->convertFrom0to1 (knob.getNormalised()) : 0.0f;
    }

private:
    RotaryBase& knob;
    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool ignoreCallback = false;
};

/** Binds a two-state Button (PillToggle, TileButton used as a toggle) to a
    bool parameter. */
class ToggleLink
{
public:
    ToggleLink (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                juce::Button& buttonToUse, std::function<void (bool)> onChanged = {})
        : button (buttonToUse),
          parameter (dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (paramID))),
          notify (std::move (onChanged))
    {
        jassert (parameter != nullptr);
        if (parameter == nullptr)
            return;

        attachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float newValue)
            {
                const juce::ScopedValueSetter<bool> guard (ignoreCallback, true);
                const bool on = newValue > 0.5f;
                button.setToggleState (on, juce::dontSendNotification);
                button.repaint();
                if (notify) notify (on);
            },
            nullptr);

        button.onClick = [this]
        {
            if (ignoreCallback || attachment == nullptr)
                return;
            attachment->setValueAsCompleteGesture (button.getToggleState() ? 1.0f : 0.0f);
        };

        attachment->sendInitialUpdate();
    }

    bool isOn() const { return button.getToggleState(); }

private:
    juce::Button& button;
    juce::RangedAudioParameter* parameter = nullptr;
    std::function<void (bool)> notify;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool ignoreCallback = false;
};

} // namespace keepthat
