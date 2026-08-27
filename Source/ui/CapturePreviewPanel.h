/*
    CapturePreviewPanel.h - the capture timeline, its draggable trim handles and
    the two rulers beneath it, plus the transport column on the left and the
    action column on the right.

    The trim handles are real: drag either one and the highlighted range, the
    header's range text and the bar ruler all follow. They clamp against each
    other with a minimum gap so a capture can never be trimmed to nothing.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Paint.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace keepthat
{

class CapturePreviewPanel : public juce::Component
{
public:
    std::function<void (float)> onSeek;

    explicit CapturePreviewPanel (KeepThatProcessor& p) : processor (p)
    {
        addAndMakeVisible (modeBox);
        modeBox.setText (processor.session().showBarsBeats ? "BARS:BEATS" : "TIME");
        modeBox.onClick = [this]
        {
            auto& s = processor.session();
            s.showBarsBeats = ! s.showBarsBeats;
            modeBox.setText (s.showBarsBeats ? "BARS:BEATS" : "TIME");
            repaint();
        };
    }

    void resized() override
    {
        modeBox.setBounds (layout::previewModeBox.translated (-getX(), -getY()));
    }

    void update (double dt)
    {
        // The playhead is driven by the preview player now, not by the clock -
        // the editor copies it across each frame. Nothing to advance here.
        juce::ignoreUnused (dt);
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = juce::Point<int> (-getX(), -getY());
        auto& s = processor.session();

        paint::panelSurface (g, getLocalBounds().toFloat(), Design::corner);

        widgets::panelTitle (g, "CAPTURE PREVIEW", layout::previewTitle.translated (off.x, off.y),
                             tokens::accentCyan);

        g.setFont (Fonts::small());
        g.setColour (tokens::textSecond);
        g.drawText (rangeText (s), layout::previewRange.translated (off.x, off.y),
                    juce::Justification::centredLeft, false);

        g.setFont (Fonts::fieldLabel());
        g.setColour (tokens::textMuted);
        g.drawText ("TIME", layout::previewModeLbl.translated (off.x, off.y),
                    juce::Justification::centredRight, false);

        paintWave (g, waveBounds(), s);
        paintTimeRuler (g, layout::previewTimeRule.translated (off.x, off.y).toFloat(), s);
        paintBarRuler  (g, layout::previewBarRule.translated (off.x, off.y).toFloat(), s);
    }

    // --- trim handle interaction ------------------------------------------
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = handleAt (e.position);
        if (h != hovered)
        {
            hovered = h;
            setMouseCursor (h == Handle::none ? juce::MouseCursor::NormalCursor
                                              : juce::MouseCursor::LeftRightResizeCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = Handle::none;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = handleAt (e.position);
        if (dragging == Handle::none && waveBounds().contains (e.position))
        {
            // Clicking the body seeks: the playhead moving without the audio
            // following it would be a lie about where playback is.
            auto& s = processor.session();
            const float at = juce::jlimit (s.trimLeft, s.trimRight, normFor (e.position.x));
            if (onSeek) onSeek (at);
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == Handle::none)
            return;

        auto& s = processor.session();
        const float v = normFor (e.position.x);
        constexpr float minGap = 0.02f;

        if (dragging == Handle::left)
            s.trimLeft = juce::jlimit (0.0f, s.trimRight - minGap, v);
        else
            s.trimRight = juce::jlimit (s.trimLeft + minGap, 1.0f, v);

        s.playhead = juce::jlimit (s.trimLeft, s.trimRight, s.playhead);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = Handle::none; }

private:
    enum class Handle { none, left, right };

    juce::Rectangle<float> waveBounds() const
    {
        return layout::previewWave.translated (-getX(), -getY()).toFloat();
    }

    float normFor (float x) const
    {
        auto r = waveBounds();
        return juce::jlimit (0.0f, 1.0f, (x - r.getX()) / juce::jmax (1.0f, r.getWidth()));
    }

    float xFor (float norm) const
    {
        auto r = waveBounds();
        return r.getX() + norm * r.getWidth();
    }

    Handle handleAt (juce::Point<float> p) const
    {
        auto r = waveBounds();
        if (! r.expanded (12.0f, 4.0f).contains (p))
            return Handle::none;
        const auto& s = processor.session();
        if (std::abs (p.x - xFor (s.trimLeft))  <= 10.0f) return Handle::left;
        if (std::abs (p.x - xFor (s.trimRight)) <= 10.0f) return Handle::right;
        return Handle::none;
    }

    juce::String rangeText (const SessionState& s) const
    {
        if (s.previewLo.empty())
            return {};

        const double span = s.previewEnd - s.previewStart;
        const double a = s.previewStart + span * s.trimLeft;
        const double b = s.previewStart + span * s.trimRight;
        const auto& len = captureLengths()[(size_t) s.selectedLength];
        return len.label() + " / " + SessionState::stampText (a)
             + " to " + SessionState::stampText (b);
    }

    void paintWave (juce::Graphics& g, juce::Rectangle<float> r, const SessionState& s)
    {
        paint::wellSurface (g, r, 5.0f);
        paint::wellGrid (g, r.reduced (1.0f), 24, 4, tokens::accentCyan.withAlpha (0.045f));

        const float lx = xFor (s.trimLeft), rx = xFor (s.trimRight);

        // Everything outside the trim is dimmed rather than hidden - the user
        // needs to see what they are about to throw away.
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());

        if (! s.previewLo.empty())
        {
            g.setOpacity (0.30f);
            paint::waveform (g, r.reduced (2.0f, 6.0f), s.previewLo.data(), s.previewHi.data(),
                             (int) s.previewLo.size(), tokens::accentCyan.withAlpha (0.5f),
                             1.0f, false);
            g.setOpacity (1.0f);

            g.saveState();
            g.reduceClipRegion (juce::Rectangle<float> (lx, r.getY(), rx - lx, r.getHeight())
                                    .toNearestInt());
            paint::waveform (g, r.reduced (2.0f, 6.0f), s.previewLo.data(), s.previewHi.data(),
                             (int) s.previewLo.size(), tokens::accentCyan, 1.0f, true);
            g.restoreState();
        }

        if (s.previewLo.empty())
        {
            // An empty waveform while a file is being read looks like a
            // capture that failed, so it says which it is.
            g.setFont (Fonts::small().withHeight (13.0f));
            g.setColour (s.previewLoading ? tokens::accentCyan : tokens::textMuted);
            g.drawText (s.previewLoading ? "Reading capture..." : "Nothing captured yet",
                        r.toNearestInt(), juce::Justification::centred, false);
        }

        // Playhead.
        if (s.playhead >= 0.0f)
        {
            const float px = xFor (s.playhead);
            juce::Path line;
            line.startNewSubPath (px, r.getY() + 2.0f);
            line.lineTo (px, r.getBottom() - 2.0f);
            paint::glowPath (g, line, tokens::textPrimary, 1.4f, 4.0f, 0.8f, 3);
        }
        g.restoreState();

        paintHandle (g, r, lx, true);
        paintHandle (g, r, rx, false);
    }

    void paintHandle (juce::Graphics& g, juce::Rectangle<float> wave, float x, bool isLeft)
    {
        const bool lit = (hovered == (isLeft ? Handle::left : Handle::right))
                      || (dragging == (isLeft ? Handle::left : Handle::right));

        auto body = juce::Rectangle<float> (12.0f, wave.getHeight() + 8.0f)
                        .withCentre ({ x, wave.getCentreY() });

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (body.translated (0.0f, 1.0f), 3.0f);

        g.setGradientFill (juce::ColourGradient (
            lit ? tokens::redHot : juce::Colour (0xffe4441f), body.getCentreX(), body.getY(),
            tokens::redDeep, body.getCentreX(), body.getBottom(), false));
        g.fillRoundedRectangle (body, 3.0f);

        juce::Path outline;
        outline.addRoundedRectangle (body.reduced (0.5f), 3.0f);
        paint::glowPath (g, outline, tokens::accentRed, 1.0f, lit ? 7.0f : 4.0f,
                         lit ? 0.9f : 0.55f, 3);

        // Grip marks.
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        for (int i = -1; i <= 1; i += 2)
            g.fillRect (body.getCentreX() + i * 1.6f - 0.7f, body.getCentreY() - 5.0f, 1.4f, 10.0f);
    }

    void paintTimeRuler (juce::Graphics& g, juce::Rectangle<float> r, const SessionState& s)
    {
        g.setColour (tokens::stroke.withAlpha (0.7f));
        g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f);

        // Marks land on whole minutes, not on an even division of the span -
        // "-3.0" is a scale, "-3.6" is arithmetic showing through.
        const double from = s.previewStart / 60.0, to = s.previewEnd / 60.0;
        const double span = juce::jmax (1.0e-6, to - from);
        const double step = niceStep (span, 6);

        g.setFont (Fonts::tiny());
        for (double v = std::ceil (from / step) * step; v <= to + 1.0e-9; v += step)
        {
            const float t = (float) ((v - from) / span);
            const float x = r.getX() + t * r.getWidth();

            g.setColour (tokens::stroke.brighter (0.2f));
            g.fillRect (x, r.getY(), 1.0f, 5.0f);
            g.setColour (tokens::textMuted);
            g.drawText (juce::String (v, 1),
                        juce::Rectangle<float> (x - 26.0f, r.getY() + 4.0f, 52.0f,
                                                r.getHeight() - 4.0f).toNearestInt(),
                        juce::Justification::centred, false);
        }
    }

    /** The largest of 1/2/5 x 10^n that keeps the tick count near `target`. */
    static double niceStep (double span, int target)
    {
        const double raw = span / juce::jmax (1, target);
        const double mag = std::pow (10.0, std::floor (std::log10 (juce::jmax (1.0e-9, raw))));
        for (double m : { 1.0, 2.0, 5.0, 10.0 })
            if (raw <= m * mag)
                return m * mag;
        return 10.0 * mag;
    }

    void paintBarRuler (juce::Graphics& g, juce::Rectangle<float> r, const SessionState& s)
    {
        paint::wellSurface (g, r, 3.0f, juce::Colour (0xff090d12));

        // Four bars of four beats, plus the downbeat of the fifth - thirteen
        // marks, which is what the approved art fits across this width.
        const int cells = 13;
        const float cellW = r.getWidth() / (float) cells;
        g.setFont (Fonts::tiny());
        for (int i = 0; i < cells; ++i)
        {
            const float x = r.getX() + i * cellW;
            const bool downbeat = (i % 4) == 0;

            g.setColour (tokens::stroke.brighter (downbeat ? 0.35f : 0.1f));
            g.fillRect (x, r.getY() + 2.0f, 1.0f, r.getHeight() - 4.0f);

            const juce::String label = s.showBarsBeats
                ? "|" + juce::String (i / 4 + 1) + "." + juce::String (i % 4 + 1)
                : "|" + juce::String (i);
            g.setColour (downbeat ? tokens::textSecond : tokens::textMuted);
            g.drawText (label,
                        juce::Rectangle<float> (x + 4.0f, r.getY(), cellW - 6.0f,
                                                r.getHeight()).toNearestInt(),
                        juce::Justification::centredLeft, false);
        }
    }

    KeepThatProcessor& processor;
    ComboField modeBox { "Ruler mode" };
    Handle hovered = Handle::none, dragging = Handle::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapturePreviewPanel)
};

} // namespace keepthat
