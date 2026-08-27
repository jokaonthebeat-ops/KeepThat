/*
    BottomControlStrip.h - the six macro knobs, the stereo output meters and
    MUTE.

    Five knobs sit in the left group under hairline dividers; OUTPUT lives in
    its own group on the right with the meters, exactly as the approved art
    separates them. Every knob's value formatting lives with the knob, so the
    units are declared once.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Paint.h"
#include "Widgets.h"
#include "../PluginProcessor.h"
#include "../Assets.h"
#include "ParameterLink.h"

namespace keepthat
{

class BottomControlStrip : public juce::Component
{
public:
    explicit BottomControlStrip (KeepThatProcessor& p) : processor (p)
    {
        auto add = [this] (MacroKnob* k) { knobs.add (k); addAndMakeVisible (k); return k; };
        auto& apvts = processor.parameters();

        bufferLength = add (new MacroKnob ("BUFFER LENGTH", "1:00", "8:00"));
        bufferLength->setFilmstrip (art::knobGold);          // gold = time values
        bufferLength->setValueTint (tokens::accentGold);
        bufferLength->formatValue = [] (float v)
        { return juce::String (juce::roundToInt (1.0f + v * 7.0f)) + ":00"; };

        sensitivity = add (new MacroKnob ("SENSITIVITY", "0%", "100%"));
        sensitivity->setFilmstrip (art::knobMacro);
        sensitivity->formatValue = percent;

        autoTrim = add (new MacroKnob ("AUTO TRIM", "0%", "100%"));
        autoTrim->setFilmstrip (art::knobMacro);
        autoTrim->formatValue = percent;

        previewMix = add (new MacroKnob ("PREVIEW MIX", "DRY", "WET"));
        previewMix->setFilmstrip (art::knobMacro);
        previewMix->setShowValue (false);

        fade = add (new MacroKnob ("FADE", "0", "200 ms"));
        fade->setFilmstrip (art::knobGold);
        fade->setValueTint (tokens::accentGold);
        fade->formatValue = [] (float v)
        { return juce::String (juce::roundToInt (v * 200.0f)) + " ms"; };

        output = add (new MacroKnob ("OUTPUT", "-inf", "+12"));
        output->setFilmstrip (art::knobMacro);
        output->setValueInFooter (true);
        output->formatValue = [] (float v)
        {
            const float db = -60.0f + v * 72.0f;
            return db <= -59.5f ? juce::String ("-inf") : juce::String (db, 1) + " dB";
        };

        // Every knob is a real parameter, so a host can automate it and it
        // saves with the session. See params/Parameters.h.
        using namespace params;
        links.add (new KnobLink (apvts, id::bufferLengthMinutes, *bufferLength));
        links.add (new KnobLink (apvts, id::sensitivity,         *sensitivity));
        links.add (new KnobLink (apvts, id::autoTrimAmount,      *autoTrim));
        links.add (new KnobLink (apvts, id::previewMix,          *previewMix));
        links.add (new KnobLink (apvts, id::fadeMilliseconds,    *fade));
        links.add (new KnobLink (apvts, id::outputGainDb,        *output));

        addAndMakeVisible (mute);
        mute.setAssetBase ("blank");       // same plate as PLAY / STOP
        mute.setCaptionInside (true);      // filled plate, "MUTE" on it
        mute.setClickingTogglesState (true);
        muteLink = std::make_unique<ToggleLink> (apvts, id::mute, mute,
                                                 [this] (bool on) { mute.setSelected (on); });
    }

    void resized() override
    {
        const auto off = juce::Point<int> (-getX(), -getY());
        auto group = layout::macroGroup.translated (off.x, off.y);
        const float cellW = group.getWidth() / (float) layout::macroCells;

        for (int i = 0; i < layout::macroCells; ++i)
            knobs[i]->setBounds (juce::Rectangle<int> (
                group.getX() + (int) std::round (i * cellW), group.getY() + 9,
                (int) std::round (cellW), group.getHeight() - 13));

        output->setBounds (layout::outputKnobCell.translated (off.x, off.y));
        mute.setBounds (layout::muteButton.translated (off.x, off.y));
    }

    void update (double dt)
    {
        const auto& m = processor.outputMeters();
        auto smooth = [dt] (float& shown, float target)
        {
            const float k = (float) (1.0 - std::exp (-dt * 1000.0
                                    / (target > shown ? 20.0 : 300.0)));
            shown += (target - shown) * k;
        };
        const float mute = 1.0f;   // the output meter reads the post-mute signal
        smooth (outL, paint::gainToNorm (m.peakL.load (std::memory_order_relaxed)) * mute);
        smooth (outR, paint::gainToNorm (m.peakR.load (std::memory_order_relaxed)) * mute);
        holdOutL = paint::gainToNorm (m.holdL.load (std::memory_order_relaxed)) * mute;
        holdOutR = paint::gainToNorm (m.holdR.load (std::memory_order_relaxed)) * mute;

        // The knobs are child components and repaint themselves when a value
        // changes; only the output meters move every frame.
        repaint (layout::outputMeters.translated (-getX(), -getY()).expanded (4));
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = juce::Point<int> (-getX(), -getY());

        auto macro = layout::macroGroup.translated (off.x, off.y).toFloat();
        auto out   = layout::outputGroup.translated (off.x, off.y).toFloat();
        paint::panelSurface (g, macro, Design::corner);
        paint::panelSurface (g, out,   Design::corner);

        // Hairline dividers between the macro cells.
        const float cellW = macro.getWidth() / (float) layout::macroCells;
        for (int i = 1; i < layout::macroCells; ++i)
        {
            const float x = macro.getX() + i * cellW;
            juce::ColourGradient rule (tokens::stroke.withAlpha (0.0f), x, macro.getY(),
                                       tokens::stroke.withAlpha (0.0f), x, macro.getBottom(),
                                       false);
            rule.addColour (0.5, tokens::stroke.brighter (0.15f));
            g.setGradientFill (rule);
            g.fillRect (x, macro.getY() + 8.0f, 1.0f, macro.getHeight() - 16.0f);
        }

        paintMeters (g, layout::outputMeters.translated (off.x, off.y).toFloat());
    }

private:
    static juce::String percent (float v)
    {
        return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    }

    void paintMeters (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto labels = r.removeFromLeft (18.0f);
        auto scaleRow = r.removeFromBottom (14.0f);
        const float rowH = (r.getHeight() - 6.0f) * 0.5f;

        auto rowL = juce::Rectangle<float> (r.getX(), r.getY(), r.getWidth(), rowH);
        auto rowR = rowL.withY (r.getY() + rowH + 6.0f);

        g.setFont (Fonts::tiny());
        g.setColour (tokens::textSecond);
        g.drawText ("L", labels.withY (rowL.getY()).withHeight (rowH).toNearestInt(),
                    juce::Justification::centred, false);
        g.drawText ("R", labels.withY (rowR.getY()).withHeight (rowH).toNearestInt(),
                    juce::Justification::centred, false);

        for (auto* row : { &rowL, &rowR })
            paint::wellSurface (g, row->expanded (2.0f, 2.0f), 3.0f, juce::Colour (0xff05080c));

        widgets::ledRow (g, rowL, outL, holdOutL);
        widgets::ledRow (g, rowR, outR, holdOutR);

        static const int marks[] = { -60, -48, -36, -24, -12, -6, -3, 0 };
        g.setFont (Fonts::make (9.0f, true));
        g.setColour (tokens::textMuted);
        for (int db : marks)
        {
            const float t = paint::dbToNorm ((float) db);
            g.drawText (juce::String (db),
                        juce::Rectangle<float> (r.getX() + t * r.getWidth() - 14.0f,
                                                scaleRow.getY(), 28.0f, scaleRow.getHeight())
                            .toNearestInt(),
                        juce::Justification::centred, false);
        }
    }

    KeepThatProcessor& processor;
    juce::OwnedArray<MacroKnob> knobs;
    juce::OwnedArray<KnobLink> links;
    std::unique_ptr<ToggleLink> muteLink;
    MacroKnob* bufferLength = nullptr;
    MacroKnob* sensitivity = nullptr;
    MacroKnob* autoTrim = nullptr;
    MacroKnob* previewMix = nullptr;
    MacroKnob* fade = nullptr;
    MacroKnob* output = nullptr;
    TileButton mute { "Mute", juce::Path(), "MUTE" };

    float outL = 0.0f, outR = 0.0f, holdOutL = 0.0f, holdOutR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomControlStrip)
};

} // namespace keepthat
