/*
    ActionPanels.h - the three small panels around the capture timeline plus
    the footer: the transport column, the capture action column, the export
    destination grid and the status bar.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Paint.h"
#include "Widgets.h"
#include "../PluginProcessor.h"
#include "../Assets.h"
#include "../export/WavExporter.h"

namespace keepthat
{

// -----------------------------------------------------------------------------
//  PLAY / STOP / TRIM
// -----------------------------------------------------------------------------
class TransportColumn : public juce::Component
{
public:
    explicit TransportColumn (KeepThatProcessor& p) : processor (p)
    {
        auto add = [this] (const char* name, juce::Path glyph, const char* base,
                           const char* iconName)
        {
            auto* b = buttons.add (new TileButton (name, std::move (glyph), name,
                                                   TileButton::Layout::iconLeft));
            b->setAccent (tokens::accentCyan);
            b->setAssetBase (base);   // art carries the glyph
            addAndMakeVisible (b);
            return b;
        };

        add ("PLAY", icons::play(), "play", "")->onClick = [this]
        {
            buttons[0]->setSelected (true);
            buttons[1]->setSelected (false);
            if (onPlay) onPlay();
        };

        add ("STOP", icons::stop(), "stop", "")->onClick = [this]
        {
            buttons[0]->setSelected (false);
            buttons[1]->setSelected (true);
            if (onStop) onStop();
        };

        add ("TRIM", icons::scissors(), "trim", "")->onClick = [this]
        {
            // Commits the handles: the trimmed span becomes the whole capture.
            auto& s = processor.session();
            if (s.previewLo.empty())
                return;

            const int n = (int) s.previewLo.size();
            const int a = juce::jlimit (0, n - 2, (int) (s.trimLeft  * n));
            const int b = juce::jlimit (a + 1, n - 1, (int) (s.trimRight * n));
            const double span = s.previewEnd - s.previewStart;

            s.previewLo = { s.previewLo.begin() + a, s.previewLo.begin() + b };
            s.previewHi = { s.previewHi.begin() + a, s.previewHi.begin() + b };
            s.previewEnd   = s.previewStart + span * s.trimRight;
            s.previewStart = s.previewStart + span * s.trimLeft;
            s.trimLeft = 0.0f;
            s.trimRight = 1.0f;
            s.playhead = -1.0f;
            if (onTransport) onTransport();
        };
    }

    std::function<void()> onTransport, onPlay, onStop;

    void resized() override
    {
        auto r = layout::transportStack.translated (-getX(), -getY());
        const float h = r.getHeight() / 3.0f;
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setBounds (r.getX(), r.getY() + (int) std::round (i * h),
                                   r.getWidth(), (int) h - 4);
    }

    void paint (juce::Graphics& g) override
    {
        paint::panelSurface (g, getLocalBounds().toFloat(), Design::corner);
    }

private:
    KeepThatProcessor& processor;
    juce::OwnedArray<TileButton> buttons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportColumn)
};

// -----------------------------------------------------------------------------
//  RENAME / SAVE WAV / DRAG TO DAW
// -----------------------------------------------------------------------------
class CaptureActionColumn : public juce::Component
{
public:
    std::function<void()> onRename, onSaveWav;
    std::function<void (juce::Component*)> onDragToDaw;

    CaptureActionColumn()
    {
        auto add = [this] (const char* name, juce::Path glyph, juce::Colour accent,
                           const char* base)
        {
            auto* b = buttons.add (new TileButton (name, std::move (glyph), name));
            b->setAccent (accent);
            b->setAssetBase (base);
            b->setArtHasLabel (true);      // the slice has its own text
            addAndMakeVisible (b);
            return b;
        };

        // These slices are complete buttons with their glyph already on them.
        add ("RENAME",   icons::pencil(),  tokens::accentCyan, "rename")->onClick
            = [this] { if (onRename) onRename(); };
        add ("SAVE WAV", icons::waveDot(), tokens::accentCyan, "save_wav")->onClick
            = [this] { if (onSaveWav) onSaveWav(); };

        auto* drag = add ("DRAG TO DAW", icons::dragArrow(), tokens::accentGold, "drag_to_daw");
        drag->onClick = [this, drag] { if (onDragToDaw) onDragToDaw (drag); };

        // "DRAG TO DAW" is the longest caption in a 62 px column, so it gets a
        // smaller face rather than being clipped to "DRAG TO".
        buttons[2]->setCaptionHeight (8.5f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const float h = r.getHeight() / 3.0f;
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setBounds (r.getX(), (int) std::round (i * h),
                                   r.getWidth(), (int) h - 4);
    }

private:
    juce::OwnedArray<TileButton> buttons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureActionColumn)
};

// -----------------------------------------------------------------------------
//  EXPORT / DESTINATION
// -----------------------------------------------------------------------------
class ExportDestinationPanel : public juce::Component
{
public:
    std::function<void (juce::String)> onFolderChanged;

    explicit ExportDestinationPanel (KeepThatProcessor& p) : processor (p)
    {
        struct Entry { const char* name; juce::Path (*glyph)(); Destination dest;
                       const char* icon; };
        static const Entry entries[] = {
            { "DAW DRAG", &icons::waveBars, Destination::dawDrag,  "drag_to_daw" },
            { "SAMPLER",  &icons::sampler,  Destination::sampler,  "sampler"  },
            { "PLAYLIST", &icons::list,     Destination::playlist, "playlist" },
            { "FOLDER",   &icons::folder,   Destination::folder,   "folder"   },
            { "DESKTOP",  &icons::desktop,  Destination::desktop,  "desktop"  },
        };

        for (const auto& e : entries)
        {
            auto* b = buttons.add (new TileButton (e.name, e.glyph(), e.name));
            b->setAssetBase (e.icon);           // a complete button per target
            b->setArtHasLabel (true);           // ...with its own label on it
            addAndMakeVisible (b);
            const auto dest = e.dest;
            b->onClick = [this, dest] { select (dest); };

            // Right-click chooses where this destination writes. That is what
            // makes SAMPLER and PLAYLIST real targets rather than labels
            // implying an integration that does not exist - point SAMPLER at
            // your sampler's own library folder and it lands there.
            if (dest != Destination::dawDrag)
                b->onSecondaryClick = [this, dest] { chooseFolder (dest); };
        }
        syncSelection();
        setTooltipsFromFolders();
    }

    /** Re-reads the destination and its folders - needed after a preset load
        changes them behind the buttons' backs. */
    void refresh()
    {
        syncSelection();
        setTooltipsFromFolders();
        repaint();
    }

    void resized() override
    {
        const auto off = juce::Point<int> (-getX(), -getY());
        auto row1 = layout::exportRow1.translated (off.x, off.y);
        auto row2 = layout::exportRow2.translated (off.x, off.y);

        buttons[0]->setBounds (layout::cell (row1, 0, 2, 6));
        buttons[1]->setBounds (layout::cell (row1, 1, 2, 6));
        for (int i = 0; i < 3; ++i)
            buttons[2 + i]->setBounds (layout::cell (row2, i, 3, 6));
    }

    void paint (juce::Graphics& g) override
    {
        paint::panelSurface (g, getLocalBounds().toFloat(), Design::corner);

        // 19 px clips this title inside a 208 px panel; the approved art sets
        // it noticeably smaller than the other panel headings for that reason.
        g.setFont (Fonts::panelTitle().withHeight (15.0f));
        g.setColour (tokens::textPrimary);
        g.drawText ("EXPORT / DESTINATION", layout::exportTitle.translated (-getX(), -getY()),
                    juce::Justification::centredLeft, false);
    }

    /** Refreshed after a folder change so hovering a button says where it
        actually writes. */
    void setTooltipsFromFolders()
    {
        static const Destination order[5] = { Destination::dawDrag, Destination::sampler,
                                              Destination::playlist, Destination::folder,
                                              Destination::desktop };
        for (int i = 0; i < buttons.size() && i < 5; ++i)
        {
            if (order[i] == Destination::dawDrag)
            {
                buttons[i]->setTooltip ("Drag the capture straight into your DAW");
                continue;
            }
            const auto dir = WavExporter::directoryFor (order[i], processor.session());
            buttons[i]->setTooltip ("Saves to " + dir.getFullPathName()
                                    + "\nRight-click to choose a different folder");
        }
    }

private:
    void chooseFolder (Destination dest)
    {
        const auto current = WavExporter::directoryFor (dest, processor.session());
        chooser = std::make_unique<juce::FileChooser> (
            "Where should this destination save captures?", current);

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                              [this, dest] (const juce::FileChooser& fc)
        {
            const auto picked = fc.getResult();
            if (picked == juce::File() || ! picked.isDirectory())
                return;

            processor.session().destinationFolder[juce::jlimit (0, 4, (int) dest)] = picked;
            setTooltipsFromFolders();
            if (onFolderChanged)
                onFolderChanged (picked.getFileName());
        });
    }

    void select (Destination d)
    {
        processor.session().destination = d;
        syncSelection();
    }

    void syncSelection()
    {
        const int index = (int) processor.session().destination;
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setSelected (i == index);
    }

    KeepThatProcessor& processor;
    juce::OwnedArray<TileButton> buttons;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportDestinationPanel)
};

// -----------------------------------------------------------------------------
//  Footer: version, the gold tagline and the year plate.
// -----------------------------------------------------------------------------
class FooterBar : public juce::Component,
                  private juce::Timer
{
public:
    FooterBar() { setInterceptsMouseClicks (false, false); }

    /** Shows `text` in place of the tagline for a few seconds. This is where
        the engine's outcomes surface - "Kept 00:04", "Nothing in the buffer
        yet" - so a failure is visible rather than a button that did nothing. */
    void flashMessage (const juce::String& text)
    {
        if (text.isEmpty())
            return;
        message = text;
        startTimer (4000);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = juce::Point<int> (-getX(), -getY());

        // A gold hairline sweeping in from both sides toward the tagline.
        const float y = 8.0f;
        juce::ColourGradient rule (tokens::accentGold.withAlpha (0.0f), 0.0f, y,
                                   tokens::accentGold.withAlpha (0.0f), (float) getWidth(), y,
                                   false);
        rule.addColour (0.28, tokens::accentGold.withAlpha (0.35f));
        rule.addColour (0.5,  tokens::accentGold.withAlpha (0.0f));
        rule.addColour (0.72, tokens::accentGold.withAlpha (0.35f));
        g.setGradientFill (rule);
        g.fillRect (0.0f, y, (float) getWidth(), 1.0f);

        g.setFont (Fonts::small());
        g.setColour (tokens::textMuted);
        g.drawText ("v" JucePlugin_VersionString,
                    layout::footerVersion.translated (off.x, off.y),
                    juce::Justification::centredLeft, false);

        if (message.isNotEmpty())
            paint::glowText (g, message.toUpperCase(),
                             layout::footerTagline.translated (off.x, off.y),
                             Fonts::footer().withHeight (12.0f), tokens::accentCyan,
                             juce::Justification::centred, 0.45f);
        else
            paint::glowText (g, "NEVER LOSE THE MOMENT",
                             layout::footerTagline.translated (off.x, off.y),
                             Fonts::footer(), tokens::accentGold,
                             juce::Justification::centred, 0.35f);

        auto plate = layout::footerYear.translated (off.x, off.y).toFloat();
        g.setColour (tokens::accentGold.withAlpha (0.45f));
        g.drawRoundedRectangle (plate.reduced (0.5f), 3.0f, 1.0f);
        g.setFont (Fonts::tiny());
        g.setColour (tokens::accentGold);
        g.drawText ("2026", plate.toNearestInt(), juce::Justification::centred, false);

        icons::draw (g, icons::brandMark(),
                     layout::footerMark.translated (off.x, off.y).toFloat(),
                     tokens::accentGold, 0.55f);
    }

private:
    void timerCallback() override
    {
        stopTimer();
        message.clear();
        repaint();
    }

    juce::String message;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FooterBar)
};

} // namespace keepthat
