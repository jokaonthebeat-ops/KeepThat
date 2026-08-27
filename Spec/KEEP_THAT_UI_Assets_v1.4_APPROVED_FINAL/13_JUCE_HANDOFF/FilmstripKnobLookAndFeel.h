#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
class FilmstripKnobLookAndFeel : public juce::LookAndFeel_V4 {
public:
    FilmstripKnobLookAndFeel(juce::Image strip, int frameCount)
        : filmstrip(std::move(strip)), frames(frameCount), frameHeight(filmstrip.getHeight()/frameCount) {}
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float, float, juce::Slider&) override {
        const int index = juce::jlimit(0, frames-1, juce::roundToInt(sliderPos*(frames-1)));
        const auto source = juce::Rectangle<int>(0, index*frameHeight, filmstrip.getWidth(), frameHeight);
        g.drawImage(filmstrip, juce::Rectangle<float>((float)x,(float)y,(float)w,(float)h), source.toFloat(), false);
    }
private:
    juce::Image filmstrip;
    int frames = 0;
    int frameHeight = 0;
};
