#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
class SegmentedMeterComponent : public juce::Component, private juce::Timer {
public:
    void setTarget(float linear) noexcept { target.store(juce::jlimit(0.0f,1.0f,linear)); }
    void paint(juce::Graphics& g) override;
private:
    void timerCallback() override { displayed += (target.load()-displayed)*0.22f; repaint(); }
    std::atomic<float> target { 0.0f };
    float displayed = 0.0f;
};
