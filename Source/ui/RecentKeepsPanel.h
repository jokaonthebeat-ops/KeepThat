/*
    RecentKeepsPanel.h - the horizontal clip browser.

    Eight cards fit the approved width; the list is allowed to grow past that,
    so the strip scrolls with the wheel or a horizontal trackpad swipe. The
    selected card is highlighted red and its waveform is redrawn in red - the
    mockup's cue that this is the clip the rest of the interface is showing.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Paint.h"
#include "Widgets.h"
#include "../PluginProcessor.h"
#include "../state/SessionHistory.h"
#include "../Assets.h"

namespace keepthat
{

class RecentKeepsPanel : public juce::Component
{
public:
    explicit RecentKeepsPanel (KeepThatProcessor& p) : processor (p) {}

    std::function<void (int)> onSelect;
    std::function<void()> onChanged;
    std::function<void (int)> onPlay;

    /** Turns a card's name into an editable field in place. A plugin should
        not open a modal dialog for something this small - hosts handle modals
        inconsistently and some will not show them at all. */
    void beginRename (int index)
    {
        auto& s = processor.session();
        if (index < 0 || index >= (int) s.keeps.size())
            return;

        renaming = index;
        editor = std::make_unique<juce::TextEditor>();
        editor->setText (s.keeps[(size_t) index].name, false);
        editor->selectAll();
        editor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff11161e));
        editor->setColour (juce::TextEditor::textColourId, tokens::textPrimary);
        editor->setColour (juce::TextEditor::outlineColourId, tokens::accentRed);
        editor->setColour (juce::TextEditor::focusedOutlineColourId, tokens::accentRed);
        editor->setFont (Fonts::cardName().withHeight (13.0f));
        editor->setJustification (juce::Justification::centredLeft);
        editor->onReturnKey = [this] { commitRename (true); };
        editor->onEscapeKey = [this] { commitRename (false); };
        editor->onFocusLost = [this] { commitRename (true); };

        auto card = cardBounds (index).reduced (7.0f, 5.0f).withHeight (18.0f);
        addAndMakeVisible (*editor);
        editor->setBounds (card.toNearestInt());
        editor->grabKeyboardFocus();
        repaint();
    }

    void update()
    {
        // Ease the strip toward its target when a scroll is in flight.
        const float target = juce::jlimit (0.0f, maxScroll(), scrollTarget);
        if (std::abs (target - scroll) > 0.4f)
        {
            scroll += (target - scroll) * 0.22f;
            repaint();
        }
        else if (scroll != target)
        {
            scroll = target;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = juce::Point<int> (-getX(), -getY());
        const auto& s = processor.session();

        paint::panelSurface (g, getLocalBounds().toFloat(), Design::corner);
        widgets::panelTitle (g, "RECENT KEEPS", layout::keepsTitle.translated (off.x, off.y));

        g.setFont (Fonts::small());
        g.setColour (tokens::textMuted);
        g.drawText (juce::String ((int) s.keeps.size()) + " / " + juce::String (s.keepCapacity),
                    layout::keepsCount.translated (off.x, off.y),
                    juce::Justification::centredRight, false);

        auto strip = stripBounds();

        if (s.keeps.empty())
        {
            g.setFont (Fonts::small().withHeight (13.0f));
            g.setColour (tokens::textMuted);
            g.drawText ("No keeps yet - press KEEP LAST to recover what you just played",
                        strip.toNearestInt(), juce::Justification::centred, false);
            return;
        }

        g.saveState();
        g.reduceClipRegion (strip.toNearestInt());
        for (int i = 0; i < (int) s.keeps.size(); ++i)
        {
            auto card = cardBounds (i);
            if (card.getRight() >= strip.getX() - 4.0f && card.getX() <= strip.getRight() + 4.0f)
                paintCard (g, card, s.keeps[(size_t) i], i == s.selectedKeep, i == hoverIndex);
        }
        g.restoreState();

        // Fade the strip edges when there is more to scroll to.
        paintScrollHint (g, strip);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int idx = indexAt (e.position);
        if (idx != hoverIndex) { hoverIndex = idx; repaint(); }
        hoverAction = actionAt (e.position);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverIndex = -1;
        hoverAction = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int idx = indexAt (e.position);
        if (idx < 0)
            return;

        auto& s = processor.session();
        const int action = actionAt (e.position);

        if (action == 2 && idx < (int) s.keeps.size())            // delete
        {
            // Undoable, and the file is deliberately NOT deleted here: the
            // whole point of this plugin is not losing takes, and undo has to
            // be able to bring the clip back intact. Temp files are swept at
            // shutdown anyway.
            processor.perform (new DeleteKeepAction (s, idx), "Delete keep");
            scrollTarget = juce::jlimit (0.0f, maxScroll(), scrollTarget);
            if (onChanged) onChanged();
        }
        else if (action == 1 && idx < (int) s.keeps.size())       // favourite
        {
            processor.perform (new FavouriteKeepAction (s, idx), "Favourite");
        }
        else if (action == 0)                                     // play
        {
            // Selecting first, so the transport, preview and export all act on
            // the clip the user just asked to hear.
            if (s.selectedKeep != idx)
            {
                s.selectedKeep = idx;
                if (onSelect) onSelect (idx);
            }
            if (onPlay) onPlay (idx);
        }
        else
        {
            s.selectedKeep = idx;
            if (onSelect) onSelect (idx);
        }
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float delta = (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY);
        scrollTarget = juce::jlimit (0.0f, maxScroll(), scrollTarget - delta * 260.0f);
    }

private:
    void commitRename (bool keepIt)
    {
        if (editor == nullptr)
            return;

        auto& s = processor.session();
        const auto text = editor->getText().trim();
        if (keepIt && text.isNotEmpty() && renaming >= 0
            && renaming < (int) s.keeps.size())
        {
            processor.perform (new RenameKeepAction (s, renaming, text), "Rename keep");
            if (onChanged) onChanged();
        }

        // Deleted asynchronously: this can be called from the editor's own
        // focus-lost callback, and destroying it inside that is a use-after-free.
        auto dead = std::move (editor);
        editor.reset();
        renaming = -1;
        juce::MessageManager::callAsync ([owned = std::shared_ptr<juce::TextEditor> (std::move (dead))] {});
        repaint();
    }

    static constexpr float cardGap = 9.0f;

    juce::Rectangle<float> stripBounds() const
    {
        return layout::keepsStrip.translated (-getX(), -getY()).toFloat();
    }

    float cardWidth() const
    {
        auto strip = stripBounds();
        return (strip.getWidth() - cardGap * (layout::keepsVisible - 1))
                 / (float) layout::keepsVisible;
    }

    juce::Rectangle<float> cardBounds (int index) const
    {
        auto strip = stripBounds();
        const float w = cardWidth();
        return { strip.getX() + index * (w + cardGap) - scroll, strip.getY(),
                 w, strip.getHeight() };
    }

    float maxScroll() const
    {
        const int n = (int) processor.session().keeps.size();
        const float total = n * cardWidth() + juce::jmax (0, n - 1) * cardGap;
        return juce::jmax (0.0f, total - stripBounds().getWidth());
    }

    int indexAt (juce::Point<float> p) const
    {
        if (! stripBounds().contains (p))
            return -1;
        for (int i = 0; i < (int) processor.session().keeps.size(); ++i)
            if (cardBounds (i).contains (p))
                return i;
        return -1;
    }

    /** 0 = play, 1 = favourite, 2 = delete, -1 = none. */
    int actionAt (juce::Point<float> p) const
    {
        const int idx = indexAt (p);
        if (idx < 0)
            return -1;
        auto card = cardBounds (idx);
        auto row = card.removeFromBottom (24.0f).reduced (8.0f, 2.0f);
        for (int i = 0; i < 3; ++i)
            if (juce::Rectangle<float> (row.getX() + i * (row.getWidth() / 3.0f), row.getY(),
                                        row.getWidth() / 3.0f, row.getHeight()).contains (p))
                return i;
        return -1;
    }

    void paintCard (juce::Graphics& g, juce::Rectangle<float> r, const CaptureClip& clip,
                    bool selected, bool hovered)
    {
        const auto accent = selected ? tokens::accentRed : tokens::stroke;

        if (componentArtwork())
        {
            // v1.4 ships two card states; hover and favourite are expressed
            // live on top of them rather than as separate art.
            const char* tileName = selected ? "recent_keep_selected"
                                            : "recent_keep_default";
            if (Assets::drawStretchedTrimmed (g, art::tile (tileName), r))
            {
                // The tile art has a decorative waveform and header bar baked
                // in. A card has to show ITS clip, so the interior is knocked
                // back and redrawn - the art keeps the frame, bevel and the
                // selected/favourite treatment, which is what carries the look.
                g.setColour (juce::Colour (0xff090d13).withAlpha (0.93f));
                g.fillRoundedRectangle (r.reduced (6.0f, 5.0f), 4.0f);
                paintCardContents (g, r, clip, selected, hovered);
                return;
            }
        }

        paint::panelSurface (g, r, 7.0f,
                             selected ? juce::Colour (0xff1d0e0a) : juce::Colour (0xff11161e),
                             selected ? juce::Colour (0xff100706) : juce::Colour (0xff0b0f15),
                             accent);
        if (selected)
        {
            juce::Path outline;
            outline.addRoundedRectangle (r.reduced (0.5f), 7.0f);
            paint::glowPath (g, outline, tokens::accentRed, 1.2f, 5.0f, 0.7f, 3);
        }
        else if (hovered)
        {
            g.setColour (tokens::accentCyan.withAlpha (0.07f));
            g.fillRoundedRectangle (r.reduced (1.0f), 7.0f);
            g.setColour (tokens::strokeHi);
            g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        }

        paintCardContents (g, r, clip, selected, hovered);
    }

    void paintCardContents (juce::Graphics& g, juce::Rectangle<float> r,
                            const CaptureClip& clip, bool selected, bool hovered)
    {
        auto body = r.reduced (7.0f, 5.0f);

        // Name + favourite star.
        auto nameRow = body.removeFromTop (16.0f);
        auto starArea = nameRow.removeFromRight (16.0f);
        g.setFont (Fonts::cardName());
        g.setColour (selected ? juce::Colour (0xffffd9cd) : tokens::textSecond);
        g.drawText (clip.name, nameRow.toNearestInt(), juce::Justification::centredLeft, false);

        if (clip.favourite)
            icons::drawGlowing (g, icons::star(), starArea.withSizeKeepingCentre (13.0f, 13.0f),
                                tokens::accentRed, 0.5f);
        else
            icons::draw (g, icons::star(), starArea.withSizeKeepingCentre (13.0f, 13.0f),
                         tokens::textMuted, 0.55f);

        // Waveform.
        auto wave = body.removeFromTop (body.getHeight() - 24.0f).reduced (0.0f, 3.0f);
        if (! clip.thumbLo.empty())
            paint::waveform (g, wave, clip.thumbLo.data(), clip.thumbHi.data(),
                             (int) clip.thumbLo.size(),
                             selected ? tokens::accentRed : tokens::textMuted.withAlpha (0.85f),
                             0.9f, selected);

        // Duration, bottom-right of the waveform block.
        g.setFont (Fonts::tiny());
        g.setColour (selected ? tokens::accentRed.brighter (0.3f) : tokens::textSecond);
        g.drawText (clip.durationText(), wave.toNearestInt().translated (0, 4),
                    juce::Justification::bottomRight, false);

        // Action row.
        auto row = body.reduced (1.0f, 2.0f);
        const float cw = row.getWidth() / 3.0f;
        const juce::Path glyphs[3] = { icons::play(), icons::heart(), icons::trash() };
        for (int i = 0; i < 3; ++i)
        {
            auto cellArea = juce::Rectangle<float> (row.getX() + i * cw, row.getY(), cw,
                                                    row.getHeight())
                                .withSizeKeepingCentre (14.0f, 14.0f);
            const bool lit = hovered && hoverAction == i;

            // The pack's favorite/delete slices are whole buttons, which would
            // be far too heavy inside a card, so the row keeps its glyphs.
            icons::draw (g, glyphs[i], cellArea,
                         lit ? tokens::textPrimary : tokens::textMuted, lit ? 1.0f : 0.7f);
        }
    }

    void paintScrollHint (juce::Graphics& g, juce::Rectangle<float> strip)
    {
        if (scroll > 1.0f)
        {
            juce::ColourGradient left (tokens::panel.withAlpha (0.95f), strip.getX(), 0.0f,
                                       tokens::panel.withAlpha (0.0f), strip.getX() + 26.0f,
                                       0.0f, false);
            g.setGradientFill (left);
            g.fillRect (strip.withWidth (26.0f));
        }
        if (scroll < maxScroll() - 1.0f)
        {
            juce::ColourGradient right (tokens::panel.withAlpha (0.0f), strip.getRight() - 26.0f,
                                        0.0f, tokens::panel.withAlpha (0.95f), strip.getRight(),
                                        0.0f, false);
            g.setGradientFill (right);
            g.fillRect (strip.withLeft (strip.getRight() - 26.0f));
        }
    }

    KeepThatProcessor& processor;
    float scroll = 0.0f, scrollTarget = 0.0f;
    int hoverIndex = -1, hoverAction = -1;
    int renaming = -1;
    std::unique_ptr<juce::TextEditor> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecentKeepsPanel)
};

} // namespace keepthat
