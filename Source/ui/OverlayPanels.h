/*
    OverlayPanels.h - what SETTINGS and HELP open.

    Both are in-editor panels, not modal dialogs. A plugin cannot rely on a
    host to present a modal window sensibly - some never show them, some show
    them behind the plugin - so anything the user needs to read or change lives
    inside the editor's own bounds and is dismissed by clicking away from it.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Paint.h"
#include "Widgets.h"
#include "../PluginProcessor.h"
#include "../export/WavExporter.h"
#include "../state/PresetManager.h"

namespace keepthat
{

/** Shared chrome: a dimmed backdrop and a centred card that closes on a click
    outside it or on Escape. */
class OverlayBase : public juce::Component
{
public:
    OverlayBase() { setVisible (false); setWantsKeyboardFocus (true); }

    std::function<void()> onDismiss;

    void show()
    {
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff05070b).withAlpha (0.72f));

        auto card = cardBounds().toFloat();
        paint::panelSurface (g, card, Design::corner, tokens::panel3, tokens::panel2,
                             tokens::accentCyan.withAlpha (0.55f));
        juce::Path outline;
        outline.addRoundedRectangle (card.reduced (0.5f), Design::corner);
        paint::glowPath (g, outline, tokens::accentCyan, 1.2f, 6.0f, 0.45f, 3);

        paintCard (g, card.reduced (26.0f, 22.0f));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cardBounds().contains (e.getPosition()))
            dismiss();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey) { dismiss(); return true; }
        return false;
    }

    void dismiss()
    {
        setVisible (false);
        if (onDismiss) onDismiss();
    }

protected:
    virtual juce::Rectangle<int> cardBounds() const = 0;
    virtual void paintCard (juce::Graphics&, juce::Rectangle<float> body) = 0;

    static void heading (juce::Graphics& g, juce::Rectangle<float>& body,
                         const juce::String& text)
    {
        g.setFont (Fonts::panelTitle());
        g.setColour (tokens::textPrimary);
        g.drawText (text, body.removeFromTop (30.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);
    }

    static void line (juce::Graphics& g, juce::Rectangle<float>& body,
                      const juce::String& left, const juce::String& right,
                      juce::Colour rightColour = tokens::textSecond)
    {
        auto row = body.removeFromTop (25.0f);
        g.setFont (Fonts::small().withHeight (13.0f));
        g.setColour (tokens::textMuted);
        g.drawText (left, row.removeFromLeft (row.getWidth() * 0.42f).toNearestInt(),
                    juce::Justification::centredLeft, false);
        g.setColour (rightColour);
        g.drawText (right, row.toNearestInt(), juce::Justification::centredLeft, false);
    }
};

// -----------------------------------------------------------------------------
//  SETTINGS
// -----------------------------------------------------------------------------
class SettingsOverlay : public OverlayBase
{
public:
    explicit SettingsOverlay (KeepThatProcessor& p) : processor (p)
    {
        auto addToggle = [this] (const juce::String& name, bool initial,
                                 std::function<void (bool)> onChange)
        {
            auto* t = toggles.add (new PillToggle (name));
            addAndMakeVisible (t);
            t->setToggleState (initial, juce::dontSendNotification);
            t->onClick = [t, cb = std::move (onChange)] { cb (t->getToggleState()); };
            return t;
        };

        addToggle ("Restart buffer after KEEP", processor.session().restartBufferAfterKeep,
                   [this] (bool on) { processor.session().restartBufferAfterKeep = on; });

        addToggle ("Low power mode", processor.session().lowPowerMode,
                   [this] (bool on)
                   {
                       processor.session().lowPowerMode = on;
                       if (onPerformanceChanged) onPerformanceChanged();
                   });

        addToggle ("Reduce motion", processor.session().reduceMotion,
                   [this] (bool on)
                   {
                       processor.session().reduceMotion = on;
                       if (onPerformanceChanged) onPerformanceChanged();
                   });

        addToggle ("Write .m3u for Playlist", processor.session().writePlaylistFile,
                   [this] (bool on) { processor.session().writePlaylistFile = on; });

        addAndMakeVisible (revealCaptures);
        revealCaptures.setAssetBase ("blank");
        revealCaptures.setCaptionInside (true);
        revealCaptures.onClick = [this]
        {
            auto dir = WavExporter::directoryFor (Destination::folder, processor.session());
            dir.createDirectory();
            WavExporter::reveal (dir);
        };

        addAndMakeVisible (revealPresets);
        revealPresets.setAssetBase ("blank");
        revealPresets.setCaptionInside (true);
        revealPresets.onClick = [] { WavExporter::reveal (PresetManager::presetsFolder()); };

        addAndMakeVisible (clearBufferButton);
        clearBufferButton.setAssetBase ("blank");
        clearBufferButton.setCaptionInside (true);
        clearBufferButton.onClick = [this]
        {
            processor.clearBuffer();
            if (onBufferCleared) onBufferCleared();
        };

        addAndMakeVisible (closeButton);
        closeButton.setAssetBase ("blank");
        closeButton.setCaptionInside (true);
        closeButton.onClick = [this] { dismiss(); };
    }

    std::function<void()> onPerformanceChanged;
    std::function<void()> onBufferCleared;

    void resized() override
    {
        auto body = cardBounds().reduced (26, 22).withTrimmedTop (76);
        for (int i = 0; i < toggles.size(); ++i)
            toggles[i]->setBounds (body.getRight() - 66, body.getY() + i * 34 + 2, 60, 28);

        auto area = cardBounds().reduced (26, 22);
        auto buttons = area.removeFromBottom (44);
        revealCaptures.setBounds (buttons.removeFromLeft (150));
        buttons.removeFromLeft (10);
        revealPresets.setBounds (buttons.removeFromLeft (150));
        closeButton.setBounds (buttons.removeFromRight (86));

        area.removeFromBottom (14);
        clearBufferButton.setBounds (area.removeFromBottom (44).removeFromLeft (150));
    }

protected:
    juce::Rectangle<int> cardBounds() const override
    {
        return juce::Rectangle<int> (520, 404).withCentre (getLocalBounds().getCentre());
    }

    void paintCard (juce::Graphics& g, juce::Rectangle<float> body) override
    {
        heading (g, body, "SETTINGS");

        g.setFont (Fonts::small().withHeight (12.0f));
        g.setColour (tokens::textMuted);
        g.drawFittedText ("Buffer length, sensitivity and the recovery tools are on the "
                          "main panel - they are automatable parameters.",
                          body.removeFromTop (32.0f).toNearestInt(),
                          juce::Justification::topLeft, 2);

        body.removeFromTop (14.0f);
        static const char* labels[] = { "Restart buffer after KEEP",
                                        "Low power mode",
                                        "Reduce motion",
                                        "Write .m3u for Playlist" };
        static const char* notes[] = { "The clock starts again from 0:00 each time you keep something",
                                       "30 fps instead of 60. Roughly halves the interface's CPU",
                                       "Stops the ring's drift; meters keep moving",
                                       "Playlist destination also maintains a playlist file" };
        for (int i = 0; i < 4; ++i)
        {
            auto row = body.removeFromTop (34.0f);
            g.setFont (Fonts::rowTitle().withHeight (13.0f));
            g.setColour (tokens::textPrimary);
            g.drawText (labels[i], row.removeFromTop (16.0f).toNearestInt(),
                        juce::Justification::centredLeft, false);
            g.setFont (Fonts::rowSub());
            g.setColour (tokens::textMuted);
            g.drawText (notes[i], row.toNearestInt(),
                        juce::Justification::centredLeft, false);
        }

        body.removeFromTop (8.0f);
        line (g, body, "Captures", WavExporter::directoryFor (Destination::folder,
                                                              processor.session())
                                       .getFullPathName());

        // The explanation for CLEAR BUFFER sits beside the button, which is
        // laid out from the bottom of the card - so it is placed against the
        // card rather than against whatever `body` has left.
        auto row = cardBounds().reduced (26, 22).removeFromBottom (102)
                               .removeFromTop (44).toFloat();
        row.removeFromLeft (160.0f);
        g.setFont (Fonts::rowTitle().withHeight (13.0f));
        g.setColour (tokens::textPrimary);
        g.drawText ("Start the buffer clock again",
                    row.removeFromTop (17.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);
        g.setFont (Fonts::rowSub());
        g.setColour (tokens::textMuted);
        g.drawFittedText ("Throws away the history and counts up from 0:00. "
                          "Recording never stops - your keeps are not affected.",
                          row.toNearestInt(), juce::Justification::topLeft, 2);
    }

private:
    KeepThatProcessor& processor;
    juce::OwnedArray<PillToggle> toggles;
    TileButton clearBufferButton { "Clear buffer", {}, "CLEAR BUFFER" };
    TileButton revealCaptures { "Reveal captures", {}, "CAPTURES FOLDER" };
    TileButton revealPresets  { "Reveal presets",  {}, "PRESETS FOLDER" };
    TileButton closeButton    { "Close", {}, "CLOSE" };
};

// -----------------------------------------------------------------------------
//  HELP
// -----------------------------------------------------------------------------
class HelpOverlay : public OverlayBase
{
public:
    HelpOverlay()
    {
        addAndMakeVisible (closeButton);
        closeButton.setAssetBase ("blank");
        closeButton.setCaptionInside (true);
        closeButton.onClick = [this] { dismiss(); };
    }

    void resized() override
    {
        auto buttons = cardBounds().reduced (26, 22).removeFromBottom (44);
        closeButton.setBounds (buttons.removeFromRight (86));
    }

protected:
    juce::Rectangle<int> cardBounds() const override
    {
        return juce::Rectangle<int> (640, 480).withCentre (getLocalBounds().getCentre());
    }

    void paintCard (juce::Graphics& g, juce::Rectangle<float> body) override
    {
        heading (g, body, "KEEP THAT!");

        g.setFont (Fonts::small().withHeight (13.0f));
        g.setColour (tokens::textSecond);
        g.drawFittedText ("Everything coming into this plugin is held in a rolling buffer. "
                          "When you play something worth keeping and the recorder was not "
                          "running, press KEEP LAST and pull it back.",
                          body.removeFromTop (46.0f).toNearestInt(),
                          juce::Justification::topLeft, 3);

        body.removeFromTop (8.0f);
        static const char* rows[][2] = {
            { "KEEP LAST",      "Recovers the selected length from the buffer" },
            { "1 BAR ... 60 SEC","How much to recover. Bars follow the host tempo" },
            { "PHRASE",         "Finds the last musical phrase and recovers that" },
            { "PLAY / STOP",    "Auditions the capture at PREVIEW MIX" },
            { "Click the wave", "Seeks; drag the red handles to trim" },
            { "TRIM",           "Commits the handles - the trimmed span becomes the clip" },
            { "RENAME",         "Renames the selected keep in place" },
            { "SAVE WAV",       "Writes a 24-bit WAV to the selected destination" },
            { "DRAG TO DAW",    "Drags the capture straight into your project" },
            { "Destinations",   "Right-click any of them to choose its folder" },
            { "Recovery tools", "Applied as a capture is made, not afterwards" },
            { "SAVE / arrows",  "Saves and browses presets" },
            { "UNDO / REDO",    "Covers delete, rename, favourite and capture" },
            { "After a KEEP",   "The buffer restarts, so the clock reads time since" },
            { "Buffer full?",   "It never fills - it rolls. AVAILABLE 8:00 means you" },
            { "",               "have the last 8 minutes. SETTINGS > CLEAR BUFFER restarts it" },
        };

        for (const auto& r : rows)
            line (g, body, r[0], r[1]);
    }

private:
    TileButton closeButton { "Close", {}, "CLOSE" };
};

} // namespace keepthat
