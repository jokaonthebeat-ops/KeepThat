/*
    LiveInputPanel.h - source selector, stereo LED meters, the scrolling input
    waveform and the BPM / KEY / PEAK / RMS readouts.

    The meters and the waveform are live from the first milestone: they read the
    processor's atomics, which are written on the audio thread. With nothing
    routed in they sit at the floor, which is the honest display - the panel
    says INPUT with a muted "NO SIGNAL" rather than faking movement.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Paint.h"
#include "Widgets.h"
#include "../PluginProcessor.h"
#include "../Assets.h"

namespace keepthat
{

class LiveInputPanel : public juce::Component
{
public:
    explicit LiveInputPanel (KeepThatProcessor& p) : processor (p)
    {
        addAndMakeVisible (sourceBox);
        sourceBox.setText (processor.session().sourceName);
        sourceBox.setFontToUse (Fonts::small().withHeight (13.0f));
        sourceBox.onClick = [this] { cycleSource(); };

        trailLo.resize (WaveTrail<256>::size);
        trailHi.resize (WaveTrail<256>::size);
    }

    void resized() override
    {
        sourceBox.setBounds (layout::sourceBox.translated (-getX(), -getY()));
    }

    /** Called once per animation frame from the editor's clock. */
    void update (double dt)
    {
        const auto& m = processor.inputMeters();
        auto smooth = [dt] (float& shown, float target, float riseMs, float fallMs)
        {
            const float k = (float) (1.0 - std::exp (-dt * 1000.0
                                    / (target > shown ? riseMs : fallMs)));
            shown += (target - shown) * k;
        };

        smooth (levelL, paint::gainToNorm (m.peakL.load (std::memory_order_relaxed)), 22.0f, 320.0f);
        smooth (levelR, paint::gainToNorm (m.peakR.load (std::memory_order_relaxed)), 22.0f, 320.0f);
        holdL = paint::gainToNorm (m.holdL.load (std::memory_order_relaxed));
        holdR = paint::gainToNorm (m.holdR.load (std::memory_order_relaxed));

        processor.trail().readOrdered (trailLo.data(), trailHi.data());

        auto& s = processor.session();
        if (processor.hasSeenAudio())
        {
            const float pk = juce::jmax (m.peakL.load (std::memory_order_relaxed),
                                         m.peakR.load (std::memory_order_relaxed));
            const float rms = 0.5f * (m.rmsL.load (std::memory_order_relaxed)
                                    + m.rmsR.load (std::memory_order_relaxed));
            s.peakDb = juce::Decibels::gainToDecibels (pk, -96.0f);
            s.rmsDb  = juce::Decibels::gainToDecibels (rms, -96.0f);
            s.inputQuality = s.peakDb > -0.2f ? "HOT" : (s.peakDb > -34.0f ? "GOOD" : "LOW");
        }
        if (processor.hostTempoValid())
            s.bpm = processor.currentBpm();

        // One repaint covering everything that moves. Splitting the readouts
        // out as a "slow" region made this WORSE, not better: PEAK and RMS
        // change on every frame anyway, so the second region was always dirty
        // too and the panel simply issued two repaints instead of one.
        repaint (liveRegion());
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = juce::Point<int> (-getX(), -getY());
        auto& s = processor.session();

        paint::panelSurface (g, getLocalBounds().toFloat(), Design::corner);

        widgets::panelTitle (g, "LIVE INPUT", layout::liveTitle.translated (off.x, off.y));
        paintArmedBadge (g, layout::alwaysListening.translated (off.x, off.y), s.armed);

        g.setFont (Fonts::fieldLabel());
        g.setColour (tokens::textMuted);
        g.drawText ("SOURCE", layout::sourceLabel.translated (off.x, off.y),
                    juce::Justification::centredLeft, false);

        paintMeters (g, layout::meterPair.translated (off.x, off.y),
                     layout::meterScale.translated (off.x, off.y));
        paintWaveform (g, layout::inputWave.translated (off.x, off.y).toFloat());
        paintReadouts (g, layout::readoutGrid.translated (off.x, off.y).toFloat(), s);
        paintFooterCells (g, layout::modeCell.translated (off.x, off.y).toFloat(),
                          layout::inputCell.translated (off.x, off.y).toFloat(), s);
    }

private:
    /** The part of this panel that changes every frame, in local coordinates. */
    juce::Rectangle<int> liveRegion() const
    {
        return layout::meterScale.getUnion (layout::inputCell)
                                 .getUnion (layout::readoutGrid)
                                 .translated (-getX(), -getY())
                                 .expanded (6);
    }

    void cycleSource()
    {
        static const char* sources[] = { "Input 1 (Audio Interface)", "Input 2 (Audio Interface)",
                                         "Stereo Bus", "Sidechain Input" };
        sourceIndex = (sourceIndex + 1) % (int) std::size (sources);
        processor.session().sourceName = sources[sourceIndex];
        sourceBox.setText (processor.session().sourceName);
    }

    void paintArmedBadge (juce::Graphics& g, juce::Rectangle<int> r, bool armed)
    {
        const auto colour = armed ? tokens::accentRed : tokens::textMuted;
        auto dot = juce::Rectangle<float> (7.0f, 7.0f)
                       .withCentre ({ (float) r.getX() + 4.0f, (float) r.getCentreY() });
        if (armed)
            paint::halo (g, dot.getCentre(), 9.0f, colour, 0.55f);
        g.setColour (colour);
        g.fillEllipse (dot);

        g.setFont (Fonts::small().withHeight (13.0f));
        g.setColour (armed ? tokens::accentRed : tokens::textMuted);
        g.drawText (armed ? "Always Listening" : "Standby", r.withTrimmedLeft (14),
                    juce::Justification::centredLeft, false);
    }

    void paintMeters (juce::Graphics& g, juce::Rectangle<int> pair, juce::Rectangle<int> scale)
    {
        auto r = pair.toFloat();

        // dB scale down the left.
        static const int marks[] = { 0, -6, -12, -18, -24, -36, -48, -60 };
        g.setFont (Fonts::tiny());
        g.setColour (tokens::textMuted);
        for (int db : marks)
        {
            const float t = 1.0f - paint::dbToNorm ((float) db);
            const int y = (int) std::round (r.getY() + t * r.getHeight());
            g.drawText (juce::String (db), scale.getX(), y - 7, scale.getWidth(), 14,
                        juce::Justification::centredRight, false);
        }

        // Channel captions.
        g.setFont (Fonts::tiny());
        g.setColour (tokens::textSecond);
        g.drawText ("L", (int) r.getX(), (int) r.getY() - 15, (int) (r.getWidth() * 0.5f), 13,
                    juce::Justification::centred, false);
        g.drawText ("R", (int) r.getCentreX(), (int) r.getY() - 15, (int) (r.getWidth() * 0.5f), 13,
                    juce::Justification::centred, false);

        const float colW = (r.getWidth() - 8.0f) * 0.5f;
        auto left  = juce::Rectangle<float> (r.getX(), r.getY(), colW, r.getHeight());
        auto right = left.withX (r.getRight() - colW);

        for (auto* col : { &left, &right })
        {
            paint::wellSurface (g, col->expanded (3.0f, 3.0f), 3.0f, juce::Colour (0xff05080c));
        }
        widgets::ledColumn (g, left,  levelL, holdL);
        widgets::ledColumn (g, right, levelR, holdR);
    }

    void paintWaveform (juce::Graphics& g, juce::Rectangle<float> r)
    {
        paint::wellSurface (g, r, 5.0f);
        paint::wellGrid (g, r.reduced (1.0f), 8, 4, tokens::accentCyan.withAlpha (0.055f));

        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        paint::waveform (g, r.reduced (4.0f, 6.0f), trailLo.data(), trailHi.data(),
                         (int) trailLo.size(), tokens::accentCyan, 1.0f, true);
        g.restoreState();

        if (! processor.hasSeenAudio())
        {
            g.setFont (Fonts::small());
            g.setColour (tokens::textMuted.withAlpha (0.75f));
            g.drawText ("NO SIGNAL", r.toNearestInt(), juce::Justification::centred, false);
        }
    }

    void paintReadouts (juce::Graphics& g, juce::Rectangle<float> r, const SessionState& s)
    {
        const float gap = 6.0f;
        const float cw = (r.getWidth() - gap) * 0.5f;
        const float ch = (r.getHeight() - gap) * 0.5f;

        auto cellAt = [&] (int col, int row)
        {
            return juce::Rectangle<float> (r.getX() + col * (cw + gap),
                                           r.getY() + row * (ch + gap), cw, ch);
        };

        widgets::readout (g, cellAt (0, 0), "BPM",
                          s.bpm > 0.0 ? juce::String (s.bpm, 1) : juce::String ("--"));
        widgets::readout (g, cellAt (1, 0), "KEY", s.key);
        widgets::readout (g, cellAt (0, 1), "PEAK",
                          s.peakDb <= -96.0f ? juce::String ("--")
                                             : juce::String (s.peakDb, 1) + " dB");
        widgets::readout (g, cellAt (1, 1), "RMS",
                          s.rmsDb <= -96.0f ? juce::String ("--")
                                            : juce::String (s.rmsDb, 1) + " dB");
    }

    void paintFooterCells (juce::Graphics& g, juce::Rectangle<float> mode,
                           juce::Rectangle<float> input, const SessionState& s)
    {
        paint::wellSurface (g, mode, 5.0f, tokens::panel2);
        auto m = mode.reduced (10.0f, 5.0f);
        g.setFont (Fonts::fieldLabel());
        g.setColour (tokens::textMuted);
        g.drawText ("MODE", m.removeFromTop (14.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);
        g.setFont (Fonts::make (17.0f, true));
        g.setColour (tokens::accentCyan);
        g.drawText (s.stereo ? "STEREO" : "MONO", m.toNearestInt(),
                    juce::Justification::centredLeft, false);

        // The blank inset square between the two cells in the approved art.
        auto chip = juce::Rectangle<float> (34.0f, 34.0f)
                        .withCentre ({ mode.getRight() - 30.0f, mode.getCentreY() });
        paint::wellSurface (g, chip, 4.0f, juce::Colour (0xff0b1017));

        paint::wellSurface (g, input, 5.0f, tokens::panel2);
        auto i = input.reduced (10.0f, 5.0f);
        g.setFont (Fonts::fieldLabel());
        g.setColour (tokens::textMuted);
        g.drawText ("INPUT", i.removeFromTop (14.0f).toNearestInt(),
                    juce::Justification::centredRight, false);
        const auto quality = s.inputQuality;
        g.setFont (Fonts::make (17.0f, true));
        g.setColour (quality == "GOOD" ? tokens::green
                   : quality == "HOT"  ? tokens::accentRed
                   : quality == "--"   ? tokens::textMuted : tokens::accentGold);
        g.drawText (quality, i.toNearestInt(), juce::Justification::centredRight, false);
    }

    KeepThatProcessor& processor;
    ComboField sourceBox { "Source" };
    int sourceIndex = 0;

    float levelL = 0.0f, levelR = 0.0f, holdL = 0.0f, holdR = 0.0f;
    std::vector<float> trailLo, trailHi;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveInputPanel)
};

} // namespace keepthat
