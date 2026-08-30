/*
    Widgets.h - the controls every panel shares.

    Nothing here is a stock JUCE widget with a colour override; the QA criteria
    reject visible default buttons, sliders and combo boxes outright. Each
    control paints itself from paint.h primitives and carries the four states
    the spec requires - normal, hover, pressed/selected, disabled - plus the
    hover fade and click ripple that keep the interface feeling alive.

    Hover animation runs on a per-control timer that stops itself once the fade
    settles, so an idle editor is not burning frames on sixty static buttons.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Paint.h"
#include "Icons.h"
#include "../Assets.h"

namespace keepthat
{

// -----------------------------------------------------------------------------
//  HoverGlow - 120 ms hover fade plus a 260 ms click ripple.
// -----------------------------------------------------------------------------
struct HoverGlow
{
    float amount = 0.0f;
    float ripple = -1.0f;
    juce::Point<float> rippleCentre;

    bool tick (bool over, double dt)
    {
        const float target = over ? 1.0f : 0.0f;
        const float step = (float) (dt * 1000.0 / 120.0);
        bool moving = false;

        if (std::abs (target - amount) > 0.001f)
        {
            amount = juce::jlimit (0.0f, 1.0f, amount + (target > amount ? step : -step));
            moving = true;
        }
        if (ripple >= 0.0f)
        {
            ripple += (float) (dt * 1000.0 / 260.0);
            if (ripple >= 1.0f) ripple = -1.0f;
            moving = true;
        }
        return moving;
    }

    void start (juce::Point<float> centre) { ripple = 0.0f; rippleCentre = centre; }

    void drawRipple (juce::Graphics& g, juce::Colour accent) const
    {
        if (ripple < 0.0f)
            return;
        const float r = 5.0f + ripple * 40.0f;
        g.setColour (accent.withAlpha ((1.0f - ripple) * 0.32f));
        g.drawEllipse (rippleCentre.x - r, rippleCentre.y - r, r * 2.0f, r * 2.0f, 1.8f);
    }
};

// -----------------------------------------------------------------------------
//  AnimatedButton - the hover/press plumbing, shared by every button below.
// -----------------------------------------------------------------------------
class AnimatedButton : public juce::Button, private juce::Timer
{
public:
    explicit AnimatedButton (const juce::String& name) : juce::Button (name)
    {
        setTitle (name);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setAccent (juce::Colour c) { accent = c; repaint(); }
    juce::Colour getAccent() const  { return accent; }

    /** Right-click (or ctrl-click on macOS). Used for the secondary action on
        a control that already has an obvious primary one. */
    std::function<void()> onSecondaryClick;

    /** Fires while the button is HELD DOWN and the mouse moves - which is the
        only gesture a file drag can use.

        A drag hung off `onClick` starts on mouse-UP: by then the user has let
        go, the pointer is back over the plug-in, and there is nothing left to
        drag onto a track. Everyone expects to press the thing and pull it out,
        so that is what this does. */
    std::function<void (juce::Component*)> onDragOut;

    void mouseEnter (const juce::MouseEvent& e) override { juce::Button::mouseEnter (e); wake(); }
    void mouseExit  (const juce::MouseEvent& e) override { juce::Button::mouseExit (e);  wake(); }
    void mouseDown  (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onSecondaryClick != nullptr)
        {
            onSecondaryClick();
            return;                       // not also a primary click
        }
        dragStarted = false;
        glow.start (e.position);
        wake();
        juce::Button::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // A few pixels of slop so a press with a shaky hand is still a click.
        if (onDragOut != nullptr && ! dragStarted && e.getDistanceFromDragStart() > 5)
        {
            dragStarted = true;
            setState (buttonNormal);      // the OS owns the pointer from here
            onDragOut (this);
            return;
        }
        juce::Button::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragStarted)
        {
            // Letting go at the end of a drag must not also fire the click.
            dragStarted = false;
            setState (buttonNormal);
            wake();
            return;
        }
        juce::Button::mouseUp (e);
    }

protected:
    HoverGlow glow;
    bool dragStarted = false;
    juce::Colour accent { tokens::accentRed };

private:
    void wake() { if (! isTimerRunning()) startTimerHz (60); }

    void timerCallback() override
    {
        if (! glow.tick (isMouseOver(), 1.0 / 60.0))
            stopTimer();
        repaint();
    }
};

// -----------------------------------------------------------------------------
//  HeaderIconButton - a glyph with a caption beneath (SAVE, SETTINGS, ...).
// -----------------------------------------------------------------------------
class HeaderIconButton : public AnimatedButton
{
public:
    HeaderIconButton (const juce::String& name, juce::Path glyph, juce::String captionIn,
                      juce::Colour tintIn = tokens::textSecond)
        : AnimatedButton (name), path (std::move (glyph)), caption (std::move (captionIn)),
          tint (tintIn)
    {
        setAccent (tintIn);
    }

    /** Base name of the supplied plate family, e.g. "blank" for
        buttons/blank_normal.png. Empty means draw the vector glyph instead. */
    void setAssetBase (juce::String base) { assetBase = std::move (base); repaint(); }

    /** Icon drawn on the plate, from the PNG_64 set. */
    void setIconAsset (juce::String name) { iconAsset = std::move (name); repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const bool lit = over || down || getToggleState();
        auto r = getLocalBounds().toFloat();

        // The supplied art is a whole button - plate, bevel and glyph - so it
        // gets the full cell above the caption. Insetting it left a small dark
        // square that read as an empty box rather than a control.
        auto iconArea = r.removeFromTop (r.getHeight() - 13.0f);
        const float size = juce::jmin (iconArea.getWidth(), iconArea.getHeight());
        iconArea = iconArea.withSizeKeepingCentre (size, size);

        // A plate in the transport's style, plus the icon on top.
        //
        // NOT the pack's own HEADER_UTILITY slices: like every BLANK_SHELLS
        // asset those are the striped kind, and at this size the stripes read
        // as lines scored through the button. buttons/blank_*.png is derived
        // from the transport art instead, so these match PLAY and STOP.
        if (componentArtwork() && assetBase.isNotEmpty())
        {
            const juce::String state = (down || getToggleState()) ? "active" : "normal";
            if (Assets::drawFittedTrimmed (g, art::button (assetBase, state), iconArea))
            {
                if (iconAsset.isNotEmpty())
                {
                    const float d = iconArea.getWidth() * 0.46f;
                    Assets::drawFittedTrimmed (g, art::icon (iconAsset),
                                               iconArea.withSizeKeepingCentre (d, d),
                                               isEnabled() ? 1.0f : 0.35f);
                }
                if (glow.amount > 0.01f)
                {
                    g.setColour (tint.withAlpha (glow.amount * 0.13f));
                    g.fillRoundedRectangle (iconArea.reduced (4.0f), 8.0f);
                }
                glow.drawRipple (g, tint);

                g.setFont (Fonts::iconCaption());
                g.setColour ((lit ? tint.brighter (0.4f) : tokens::textMuted)
                                 .withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f));
                g.drawText (caption, getLocalBounds().removeFromBottom (13),
                            juce::Justification::centred, false);
                return;
            }
        }

        const auto colour = lit ? tint.brighter (0.55f) : tint;
        if (lit)
            icons::drawGlowing (g, path, iconArea.reduced (down ? 1.5f : 0.0f), colour, 0.5f);
        else
            icons::draw (g, path, iconArea, colour, isEnabled() ? 0.92f : 0.35f);

        glow.drawRipple (g, tint);

        g.setFont (Fonts::iconCaption());
        g.setColour (lit ? tint.brighter (0.4f) : tokens::textMuted);
        g.drawText (caption, getLocalBounds().removeFromBottom (14),
                    juce::Justification::centred, false);
    }

private:
    juce::Path path;
    juce::String caption, assetBase, iconAsset;
    juce::Colour tint;
};

// -----------------------------------------------------------------------------
//  TileButton - icon over caption inside a tile. Transport, capture actions
//  and the export destination grid are all this control.
// -----------------------------------------------------------------------------
class TileButton : public AnimatedButton
{
public:
    enum class Layout { iconLeft, iconAbove };

    TileButton (const juce::String& name, juce::Path glyph, juce::String captionIn,
                Layout layoutIn = Layout::iconAbove)
        : AnimatedButton (name), path (std::move (glyph)), caption (std::move (captionIn)),
          layout (layoutIn)
    {
        setClickingTogglesState (false);
    }

    void setSelected (bool s) { selected = s; repaint(); }
    bool isSelected() const   { return selected; }
    void setCornerSize (float c) { corner = c; repaint(); }
    void setCaptionHeight (float h) { captionHeight = h; repaint(); }

    /** Supplied state family, e.g. "transport_play" or "export_button". */
    void setAssetBase (juce::String base) { assetBase = std::move (base); repaint(); }
    /** Supplied icon drawn over the shell, e.g. "playlist". */
    void setIconAsset (juce::String name) { iconAsset = std::move (name); repaint(); }

    /** True when the art already contains this button's text. v1.4's action
        and destination slices do, so drawing the caption as well printed every
        label twice and shrank the art to make room for the duplicate. */
    void setArtHasLabel (bool has) { artHasLabel = has; repaint(); }

    /** Draws a blank shell stretched across the whole button with the caption
        centred on it, rather than a fitted icon with a caption underneath.
        This is how the approved art draws MUTE, and it is the right treatment
        for any blank plate that has to fill a slot of its own shape. */
    void setCaptionInside (bool inside) { captionInside = inside; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float lift = glow.amount * 0.5f + (selected ? 1.0f : 0.0f);

        if (componentArtwork() && assetBase.isNotEmpty() && paintFromArt (g, over, down))
            return;

        paint::panelSurface (g, r, corner,
                             tokens::panel3.brighter (0.04f * lift),
                             tokens::panel2,
                             selected ? accent : tokens::stroke);

        if (selected)
        {
            // Bright outline + internal glow, per the spec's destination rule.
            juce::Path outline;
            outline.addRoundedRectangle (r.reduced (0.5f), corner);
            paint::glowPath (g, outline, accent, 1.6f, 5.0f, 0.85f, 3);

            juce::ColourGradient inner (accent.withAlpha (0.16f), r.getCentreX(), r.getBottom(),
                                        accent.withAlpha (0.0f), r.getCentreX(), r.getY(), false);
            g.setGradientFill (inner);
            g.fillRoundedRectangle (r.reduced (1.5f), corner);
        }
        else if (glow.amount > 0.01f)
        {
            g.setColour (accent.withAlpha (glow.amount * 0.09f));
            g.fillRoundedRectangle (r.reduced (1.0f), corner);
        }

        if (down)
        {
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRoundedRectangle (r.reduced (1.0f), corner);
        }

        glow.drawRipple (g, accent);

        const auto tint = selected ? accent
                        : (over ? tokens::textPrimary : tokens::textSecond);
        const float alpha = isEnabled() ? 1.0f : 0.35f;

        if (layout == Layout::iconAbove)
        {
            auto body = r.reduced (4.0f);
            auto capArea = body.removeFromBottom (caption.isEmpty() ? 0.0f : 14.0f);
            const float s = juce::jmin (body.getWidth(), body.getHeight()) * 0.78f;
            auto iconArea = body.withSizeKeepingCentre (s, s).translated (0.0f, down ? 1.0f : 0.0f);

            if (selected) icons::drawGlowing (g, path, iconArea, tint, 0.55f);
            else          icons::draw (g, path, iconArea, tint, alpha);

            if (caption.isNotEmpty())
            {
                g.setFont (Fonts::iconCaption().withHeight (captionHeight));
                g.setColour (tint.withMultipliedAlpha (alpha));
                // Fitted, not clipped: "DRAG TO DAW" has to survive a 62 px column.
                g.drawFittedText (caption, capArea.toNearestInt(),
                                  juce::Justification::centred, 1, 0.75f);
            }
        }
        else
        {
            auto body = r.reduced (10.0f, 4.0f);
            auto iconArea = body.removeFromLeft (body.getHeight() * 0.85f)
                                .withSizeKeepingCentre (body.getHeight() * 0.68f,
                                                        body.getHeight() * 0.68f);
            if (selected) icons::drawGlowing (g, path, iconArea, tint, 0.55f);
            else          icons::draw (g, path, iconArea, tint, alpha);

            g.setFont (Fonts::buttonLabel());
            g.setColour (tint.withMultipliedAlpha (alpha));
            g.drawText (caption, body.toNearestInt().translated (4, 0),
                        juce::Justification::centredLeft, false);
        }
    }

private:
    /** Shell art + optional supplied icon + live caption. Returns false when
        the family is missing so the procedural path takes over. */
    bool paintFromArt (juce::Graphics& g, bool over, bool down)
    {
        juce::String state = ! isEnabled() ? "disabled"
                           : down          ? "pressed"
                           : selected      ? "selected"
                           : over          ? "hover" : "normal";

        // Not every family ships every state, and they do not all use the
        // same word: transport calls its lit state "active". Probe quietly and
        // fall back rather than logging a miss that is not a fault.
        if (! Assets::exists (art::button (assetBase, state)))
            if (Assets::exists (art::button (assetBase, "active"))
                && (state == "selected" || state == "pressed"))
                state = "active";
        if (! Assets::exists (art::button (assetBase, state)))
            state = "normal";
        if (! Assets::exists (art::button (assetBase, state)))
            return false;

        auto full = getLocalBounds().toFloat();

        if (captionInside)
        {
            Assets::drawStretchedTrimmed (g, art::button (assetBase, state), full);
            glow.drawRipple (g, accent);
            g.setFont (Fonts::buttonLabel());
            g.setColour ((selected ? accent : tokens::textPrimary)
                             .withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
            g.drawFittedText (caption, full.toNearestInt(),
                              juce::Justification::centred, 1, 0.7f);
            return true;
        }

        auto shell = full;
        if (caption.isNotEmpty() && ! artHasLabel)
            shell = layout == Layout::iconAbove
                      ? full.withTrimmedBottom (13.0f)
                      // Square the art off on the left so the label has its own
                      // room; drawing it over the button made both unreadable.
                      : full.removeFromLeft (full.getHeight()).reduced (2.0f);
        Assets::drawFittedTrimmed (g, art::button (assetBase, state), shell, 1.0f, 0.04f);

        if (iconAsset.isNotEmpty())
        {
            const float d = juce::jmin (shell.getWidth(), shell.getHeight()) * 0.44f;
            // v1.4 stages the action glyphs beside the tiles; fall back to
            // the PNG_64 icon family for anything not in that set.
            auto path = art::tile (iconAsset);
            if (! Assets::exists (path))
                path = art::icon (iconAsset);
            Assets::drawFittedTrimmed (g, path, shell.withSizeKeepingCentre (d, d));
        }

        glow.drawRipple (g, accent);

        if (artHasLabel)
            return true;

        if (caption.isNotEmpty() && layout == Layout::iconAbove)
        {
            g.setFont (Fonts::iconCaption().withHeight (captionHeight));
            g.setColour ((selected ? accent : tokens::textSecond)
                             .withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
            g.drawFittedText (caption, full.removeFromBottom (13.0f).toNearestInt(),
                              juce::Justification::centred, 1, 0.75f);
        }
        else if (caption.isNotEmpty())
        {
            g.setFont (Fonts::buttonLabel());
            g.setColour ((selected ? accent : tokens::textSecond)
                             .withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
            g.drawFittedText (caption, full.withTrimmedLeft (4.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.8f);
        }
        return true;
    }

    juce::Path path;
    juce::String caption, assetBase, iconAsset;
    Layout layout;
    bool artHasLabel = false, captionInside = false;
    bool selected = false;
    float corner = 8.0f;
    float captionHeight = 10.5f;
};

// -----------------------------------------------------------------------------
//  LengthButton - the capture-size selectors. Text only; selection glows red.
// -----------------------------------------------------------------------------
class LengthButton : public AnimatedButton
{
public:
    LengthButton (const juce::String& text, juce::String assetIdIn)
        : AnimatedButton (text), label (text), assetId (std::move (assetIdIn)) {}

    void setSelected (bool s) { selected = s; repaint(); }
    bool isSelected() const   { return selected; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float corner = 5.0f;

        if (componentArtwork() && assetId.isNotEmpty())
        {
            // The v1.4 selectors ship one PNG per label in each of two states,
            // so the text is part of the art and must not be drawn again.
            if (Assets::drawFittedTrimmed (g, art::selector (assetId, selected),
                                           getLocalBounds().toFloat(), 1.0f, 0.05f))
            {
                if (glow.amount > 0.01f && ! selected)
                {
                    g.setColour (accent.withAlpha (glow.amount * 0.13f));
                    g.fillRoundedRectangle (r, corner);
                }
                if (down)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.25f));
                    g.fillRoundedRectangle (r, corner);
                }
                glow.drawRipple (g, accent);
                return;
            }
        }

        g.setGradientFill (juce::ColourGradient (
            selected ? juce::Colour (0xff2a1009) : tokens::panel3,
            r.getCentreX(), r.getY(),
            selected ? juce::Colour (0xff180a06) : tokens::panel2,
            r.getCentreX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);

        if (selected)
        {
            juce::Path outline;
            outline.addRoundedRectangle (r.reduced (0.5f), corner);
            paint::glowPath (g, outline, accent, 1.5f, 6.0f, 0.9f, 3);

            juce::ColourGradient inner (accent.withAlpha (0.20f), r.getCentreX(), r.getBottom(),
                                        accent.withAlpha (0.0f), r.getCentreX(), r.getY(), false);
            g.setGradientFill (inner);
            g.fillRoundedRectangle (r.reduced (1.5f), corner);
        }
        else
        {
            g.setColour (tokens::stroke.withAlpha (0.9f + glow.amount * 0.1f));
            g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
            if (glow.amount > 0.01f)
            {
                g.setColour (accent.withAlpha (glow.amount * 0.12f));
                g.fillRoundedRectangle (r.reduced (1.0f), corner);
                g.setColour (accent.withAlpha (glow.amount * 0.45f));
                g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
            }
        }

        if (down)
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (r.reduced (1.0f), corner);
        }
        glow.drawRipple (g, accent);

        const auto textColour = selected ? juce::Colour (0xffffd9cd)
                              : (over ? tokens::textPrimary : tokens::textSecond);
        if (selected)
            paint::glowText (g, label, getLocalBounds(), Fonts::buttonLabel(), textColour,
                             juce::Justification::centred, 0.55f);
        else
        {
            g.setFont (Fonts::buttonLabel());
            g.setColour (textColour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.35f));
            g.drawText (label, getLocalBounds(), juce::Justification::centred, false);
        }
    }

private:
    juce::String label, assetId;
    bool selected = false;
};

// -----------------------------------------------------------------------------
//  KeepLastButton - the focal point of the whole interface.
//
//  A chamfered slab with a red gradient face, a lit bevel, a bloom that
//  breathes while the buffer is armed, and a bright core outline. The breathing
//  is driven from the editor's animation clock, not a timer of its own, so it
//  stays in step with the HUD ring.
// -----------------------------------------------------------------------------
class KeepLastButton : public AnimatedButton
{
public:
    KeepLastButton() : AnimatedButton ("KEEP LAST") {}

    void setPulse (float p) { pulse = p; repaint(); }
    void setArmed (bool a)  { armed = a; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 3.0f);
        const float cut = r.getHeight() * 0.26f;
        auto face = paint::chamfered (r, cut);

        const float breathe = armed ? (0.78f + 0.22f * pulse) : 0.38f;
        const float lift = juce::jmax (glow.amount, down ? 1.0f : 0.0f);

        // v1.3 ships keep_last_normal / _hover / _pressed / _disabled with the
        // text and the whole housing already on them, so the button is the art
        // plus the one thing art cannot do: breathe while armed.
        if (componentArtwork())
        {
            const juce::String state = ! armed ? "disabled"
                                     : down    ? "pressed"
                                     : over    ? "hover" : "normal";
            auto full = getLocalBounds().toFloat();
            if (Assets::drawFittedTrimmed (g, art::button ("keep_last", state), full, 1.0f, 0.02f))
            {
                if (armed && breathe > 0.001f)
                {
                    g.setColour (tokens::accentRed.withAlpha (0.12f * breathe));
                    g.fillPath (paint::chamfered (full.reduced (6.0f, 8.0f),
                                                  full.getHeight() * 0.26f));
                }
                glow.drawRipple (g, tokens::redHot);
                return;
            }
        }

        // --- the face -----------------------------------------------------
        // Measured off the approved art: #590202 / #610301 - a very dark
        // maroon. The button's light comes from its OUTLINE, not from a filled
        // body; an earlier pass filled it bright orange-red and it read as a
        // completely different control.
        auto top = juce::Colour (0xff6b0403);
        auto bot = juce::Colour (0xff3a0101);
        if (down)            { top = juce::Colour (0xff4a0202); bot = juce::Colour (0xff260000); }
        else if (lift > 0.0f) { top = top.brighter (0.16f * lift); bot = bot.brighter (0.12f * lift); }

        g.setGradientFill (juce::ColourGradient (top, r.getCentreX(), r.getY(),
                                                 bot, r.getCentreX(), r.getBottom(), false));
        g.fillPath (face);

        // Inner bevel: a lit band just inside the top edge, and a darker one
        // along the bottom - the art has a clear rolled edge.
        g.saveState();
        g.reduceClipRegion (face, juce::AffineTransform());
        juce::ColourGradient bevel (juce::Colour (0xffb07a72).withAlpha (0.55f),
                                    r.getCentreX(), r.getY() + 2.0f,
                                    juce::Colours::transparentBlack,
                                    r.getCentreX(), r.getY() + r.getHeight() * 0.42f, false);
        g.setGradientFill (bevel);
        g.fillRect (r);

        juce::ColourGradient foot (juce::Colours::transparentBlack,
                                   r.getCentreX(), r.getBottom() - r.getHeight() * 0.3f,
                                   juce::Colours::black.withAlpha (0.5f),
                                   r.getCentreX(), r.getBottom(), false);
        g.setGradientFill (foot);
        g.fillRect (r);
        g.restoreState();

        // --- the outlines that actually light it --------------------------
        // Two traces: a white-hot core on the slab edge, and a second line a
        // few pixels outside it, which is the doubled edge in the reference.
        paint::glowPath (g, face, tokens::accentRed, 4.4f, 13.0f * breathe,
                         0.9f + 0.1f * lift, 5);

        juce::Path hot;
        hot.addPath (paint::chamfered (r.reduced (2.5f), cut - 2.0f));
        paint::glowPath (g, hot, juce::Colour (0xffffd8cc), 1.5f, 6.0f,
                         (0.75f + 0.25f * lift) * breathe, 3);

        auto outerR = getLocalBounds().toFloat();
        auto outline = paint::chamfered (outerR, outerR.getHeight() * 0.24f);
        paint::glowPath (g, outline, tokens::accentRed, 1.6f, 9.0f,
                         (0.55f + 0.2f * lift) * breathe, 4);

        glow.drawRipple (g, tokens::redHot);

        paint::glowText (g, "KEEP LAST", getLocalBounds().translated (0, down ? 2 : 0),
                         Fonts::keepLast(), juce::Colour (0xfffdf3ef),
                         juce::Justification::centred, 0.9f * breathe);
    }

private:
    float pulse = 0.0f;
    bool armed = true;
};

// -----------------------------------------------------------------------------
//  PillToggle - the recovery-tool switches.
// -----------------------------------------------------------------------------
class PillToggle : public AnimatedButton
{
public:
    explicit PillToggle (const juce::String& name) : AnimatedButton (name)
    {
        setClickingTogglesState (true);
    }

    void setCompact (bool c) { compact = c; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const float radius = r.getHeight() * 0.5f;
        const bool on = getToggleState();

        if (componentArtwork())
        {
            const juce::String base = compact ? "toggle_compact" : "toggle";
            juce::String path = "switches/" + base + juce::String (on ? "_on" : "_off")
                              + (! isEnabled() ? "_disabled" : (over && ! compact ? "_hover" : ""))
                              + ".png";
            if (! Assets::exists (path))
                path = "switches/" + base + juce::String (on ? "_on" : "_off") + ".png";
            if (Assets::drawFittedTrimmed (g, path, getLocalBounds().toFloat()))
            {
                glow.drawRipple (g, on ? accent : tokens::textSecond);
                return;
            }
        }

        // Track.
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3c1409),
                                                     r.getCentreX(), r.getY(),
                                                     juce::Colour (0xff1c0904),
                                                     r.getCentreX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);

            juce::Path outline;
            outline.addRoundedRectangle (r.reduced (0.5f), radius);
            paint::glowPath (g, outline, accent, 1.4f, 4.5f, 0.85f, 3);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff272e38),
                                                     r.getCentreX(), r.getY(),
                                                     juce::Colour (0xff161b22),
                                                     r.getCentreX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);
            g.setColour (tokens::stroke);
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        }

        if (glow.amount > 0.01f)
        {
            g.setColour ((on ? accent : tokens::textSecond).withAlpha (glow.amount * 0.13f));
            g.fillRoundedRectangle (r.reduced (0.5f), radius);
        }

        // Thumb.
        const float d = r.getHeight() - 5.0f;
        const float x = on ? r.getRight() - d - 2.5f : r.getX() + 2.5f;
        auto thumb = juce::Rectangle<float> (x, r.getY() + 2.5f, d, d);

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (thumb.translated (0.0f, 1.0f).expanded (0.6f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, thumb.getCentreX(),
                                                 thumb.getY(),
                                                 juce::Colour (0xffbfc7d1), thumb.getCentreX(),
                                                 thumb.getBottom(), false));
        g.fillEllipse (thumb);

        if (on)
        {
            g.setColour (accent.withAlpha (0.35f));
            g.drawEllipse (thumb.expanded (1.6f), 1.6f);
        }
        glow.drawRipple (g, on ? accent : tokens::textSecond);
    }

private:
    bool compact = false;
};

// -----------------------------------------------------------------------------
//  RotaryBase - the shared knob mechanics: drag handling and the arc geometry.
// -----------------------------------------------------------------------------
class RotaryBase : public juce::Component
{
public:
    std::function<void (float)> onValueChange;

    /** Gesture bounds, so host automation records a drag as one move rather
        than a burst of unrelated writes. */
    std::function<void()> onDragStart, onDragEnd;

    void setNormalised (float v, juce::NotificationType n = juce::dontSendNotification)
    {
        const float clamped = juce::jlimit (0.0f, 1.0f, v);
        if (std::abs (clamped - value) < 1.0e-6f)
            return;
        value = clamped;
        if (n == juce::sendNotification && onValueChange)
            onValueChange (value);
        repaint();
    }

    float getNormalised() const { return value; }
    void setBipolar (bool b)    { bipolar = b; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStart = value;
        e.source.enableUnboundedMouseMovement (true);
        setMouseCursor (juce::MouseCursor::NoCursor);
        if (onDragStart) onDragStart();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Vertical drag, finer with a modifier - the convention every producer
        // already has in their hands from every other plugin.
        const float speed = e.mods.isShiftDown() ? 0.0012f : 0.005f;
        setNormalised (dragStart - e.getDistanceFromDragStartY() * speed,
                       juce::sendNotification);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        e.source.enableUnboundedMouseMovement (false);
        setMouseCursor (juce::MouseCursor::NormalCursor);
        if (onDragEnd) onDragEnd();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onDragStart) onDragStart();
        setNormalised (defaultValue, juce::sendNotification);
        if (onDragEnd) onDragEnd();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (onDragStart) onDragStart();
        setNormalised (value + w.deltaY * 0.4f, juce::sendNotification);
        if (onDragEnd) onDragEnd();
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

    void setDefault (float v) { defaultValue = juce::jlimit (0.0f, 1.0f, v); }

    /** The supplied filmstrip for this knob, if any. When set and loadable it
        replaces every drawn layer of the knob. The frame count travels with
        the path so the two cannot drift apart. */
    void setFilmstrip (juce::String path, int frames = art::knobFrames)
    {
        strip = std::move (path);
        stripFrames = juce::jmax (1, frames);
        repaint();
    }

protected:
    static constexpr float startAngle = -2.356f;    // 7 o'clock
    static constexpr float endAngle   =  2.356f;    // 5 o'clock

    float angleFor (float v) const { return startAngle + v * (endAngle - startAngle); }

    /** Draws the whole knob - track, arc, ticks, cap, pointer - into `area`. */
    void paintKnob (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour arcColour)
    {
        // The v1.3 filmstrips carry the ring, the indicator arc and the pointer
        // in one frame, so a strip replaces every layer below it.
        if (componentArtwork() && strip.isNotEmpty() && Assets::has (strip))
        {
            auto frame = Assets::filmstripFrame (strip, stripFrames, value);
            if (frame.isValid())
            {
                // A small bleed for the glow, not the 16 % it used to be: the
                // knob content fills 153 of its 160 px frame, so expanding by
                // a sixth put a ~94 px knob in a slot the approved art draws
                // at ~60, and it ran into the cell's title and range labels.
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (frame, area.expanded (area.getWidth() * 0.04f),
                             juce::RectanglePlacement::centred);
                if (hover)
                {
                    g.setColour (arcColour.withAlpha (0.13f));
                    g.drawEllipse (area.reduced (area.getWidth() * 0.06f), 2.0f);
                }
                return;
            }
        }

        const auto centre = area.getCentre();
        const float outer = area.getWidth() * 0.5f;
        const float trackR = outer - 1.5f;

        // Tick dots around the outside.
        g.setColour (tokens::stroke.brighter (0.15f));
        for (int i = 0; i <= 20; ++i)
        {
            const float a = angleFor (i / 20.0f) - juce::MathConstants<float>::halfPi;
            const float rr = trackR + 1.0f;
            const auto pt = centre.getPointOnCircumference (rr, a + juce::MathConstants<float>::halfPi);
            g.fillEllipse (pt.x - 0.9f, pt.y - 0.9f, 1.8f, 1.8f);
        }

        // Unlit track.
        auto track = paint::arcPath (centre, trackR - 4.0f, startAngle, endAngle);
        g.setColour (juce::Colour (0xff1a2029));
        g.strokePath (track, juce::PathStrokeType (3.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Lit arc.
        const float from = bipolar ? angleFor (0.5f) : startAngle;
        const float to = angleFor (value);
        if (std::abs (to - from) > 0.004f)
        {
            auto lit = paint::arcPath (centre, trackR - 4.0f,
                                       juce::jmin (from, to), juce::jmax (from, to));
            paint::glowPath (g, lit, arcColour, 3.0f, 5.0f, hover ? 0.95f : 0.8f, 3);
        }

        // Cap.
        const float capR = trackR - 9.0f;
        auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);
        paint::metalCap (g, cap);

        // Pointer.
        const float a = angleFor (value) - juce::MathConstants<float>::halfPi;
        const auto tip  = centre.getPointOnCircumference (capR - 2.0f, a + juce::MathConstants<float>::halfPi);
        const auto tail = centre.getPointOnCircumference (capR * 0.30f, a + juce::MathConstants<float>::halfPi);
        juce::Path pointer;
        pointer.startNewSubPath (tail);
        pointer.lineTo (tip);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.strokePath (pointer, juce::PathStrokeType (3.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        paint::glowPath (g, pointer, arcColour.brighter (0.25f), 1.8f, 3.5f, 0.95f, 2);

        if (hover)
        {
            g.setColour (arcColour.withAlpha (0.14f));
            g.drawEllipse (cap.expanded (3.5f), 2.0f);
        }
    }

    float value = 0.5f, defaultValue = 0.5f, dragStart = 0.5f;
    bool bipolar = false, hover = false;
    juce::String strip;
    int stripFrames = art::knobFrames;
};

// -----------------------------------------------------------------------------
//  MacroKnob - a full bottom-strip cell: title, knob, value, range labels.
// -----------------------------------------------------------------------------
class MacroKnob : public RotaryBase
{
public:
    MacroKnob (juce::String titleIn, juce::String minLabelIn, juce::String maxLabelIn)
        : title (std::move (titleIn)), minLabel (std::move (minLabelIn)),
          maxLabel (std::move (maxLabelIn))
    {
        setTitle (title);
    }

    /** Formats the current value; set by the owner so units stay in one place. */
    std::function<juce::String (float)> formatValue;

    void setValueTint (juce::Colour c) { valueTint = c; repaint(); }
    void setArcColour (juce::Colour c) { arcColour = c; repaint(); }
    void setShowValue (bool s)         { showValue = s; repaint(); }

    /** Puts the value between the range labels instead of beside the knob -
        what the approved art does for OUTPUT, whose cell has no room to its
        right before the meters start. */
    void setValueInFooter (bool f)     { valueInFooter = f; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setFont (Fonts::knobTitle());
        g.setColour (tokens::textSecond);
        g.drawText (title, r.removeFromTop (24.0f).toNearestInt(),
                    juce::Justification::centred, false);

        auto body = r.reduced (6.0f, 0.0f).withTrimmedBottom (18.0f);

        // Capped, not just fitted. Filling the cell's height makes the knob
        // collide with the title above and the range labels below - the
        // approved art leaves clear air around it.
        const float d = juce::jmin (62.0f,
                                    juce::jmin (body.getHeight(), body.getWidth() * 0.56f));
        auto knobArea = juce::Rectangle<float> (d, d)
                            .withCentre ({ body.getCentreX(), body.getCentreY() });
        paintKnob (g, knobArea, arcColour);

        if (showValue && formatValue && ! valueInFooter)
        {
            auto valueArea = body.withLeft (knobArea.getRight() + 6.0f);
            g.setFont (Fonts::knobValue());
            g.setColour (valueTint);
            g.drawText (formatValue (value), valueArea.toNearestInt(),
                        juce::Justification::centredLeft, false);
        }

        auto foot = getLocalBounds().toFloat().removeFromBottom (18.0f).reduced (8.0f, 0.0f);
        g.setFont (Fonts::knobRange());
        g.setColour (tokens::textMuted.withAlpha (0.8f));
        g.drawText (minLabel, foot.toNearestInt(), juce::Justification::centredLeft, false);
        g.drawText (maxLabel, foot.toNearestInt(), juce::Justification::centredRight, false);

        if (showValue && formatValue && valueInFooter)
        {
            g.setFont (Fonts::knobValue().withHeight (14.0f));
            g.setColour (valueTint);
            g.drawText (formatValue (value), foot.toNearestInt(),
                        juce::Justification::centred, false);
        }
    }

private:
    juce::String title, minLabel, maxLabel;
    juce::Colour valueTint { tokens::textPrimary };
    juce::Colour arcColour { tokens::accentRed };
    bool showValue = true, valueInFooter = false;
};

// -----------------------------------------------------------------------------
//  MiniKnob - the compact dials inside the FADE IN / OUT row.
// -----------------------------------------------------------------------------
class MiniKnob : public RotaryBase
{
public:
    MiniKnob (juce::String captionIn, juce::String unitIn)
        : caption (std::move (captionIn)), unit (std::move (unitIn)) {}

    std::function<juce::String (float)> formatValue;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto labels = r.removeFromLeft (r.getWidth() * 0.44f);
        const float d = juce::jmin (r.getWidth(), r.getHeight()) - 2.0f;
        auto knobArea = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
        paintKnob (g, knobArea, tokens::accentRed);

        g.setFont (Fonts::tiny());
        g.setColour (tokens::textMuted);
        g.drawText (caption, labels.removeFromTop (labels.getHeight() * 0.5f).toNearestInt(),
                    juce::Justification::centredBottom, false);
        g.setFont (Fonts::small());
        g.setColour (tokens::textSecond);
        g.drawText (formatValue ? formatValue (value) : unit, labels.toNearestInt(),
                    juce::Justification::centredTop, false);
    }

private:
    juce::String caption, unit;
};

// -----------------------------------------------------------------------------
//  ComboField - the source selector and the TIME / BARS:BEATS switch.
// -----------------------------------------------------------------------------
class ComboField : public AnimatedButton
{
public:
    explicit ComboField (const juce::String& name) : AnimatedButton (name)
    {
        setAccent (tokens::accentCyan);
    }

    void setText (juce::String t) { text = std::move (t); repaint(); }
    juce::String getText() const  { return text; }
    void setFontToUse (juce::Font f) { font = std::move (f); repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        paint::wellSurface (g, r, 4.0f, tokens::panel2);

        if (glow.amount > 0.01f)
        {
            g.setColour (accent.withAlpha (glow.amount * 0.30f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }
        if (down)
        {
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRoundedRectangle (r, 4.0f);
        }

        auto body = r.reduced (9.0f, 0.0f);
        auto arrowArea = body.removeFromRight (18.0f);

        g.setFont (font);
        g.setColour (over ? tokens::textPrimary : tokens::textSecond);
        g.drawText (text, body.toNearestInt(), juce::Justification::centredLeft, false);

        icons::draw (g, icons::chevron (juce::MathConstants<float>::halfPi),
                     arrowArea.withSizeKeepingCentre (11.0f, 11.0f),
                     over ? tokens::textPrimary : tokens::textMuted);
    }

private:
    juce::String text;
    juce::Font font { Fonts::small() };
};

// -----------------------------------------------------------------------------
//  Small painted primitives the panels reuse.
// -----------------------------------------------------------------------------
namespace widgets
{
    /** A vertical LED meter column: cyan body, red above -6 dB, dim below. */
    inline void ledColumn (juce::Graphics& g, juce::Rectangle<float> r,
                           float level, float hold, int segments = 26)
    {
        if (componentArtwork() && Assets::has (art::meterV))
        {
            auto frame = Assets::filmstripFrame (art::meterV, 64, level);
            if (frame.isValid())
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (frame, r, juce::RectanglePlacement::stretchToFit);
                return;
            }
        }

        const float segH = r.getHeight() / (float) segments;
        const float gap = juce::jmax (0.8f, segH * 0.22f);
        const int litCount  = (int) std::round (level * segments);
        const int holdIndex = (int) std::round (hold * segments) - 1;

        for (int i = 0; i < segments; ++i)
        {
            const float y = r.getBottom() - (i + 1) * segH;
            auto seg = juce::Rectangle<float> (r.getX(), y, r.getWidth(), segH - gap);

            // Top four segments are the red zone; the rest cyan.
            const bool redZone = i >= segments - 4;
            const auto on = redZone ? tokens::accentRed : tokens::accentCyan;
            const bool lit = i < litCount;

            if (lit)
            {
                g.setColour (on.withAlpha (0.22f));
                g.fillRect (seg.expanded (1.4f, 0.6f));
                g.setColour (on);
                g.fillRect (seg);
            }
            else
            {
                g.setColour (on.withAlpha (redZone ? 0.16f : 0.13f));
                g.fillRect (seg);
            }

            if (i == holdIndex && holdIndex >= 0)
            {
                g.setColour (redZone ? tokens::accentRed.brighter (0.4f) : tokens::cyanPale);
                g.fillRect (seg.withHeight (juce::jmax (1.4f, segH * 0.34f)));
            }
        }
    }

    /** A horizontal LED meter row, as used by the OUTPUT section. */
    inline void ledRow (juce::Graphics& g, juce::Rectangle<float> r,
                        float level, float hold, int segments = 34)
    {
        if (componentArtwork() && Assets::has (art::meterH))
        {
            auto frame = Assets::filmstripFrame (art::meterH, 64, level);
            if (frame.isValid())
            {
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (frame, r, juce::RectanglePlacement::stretchToFit);
                return;
            }
        }

        const float segW = r.getWidth() / (float) segments;
        const float gap = juce::jmax (0.8f, segW * 0.26f);
        const int litCount  = (int) std::round (level * segments);
        const int holdIndex = (int) std::round (hold * segments) - 1;

        for (int i = 0; i < segments; ++i)
        {
            auto seg = juce::Rectangle<float> (r.getX() + i * segW, r.getY(),
                                               segW - gap, r.getHeight());
            const bool redZone = i >= segments - 3;
            const auto on = redZone ? tokens::accentRed : tokens::accentCyan;

            if (i < litCount)
            {
                g.setColour (on.withAlpha (0.22f));
                g.fillRect (seg.expanded (0.6f, 1.4f));
                g.setColour (on);
                g.fillRect (seg);
            }
            else
            {
                g.setColour (juce::Colour (0xff2b333d).withAlpha (redZone ? 0.9f : 0.75f));
                g.fillRect (seg);
            }

            if (i == holdIndex && holdIndex >= 0)
            {
                g.setColour (redZone ? tokens::accentRed.brighter (0.4f) : tokens::cyanPale);
                g.fillRect (seg.withWidth (juce::jmax (1.4f, segW * 0.4f)));
            }
        }
    }

    /** Panel title in the mockup's tracked, semi-bold treatment. */
    inline void panelTitle (juce::Graphics& g, const juce::String& text,
                            juce::Rectangle<int> area, juce::Colour colour = tokens::textPrimary)
    {
        g.setFont (Fonts::panelTitle());
        g.setColour (colour);
        g.drawText (text, area, juce::Justification::centredLeft, false);
    }

    /** A labelled readout cell: small caption over a large value. */
    inline void readout (juce::Graphics& g, juce::Rectangle<float> r,
                         const juce::String& label, const juce::String& value,
                         juce::Colour valueColour = tokens::textPrimary)
    {
        paint::wellSurface (g, r, 5.0f, tokens::panel2);
        auto body = r.reduced (6.0f, 7.0f);
        g.setFont (Fonts::fieldLabel());
        g.setColour (tokens::textMuted);
        g.drawText (label, body.removeFromTop (15.0f).toNearestInt(),
                    juce::Justification::centred, false);
        g.setFont (Fonts::fieldValue());
        g.setColour (valueColour);
        g.drawText (value, body.toNearestInt(), juce::Justification::centred, false);
    }
}

} // namespace keepthat
