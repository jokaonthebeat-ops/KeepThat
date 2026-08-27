/*
    HeaderComponent.h - brand mark, wordmark, preset navigation and the six
    utility buttons.

    The logo is drawn, not blitted: a red capture ring beside "KEEP THAT!" in
    a heavy grotesque with a metallic vertical gradient, over the subtitle
    "Always-On Idea Capture".
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

class HeaderComponent : public juce::Component
{
public:
    explicit HeaderComponent (KeepThatProcessor& p) : processor (p)
    {
        setInterceptsMouseClicks (false, true);

        auto addUtility = [this] (const juce::String& name, juce::Path glyph,
                                  const juce::String& caption, juce::Colour tint,
                                  const juce::String& assetBase)
        {
            auto* b = utilities.add (new HeaderIconButton (name, std::move (glyph), caption, tint));
            b->setAssetBase ("blank");        // transport-style plate
            b->setIconAsset (assetBase);      // PNG_64 glyph on top
            addAndMakeVisible (b);
            return b;
        };

        addUtility ("Save",     icons::save(),  "SAVE",     tokens::textSecond, "save")
            ->onClick = [this] { if (onSave) onSave(); };
        addUtility ("Settings", icons::gear(),  "SETTINGS", tokens::textSecond, "settings")
            ->onClick = [this] { if (onSettings) onSettings(); };
        addUtility ("Help",     icons::help(),  "HELP",     tokens::textSecond, "help")
            ->onClick = [this] { if (onHelp) onHelp(); };
        undoButton = addUtility ("Undo", icons::undoArrow (false), "UNDO",
                                 tokens::textSecond, "undo");
        redoButton = addUtility ("Redo", icons::undoArrow (true), "REDO",
                                 tokens::textSecond, "redo");

        undoButton->onClick = [this]
        {
            auto& h = processor.history();
            const auto what = h.getUndoDescription();
            if (h.undo() && onHistoryChanged)
                onHistoryChanged (what.isEmpty() ? juce::String ("Undone")
                                                 : "Undone: " + what);
        };
        redoButton->onClick = [this]
        {
            auto& h = processor.history();
            const auto what = h.getRedoDescription();
            if (h.redo() && onHistoryChanged)
                onHistoryChanged (what.isEmpty() ? juce::String ("Redone")
                                                 : "Redone: " + what);
        };
        powerButton = addUtility ("Power", icons::power(), "POWER",   tokens::accentRed,  "power");

        powerButton->setClickingTogglesState (true);
        powerButton->setToggleState (processor.session().armed, juce::dontSendNotification);
        powerButton->onClick = [this]
        {
            processor.session().armed = powerButton->getToggleState();
            if (onArmedChanged) onArmedChanged();
        };

        addAndMakeVisible (prevPreset);
        addAndMakeVisible (nextPreset);
        prevPreset.onClick = [this] { if (onPresetStep) onPresetStep (-1); repaint(); };
        nextPreset.onClick = [this] { if (onPresetStep) onPresetStep (1); repaint(); };
    }

    std::function<void()> onArmedChanged;
    std::function<void (juce::String)> onHistoryChanged;
    std::function<void()> onSave, onSettings, onHelp;
    std::function<void (int)> onPresetStep;

    void resized() override
    {
        auto bar = layout::presetBar.translated (-getX(), -getY());
        prevPreset.setBounds (bar.getX() + 4, bar.getY() + 4, 42, bar.getHeight() - 8);
        nextPreset.setBounds (bar.getRight() - 46, bar.getY() + 4, 42, bar.getHeight() - 8);

        auto utils = layout::headerUtils.translated (-getX(), -getY());
        // SAVE / SETTINGS / HELP / UNDO / REDO sit on an even pitch; POWER is
        // pushed to the far right with a wider gap, as in the mockup.
        const int cellW = 70;
        for (int i = 0; i < 5; ++i)
            utilities[i]->setBounds (utils.getX() + i * cellW, utils.getY(), cellW, utils.getHeight());
        powerButton->setBounds (utils.getRight() - 60, utils.getY(), 60, utils.getHeight());
    }

    void paint (juce::Graphics& g) override
    {
        paintLogo (g);
        paintPresetBar (g);

        // Hairline under the header, brightening toward the centre.
        const float y = (float) getHeight() - 1.0f;
        juce::ColourGradient rule (tokens::stroke.withAlpha (0.0f), 0.0f, y,
                                   tokens::stroke.withAlpha (0.0f), (float) getWidth(), y, false);
        rule.addColour (0.5, tokens::strokeHi.withAlpha (0.9f));
        g.setGradientFill (rule);
        g.fillRect (0.0f, y, (float) getWidth(), 1.0f);
    }

    void refresh()
    {
        if (powerButton->getToggleState() != processor.session().armed)
            powerButton->setToggleState (processor.session().armed, juce::dontSendNotification);

        // A button that looks live but does nothing is worse than one that
        // looks unavailable, so these dim when the history is empty.
        auto& h = processor.history();
        setUtilityEnabled (undoButton, h.canUndo());
        setUtilityEnabled (redoButton, h.canRedo());
    }

private:
    void paintLogo (juce::Graphics& g)
    {
        // The supplied logo already contains the mark, the wordmark and the
        // "ALWAYS-ON IDEA CAPTURE" rule, so it replaces all three.
        if (componentArtwork())
        {
            auto area = layout::logoMark.getUnion (layout::logoText)
                            .translated (-getX(), -getY()).toFloat();
            if (Assets::drawFitted (g, art::logo, area))
                return;
        }

        auto mark = layout::logoMark.translated (-getX(), -getY()).toFloat();

        paint::halo (g, mark.getCentre(), mark.getWidth() * 0.85f, tokens::accentRed, 0.30f);
        icons::drawGlowing (g, icons::brandMark(), mark.reduced (2.0f), tokens::accentRed, 0.7f);

        auto text = layout::logoText.translated (-getX(), -getY());
        auto wordArea = text.removeFromTop (48);

        // Brushed-metal wordmark: light crown, graphite foot, red underglow.
        const auto font = Fonts::logo();
        g.setFont (font);
        g.setColour (tokens::accentRed.withAlpha (0.25f));
        g.drawText ("KEEP THAT!", wordArea.translated (0, 2),
                    juce::Justification::centredLeft, false);

        juce::ColourGradient metal (juce::Colour (0xfff7f9fb), 0.0f, (float) wordArea.getY(),
                                    juce::Colour (0xff8b949f), 0.0f, (float) wordArea.getBottom(),
                                    false);
        metal.addColour (0.52, juce::Colour (0xffe2e7ec));
        metal.addColour (0.56, juce::Colour (0xffa9b2bc));
        g.setGradientFill (metal);
        g.drawText ("KEEP THAT!", wordArea, juce::Justification::centredLeft, false);

        g.setFont (Fonts::logoSub());
        g.setColour (tokens::textSecond);
        g.drawText ("Always-On Idea Capture", text.translated (2, -4),
                    juce::Justification::topLeft, false);
    }

    void paintPresetBar (juce::Graphics& g)
    {
        auto bar = layout::presetBar.translated (-getX(), -getY()).toFloat();

        paint::wellSurface (g, bar, bar.getHeight() * 0.5f, juce::Colour (0xff0a0e14));
        g.setColour (tokens::strokeHi.withAlpha (0.55f));
        g.drawRoundedRectangle (bar.reduced (0.5f), bar.getHeight() * 0.5f, 1.0f);
        paintPresetName (g, false);
    }

    /** When `patch` is set the artwork's baked preset name is covered first. */
    void paintPresetName (juce::Graphics& g, bool patch)
    {
        auto bar = layout::presetBar.translated (-getX(), -getY()).toFloat();

        if (patch)
        {
            // Only the text run, not the whole pill - the bar's frame, its
            // inner shadow and the arrows all stay as supplied.
            auto strip = bar.reduced (52.0f, 6.0f);
            juce::ColourGradient bed (juce::Colour (0xff0c1118), strip.getCentreX(), strip.getY(),
                                      juce::Colour (0xff090d13), strip.getCentreX(),
                                      strip.getBottom(), false);
            g.setGradientFill (bed);
            g.fillRoundedRectangle (strip, strip.getHeight() * 0.5f);
        }

        g.setFont (Fonts::presetName());
        g.setColour (tokens::textPrimary);
        g.drawText (processor.session().presetName, bar.toNearestInt(),
                    juce::Justification::centred, false);
    }

    static void setUtilityEnabled (HeaderIconButton* b, bool on)
    {
        if (b != nullptr && b->isEnabled() != on)
        {
            b->setEnabled (on);
            b->repaint();
        }
    }

    KeepThatProcessor& processor;
    juce::OwnedArray<HeaderIconButton> utilities;
    HeaderIconButton* powerButton = nullptr;
    HeaderIconButton* undoButton = nullptr;
    HeaderIconButton* redoButton = nullptr;

    class ArrowButton : public AnimatedButton
    {
    public:
        ArrowButton (const juce::String& n, bool right)
            : AnimatedButton (n), path (icons::chevron (right ? 0.0f : juce::MathConstants<float>::pi)) {}

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat();
            if (glow.amount > 0.01f)
            {
                g.setColour (tokens::accentCyan.withAlpha (glow.amount * 0.10f));
                g.fillRoundedRectangle (r.reduced (2.0f), 6.0f);
            }
            icons::draw (g, path, r.withSizeKeepingCentre (18.0f, 18.0f)
                                   .translated (0.0f, down ? 1.0f : 0.0f),
                         over ? tokens::textPrimary : tokens::textSecond);
        }

    private:
        juce::Path path;
    };

    ArrowButton prevPreset { "Previous preset", false };
    ArrowButton nextPreset { "Next preset", true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderComponent)
};

} // namespace keepthat
