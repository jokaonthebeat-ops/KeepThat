#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
class ImageStateButton : public juce::Button {
public:
    struct States { juce::Image normal, hover, pressed, disabled, selected; };
    ImageStateButton(const juce::String& name, States s) : juce::Button(name), states(std::move(s)) {}
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        const juce::Image* im = &states.normal;
        if (!isEnabled() && states.disabled.isValid()) im = &states.disabled;
        else if (getToggleState() && states.selected.isValid()) im = &states.selected;
        else if (down && states.pressed.isValid()) im = &states.pressed;
        else if (over && states.hover.isValid()) im = &states.hover;
        g.drawImage(*im, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    }
private:
    States states;
};
