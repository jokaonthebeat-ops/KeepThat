/*
    RecoveryToolsPanel.h - the six processing toggles and the PHRASE DETECTED
    card beneath them.

    Two rows carry extra controls rather than a plain switch: FADE IN / OUT owns
    a pair of compact dials, and NORMALIZE owns a target-level readout. Both are
    laid out inside the row so the stack keeps a single rhythm.
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

class RecoveryToolsPanel : public juce::Component
{
public:
    explicit RecoveryToolsPanel (KeepThatProcessor& p) : processor (p)
    {
        struct Row { const char* title; const char* sub; juce::Path (*glyph)(); };
        static const Row rows[] = {
            { "AUTO TRIM",     "Remove silence from ends", &icons::waveBars },
            { "SILENCE DETECT","Detect and cut silent gaps", &icons::waveGap },
            { "ZERO-CROSSING", "Trim at zero crossings",   &icons::zeroCrossing },
            { "FADE IN / OUT", "",                          &icons::fadeCurve },
            { "NORMALIZE",     "",                          &icons::normalizeBars },
            { "DRAG EXPORT",   "Drag audio to your DAW",    &icons::download },
        };

        for (int i = 0; i < layout::recoveryRows; ++i)
        {
            titles.add (rows[i].title);
            subs.add (rows[i].sub);
            glyphs.push_back (rows[i].glyph());

            // The FADE row has no switch in the approved art - the two dials
            // are the control. A null entry keeps the row indices aligned with
            // SessionState::tool.
            if (i == fadeRowIndex)
            {
                toggles.add (nullptr);
                continue;
            }

            auto* t = new PillToggle (rows[i].title);
            toggles.add (t);
            addAndMakeVisible (t);

            // Each switch is a real parameter, so it automates and persists.
            using namespace params;
            static const char* paramFor[layout::recoveryRows] = {
                id::autoTrimEnabled, id::silenceDetectEnabled, id::zeroCrossingEnabled,
                nullptr, id::normalizeEnabled, id::dragExportEnabled
            };
            if (paramFor[i] != nullptr)
                links.add (new ToggleLink (processor.parameters(), paramFor[i], *t,
                                           [this] (bool) { repaint(); }));
        }

        addAndMakeVisible (fadeIn);
        addAndMakeVisible (fadeOut);
        fadeIn.setFilmstrip (art::knobSmall);
        fadeOut.setFilmstrip (art::knobSmall);
        fadeIn.formatValue  = [] (float v) { return juce::String (juce::roundToInt (v * 200.0f)) + " ms"; };
        fadeOut.formatValue = [] (float v) { return juce::String (juce::roundToInt (v * 200.0f)) + " ms"; };

        // The plan has one fade parameter, and the approved panel has an IN and
        // an OUT dial. Both are bound to it so the interface cannot show two
        // numbers that the engine does not actually apply separately.
        knobLinks.add (new KnobLink (processor.parameters(), params::id::fadeMilliseconds, fadeIn));
        knobLinks.add (new KnobLink (processor.parameters(), params::id::fadeMilliseconds, fadeOut));
    }

    void resized() override
    {
        const auto off = juce::Point<int> (-getX(), -getY());
        auto stack = layout::recoveryStack.translated (off.x, off.y);
        const float rowH = stack.getHeight() / (float) layout::recoveryRows;

        for (int i = 0; i < toggles.size(); ++i)
            if (auto* t = toggles[i])
            {
                auto row = rowAt (stack, i, rowH);
                t->setBounds (row.getRight() - 68, row.getCentreY() - 15, 62, 30);
            }

        auto fadeRow = rowAt (stack, fadeRowIndex, rowH);
        const int knobH = (int) rowH - 8;
        fadeIn.setBounds  (fadeRow.getRight() - 174, fadeRow.getY() + 3, 84, knobH);
        fadeOut.setBounds (fadeRow.getRight() -  88, fadeRow.getY() + 3, 84, knobH);
    }

    void update()
    {
        // The parameter links keep the switches in step with the host, so
        // there is nothing to poll here.
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = juce::Point<int> (-getX(), -getY());
        paint::panelSurface (g, getLocalBounds().toFloat(), Design::corner);

        widgets::panelTitle (g, "RECOVERY TOOLS", layout::recoveryTitle.translated (off.x, off.y));
        icons::draw (g, icons::info(), layout::recoveryInfo.translated (off.x, off.y).toFloat(),
                     tokens::textMuted, 0.8f);

        auto stack = layout::recoveryStack.translated (off.x, off.y);
        const float rowH = stack.getHeight() / (float) layout::recoveryRows;
        for (int i = 0; i < layout::recoveryRows; ++i)
            paintRow (g, rowAt (stack, i, rowH).toFloat(), i);

        paintPhraseCard (g, layout::phraseCard.translated (off.x, off.y).toFloat());
    }

private:
    static juce::Rectangle<int> rowAt (juce::Rectangle<int> stack, int i, float rowH)
    {
        return { stack.getX(), stack.getY() + (int) std::round (i * rowH),
                 stack.getWidth(), (int) std::round (rowH) - 4 };
    }

    void paintRow (juce::Graphics& g, juce::Rectangle<float> r, int index)
    {
        const bool on = index < toggles.size() && toggles[index] != nullptr
                          ? toggles[index]->getToggleState()
                          : processor.settings().fadeEnabled;

        // No supplied row art is used here. v1.4 ships none, and v1.3's
        // recovery_row_* belongs to that pack's heavier look - dropped onto
        // this layout it buries the row's own title and subtitle.

        paint::panelSurface (g, r, 6.0f,
                             on ? juce::Colour (0xff151c26) : juce::Colour (0xff11171f),
                             juce::Colour (0xff0c1117),
                             on ? tokens::stroke.brighter (0.10f) : tokens::stroke);

        paintRowContents (g, r, index, on);
    }

    void paintRowContents (juce::Graphics& g, juce::Rectangle<float> r, int index, bool on)
    {
        // The supplied row shell is recessed, so its content sits further in
        // than the procedural row's did.
        auto body = r.reduced (12.0f, 0.0f);
        auto iconArea = body.removeFromLeft (34.0f).withSizeKeepingCentre (26.0f, 26.0f);
        const auto tint = on ? tokens::accentGold : tokens::textMuted;
        static const char* iconNames[] = { "auto_trim", "silence", "zero_cross",
                                           "fade", "normalize", "download" };
        if (! (componentArtwork()
               && Assets::drawFittedTrimmed (g, art::icon (iconNames[index]), iconArea,
                                             on ? 1.0f : 0.55f)))
            icons::draw (g, glyphs[(size_t) index], iconArea, tint, on ? 0.95f : 0.55f);

        auto text = body.withTrimmedLeft (10.0f)
                        .withTrimmedRight (index == fadeRowIndex ? 182.0f : 72.0f);
        const bool hasSub = subs[index].isNotEmpty();

        g.setFont (Fonts::rowTitle());
        g.setColour (on ? tokens::textPrimary : tokens::textSecond);
        g.drawText (titles[index],
                    (hasSub ? text.removeFromTop (text.getHeight() * 0.56f) : text).toNearestInt(),
                    hasSub ? juce::Justification::bottomLeft : juce::Justification::centredLeft,
                    false);

        if (hasSub)
        {
            g.setFont (Fonts::rowSub());
            g.setColour (tokens::textMuted);
            g.drawText (subs[index], text.toNearestInt(), juce::Justification::topLeft, false);
        }

        // NORMALIZE carries its target level in the space the subtitle would use.
        if (index == 4)
        {
            auto target = juce::Rectangle<float> (r.getX() + 190.0f, r.getY() + 4.0f,
                                                  150.0f, r.getHeight() - 8.0f);
            g.setFont (Fonts::tiny());
            g.setColour (tokens::textMuted);
            g.drawText ("Target Level",
                        target.removeFromTop (target.getHeight() * 0.5f).toNearestInt(),
                        juce::Justification::centredBottom, false);
            g.setFont (Fonts::make (15.0f, true));
            g.setColour (on ? tokens::textPrimary : tokens::textSecond);
            g.drawText (juce::String (processor.settings().normalizeTargetDb, 1) + " dB",
                        target.toNearestInt(), juce::Justification::centredTop, false);
        }
    }

    void paintPhraseCard (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& p = processor.session().phrase;

        if (! (componentArtwork()
               && Assets::drawStretchedTrimmed (g, art::tile (p.detected ? "phrase_card_detected"
                                                                         : "phrase_card_normal"),
                                                r)))
        {
            paint::panelSurface (g, r, 8.0f, juce::Colour (0xff180c09),
                                 juce::Colour (0xff0d0706),
                                 tokens::accentRed.withAlpha (0.75f));
            juce::Path outline;
            outline.addRoundedRectangle (r.reduced (0.5f), 8.0f);
            paint::glowPath (g, outline, tokens::accentRed, 1.2f, 5.0f, 0.55f, 3);
        }

        // The supplied card carries decorative bars; the live readout needs a
        // clean ground, so the text half is knocked back before it is drawn.
        if (componentArtwork())
        {
            g.setColour (juce::Colour (0xff120806).withAlpha (0.88f));
            g.fillRoundedRectangle (r.reduced (8.0f, 7.0f).withTrimmedLeft (r.getWidth() * 0.36f),
                                    4.0f);
        }

        auto body = r.reduced (10.0f, 8.0f);
        auto waveArea = body.removeFromLeft (body.getWidth() * 0.36f);

        paint::wellSurface (g, waveArea, 4.0f, juce::Colour (0xff07090d));
        if (! p.thumbLo.empty())
        {
            g.saveState();
            g.reduceClipRegion (waveArea.toNearestInt());
            paint::waveform (g, waveArea.reduced (5.0f, 6.0f), p.thumbLo.data(), p.thumbHi.data(),
                             (int) p.thumbLo.size(), tokens::accentCyan, 0.95f, true);
            g.restoreState();
        }
        // The suggested start, marked in red on the thumbnail.
        g.setColour (tokens::accentRed);
        g.fillRect (waveArea.getX() + 5.0f, waveArea.getY() + 3.0f, 1.6f, waveArea.getHeight() - 6.0f);

        auto text = body.withTrimmedLeft (12.0f);
        auto line = [&text] (float h) { return text.removeFromTop (h).toNearestInt(); };

        g.setFont (Fonts::make (13.5f, false, true).withExtraKerningFactor (0.05f));
        g.setColour (tokens::accentRed);
        g.drawText (p.detected ? "PHRASE DETECTED" : "NO PHRASE FOUND", line (18.0f),
                    juce::Justification::centredLeft, false);

        g.setFont (Fonts::small());
        g.setColour (tokens::textSecond);
        g.drawText ("Suggested: " + juce::String (p.suggestedBars, 1) + " Bars", line (17.0f),
                    juce::Justification::centredLeft, false);

        auto confRow = line (17.0f);
        g.setColour (tokens::textSecond);
        g.drawText ("Confidence: " + juce::String ((int) std::round (p.confidence * 100.0f)) + "%",
                    confRow, juce::Justification::centredLeft, false);

        // Confidence bar, right of the label.
        auto bar = juce::Rectangle<float> ((float) confRow.getX() + 96.0f,
                                           (float) confRow.getCentreY() - 4.0f,
                                           (float) confRow.getWidth() - 100.0f, 8.0f);
        widgets::ledRow (g, bar, p.confidence, -1.0f, 14);

        g.setFont (Fonts::tiny());
        g.setColour (tokens::textMuted);
        auto stamps = line (18.0f);
        g.drawText ("Start: " + SessionState::stampText (p.startSeconds), stamps,
                    juce::Justification::centredLeft, false);
        g.drawText ("End:  " + SessionState::stampText (p.endSeconds), stamps,
                    juce::Justification::centredRight, false);
    }

    static constexpr int fadeRowIndex = 3;

    KeepThatProcessor& processor;
    juce::OwnedArray<PillToggle> toggles;   // null at fadeRowIndex
    juce::OwnedArray<KnobLink> knobLinks;
    juce::OwnedArray<ToggleLink> links;
    juce::StringArray titles, subs;
    std::vector<juce::Path> glyphs;
    MiniKnob fadeIn { "IN", "10 ms" }, fadeOut { "OUT", "10 ms" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecoveryToolsPanel)
};

} // namespace keepthat
