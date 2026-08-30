/*
    BufferHudPanel.h - the rolling-buffer HUD, the KEEP LAST action and the two
    rows of capture-length selectors.

    The ring is five concentric layers, all drawn live. Their radii were
    measured off the approved mockup (see layout::r* in Theme.h):

        200..216   the dominant outer arc - red left, cyan right
        184..198   sparse tick ring, between the two big arcs
        170..182   the mid arc
        155..159   a thin accent line
        136..147   the inner bright arc
        118..132   a fine dashed segment ring
        0..107     the perforated inner disc carrying the readout

    The ring is a timeline, and the approved art's own labels say which way it
    runs: "-4:00" sits at nine o'clock, "-2:00" at twelve, "0:00 NOW" at three.
    So time runs CLOCKWISE over the top - oldest on the left, now on the right -
    with red covering the older half and cyan the recent half, and a dark gap
    across the bottom where a gauge would have its dead zone.

    The arcs always span the whole timeline, because the timeline IS the history
    that exists - the scale labels rescale as the buffer grows, so "-4:00" on a
    part-filled buffer means the same thing "-8:00" means on a full one. What
    reports buffer fullness is the outer tick ring, which lights in proportion
    to available/max. The arcs breathe while armed and specks drift toward now.
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

class BufferHudPanel : public juce::Component
{
public:
    explicit BufferHudPanel (KeepThatProcessor& p) : processor (p)
    {
        addAndMakeVisible (keepLast);
        keepLast.onClick = [this] { if (onKeepLast) onKeepLast(); };

        static const char* assetIds[] = { "1_bar", "2_bars", "4_bars", "8_bars",
                                          "15_sec", "30_sec", "60_sec", "phrase" };
        const auto& table = captureLengths();
        for (int i = 0; i < (int) table.size(); ++i)
        {
            auto* b = lengthButtons.add (new LengthButton (table[(size_t) i].label(),
                                                           assetIds[i]));
            addAndMakeVisible (b);
            b->onClick = [this, i] { selectLength (i); };
        }
        syncSelection();

        for (auto& s : sparks)
            s = juce::Random::getSystemRandom().nextFloat();
    }

    std::function<void()> onKeepLast;
    std::function<void (int)> onLengthChanged;

    /** Reduce Motion stops the ring's drifting sparks and its breathe, leaving
        the readout and the meters moving. ANIMATION_SPEC asks for exactly that
        split rather than freezing the whole interface. */
    void setReduceMotion (bool reduce) { reduceMotion = reduce; repaint(); }

    /** Re-reads the selected length from the session - needed after a preset
        load changes it behind the buttons' backs. */
    void syncSelection()
    {
        const auto& s = processor.session();
        // Exactly one button lit - the one KEEP LAST will actually use.
        for (int i = 0; i < lengthButtons.size(); ++i)
            lengthButtons[i]->setSelected (i == s.selectedLength);
    }

    void resized() override
    {
        const auto off = juce::Point<int> (-getX(), -getY());
        keepLast.setBounds (layout::keepButton.translated (off.x, off.y));

        for (int i = 0; i < lengthButtons.size(); ++i)
        {
            const auto row = i < 4 ? layout::lengthRow1 : layout::lengthRow2;
            lengthButtons[i]->setBounds (
                layout::cell (row.translated (off.x, off.y), i % 4, layout::lengthCols, 2));
        }
    }

    void update (double dt)
    {
        phase += dt;
        pulse = 0.5f + 0.5f * (float) std::sin (phase * 1.9f);
        keepLast.setPulse (pulse);
        keepLast.setArmed (processor.session().armed);

        orbitPhase += (float) dt * 0.16f;   if (orbitPhase > 1.0f) orbitPhase -= 1.0f;
        pulsePhase += (float) dt * 0.55f;   if (pulsePhase > 1.0f) pulsePhase -= 1.0f;

        // Drift the captured-side sparks toward the marker at the top.
        if (! reduceMotion)
            for (auto& s : sparks)
            {
                s += (float) dt * 0.085f;
                if (s > 1.0f) s -= 1.0f;
            }

        auto& session = processor.session();
        if (processor.hasSeenAudio())
        {
            session.bufferAvailable = processor.buffer().availableSeconds();
            session.bufferMax = processor.buffer().maxSeconds();
        }

        repaint (layout::ringArt.translated (-getX(), -getY()).expanded (4));
    }

    void paintArtHud (juce::Graphics& g, const SessionState& s, float breathe)
    {
        const auto centre = juce::Point<float> (layout::ringCentreX - getX(),
                                                layout::ringCentreY - getY());
        auto area = layout::ringArt.translated (-getX(), -getY()).toFloat();
        const auto artCentre = juce::Point<float> (layout::ringArtCentreX - getX(),
                                                   layout::ringArtCentreY - getY());
        juce::ignoreUnused (centre);

        Assets::drawFitted (g, art::hudRing, area);

        // NO knock-back over the ring. The artwork is drawn as authored and
        // left alone: the buffer level is reported by the readout in the
        // middle, which is where anyone actually reads it. A dim wedge laid
        // over the arcs to show the unfilled stretch was tried and removed -
        // it fought the art at every fill and never earned its keep.

        // Elapsed-time marker, riding the artwork's own tick ring. One lap
        // per ringSpanSeconds so the hand agrees with the ring's four-minute
        // scale, and clockwise because that is the direction the printed
        // scale runs: -4:00 at nine o'clock, through -2:00 at twelve, to
        // 0:00 NOW at three.
        //
        // This ADDS light to the ticks rather than taking any away. That is
        // the whole difference between it and the knock-back wedge that used
        // to live here: the artwork stays as authored and the marker is one
        // small bright thing on top of it.
        {
            const float a    = sweepAngle (s.bufferAvailable);
            const float rIn  = layout::ringArtSide * layout::ringTickInnerF;
            const float rOut = layout::ringArtSide * layout::ringTickOuterF;

            // A short tail behind the head, so which way time is running is
            // readable without having to watch it move.
            for (int i = 4; i >= 1; --i)
            {
                auto tail = paint::ringSegment (artCentre, rIn + 4.0f, rOut - 4.0f,
                                                a + 0.040f * (float) i,
                                                a + 0.040f * (float) (i + 1));
                g.setColour (tokens::accentGold.withAlpha (0.16f / (float) i));
                g.fillPath (tail);
            }

            auto glow = paint::ringSegment (artCentre, rIn - 6.0f, rOut + 6.0f,
                                            a - 0.060f, a + 0.060f);
            g.setColour (tokens::accentGold.withAlpha (0.20f));
            g.fillPath (glow);

            auto head = paint::ringSegment (artCentre, rIn, rOut, a - 0.028f, a + 0.028f);
            g.setColour (tokens::accentGold.withAlpha (0.95f));
            g.fillPath (head);

            // A white core, so the head reads as clearly over the cyan half
            // as it does over the red.
            auto core = paint::ringSegment (artCentre, rIn + 7.0f, rOut - 7.0f,
                                            a - 0.013f, a + 0.013f);
            g.setColour (juce::Colours::white.withAlpha (0.90f));
            g.fillPath (core);
        }

        // No filmstrip overlay here. The pack still carries v1.3's 64-frame
        // orbit and 32-frame pulse, but those were authored for v1.3's chunky
        // segmented wheel and lay gold segments straight across this ring's
        // thin arcs. Motion instead comes from a breath on the ring's own
        // colours, which cannot clash with the art because it IS the art.
        if (s.armed)
        {
            const auto centre2 = juce::Point<float> (layout::ringCentreX - getX(),
                                                     layout::ringCentreY - getY());
            paint::halo (g, centre2, layout::rOuterArcMid * 1.05f,
                         tokens::accentRed, 0.05f * breathe);
            auto lit = paint::ringSegment (centre2, layout::rInnerArcMid,
                                           layout::rOuterArcMid + 6.0f, spanStart, spanEnd);
            g.setColour (juce::Colours::white.withAlpha (0.035f * breathe));
            g.fillPath (lit);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& s = processor.session();
        const auto centre = juce::Point<float> (layout::ringCentreX - getX(),
                                                layout::ringCentreY - getY());
        const float fill = (float) juce::jlimit (0.0, 1.0,
                               s.bufferAvailable / layout::ringSpanSeconds);
        const float breathe = reduceMotion ? 0.85f
                            : (s.armed ? (0.82f + 0.18f * pulse) : 0.45f);
        const auto off = juce::Point<int> (-getX(), -getY());

        if (componentArtwork() && Assets::has (art::hudRing))
        {
            // The ring slice carries the -2:00 / -4:00 / 0:00 NOW scale, and
            // the KEEP LAST art carries its own housing, so neither is drawn
            // again here. Its centre is empty by design, for the live clock.
            paintArtHud (g, s, breathe);
            paintReadout (g, s);
            return;
        }

        paintRingBed (g, centre, breathe);
        paintTickRing (g, centre, fill);
        paintMainArcs (g, centre, breathe);
        paintDashRing (g, centre);
        paintInnerDisc (g, centre);
        paintNowMarker (g, centre, breathe);
        if (! reduceMotion)
            paintSparks (g, centre);
        paintReadout (g, s);
        paintRingLabels (g, s);
        paintKeepChassis (g);
        juce::ignoreUnused (off, fill);
    }

private:
    // Angles use juce::Path::addCentredArc's convention: 0 = twelve o'clock,
    // positive clockwise. The live span runs from 7:30 round the top to 4:30,
    // leaving a 90-degree dark gap across the bottom.
    /** Where the elapsed-time marker sits, in JUCE's angle convention
        (0 = twelve o'clock, positive clockwise).

        It reads against the scale the ARTWORK prints, which is the top half
        only: 0:00 NOW at three o'clock, -2:00 at twelve, -4:00 at nine. So
        the marker starts at NOW and travels backwards - anticlockwise - as
        the buffer fills, and it is pointing at how far back you can actually
        go. Two minutes of history puts it exactly on the -2:00 mark, four
        puts it on -4:00.

        A free-running lap from twelve o'clock, which is what this did first,
        agreed with those labels nowhere.

        Clamped at -4:00: the buffer can hold more than the ring shows (MAX is
        up to 8:00), and once you have the whole visible window the marker has
        nothing further to say. */
    static float sweepAngle (double availableSeconds) noexcept
    {
        const double f = juce::jlimit (0.0, 1.0,
                             availableSeconds / layout::ringSpanSeconds);
        return (float) (juce::MathConstants<double>::halfPi
                            - f * juce::MathConstants<double>::pi);
    }

    static constexpr float spanStart = -2.60f;    // ~7:00, the oldest end
    static constexpr float spanEnd   =  2.60f;    // ~5:00, "now"

    // For the record, since it is not obvious from the art and cost real time
    // to work out: the ring's PRINTED scale is the top half only - -4:00 at
    // nine o'clock, -2:00 at twelve, 0:00 NOW at three. The arcs sweep well
    // past both ends of that, down to about 6:00 on each side, and those
    // tails are end decoration rather than time. Anything added later that
    // has to agree with those labels must measure against +/-halfPi, NOT
    // against spanStart/spanEnd.
    static constexpr float gap       =  0.030f;   // hairline either side of top

    void selectLength (int index)
    {
        auto& s = processor.session();
        // One choice across both rows: picking a seconds length REPLACES the
        // bars pick, because that is what pressing it means. Two lit rows was
        // not a style - the seconds row simply never took effect.
        s.selectedLength = index;
        if (index >= 4)
            s.selectedSeconds = index;     // kept in step for old sessions
        syncSelection();
        if (onLengthChanged) onLengthChanged (index);
    }

    void paintRingBed (juce::Graphics& g, juce::Point<float> centre, float breathe)
    {
        // A dark disc under everything so the arcs sit on their own ground
        // rather than on whatever the shell texture is doing.
        // Opaque out to the artwork's outermost ring pixel, then feathered -
        // with the shell art underneath, a translucent bed would let the baked
        // ring ghost through behind the live one.
        const float solidTo = layout::rOuterArcMid + 16.0f;
        g.setColour (juce::Colour (0xff090c12));
        g.fillEllipse (juce::Rectangle<float> (solidTo * 2.0f, solidTo * 2.0f)
                           .withCentre (centre));

        juce::ColourGradient bed (juce::Colour (0xff0b1017), centre.x, centre.y,
                                  juce::Colour (0x000b1017), centre.x,
                                  centre.y - layout::rOuterArcMid - 16.0f, true);
        g.setGradientFill (bed);
        g.fillEllipse (juce::Rectangle<float> (1.0f, 1.0f)
                           .withSizeKeepingCentre ((layout::rOuterArcMid + 20.0f) * 2.0f,
                                                   (layout::rOuterArcMid + 20.0f) * 2.0f)
                           .withCentre (centre));

        paint::halo (g, centre, layout::rOuterArcMid * 1.15f, tokens::accentRed, 0.05f * breathe);
    }

    void paintTickRing (juce::Graphics& g, juce::Point<float> centre, float fill)
    {
        // The one layer that reports buffer fullness: marks light from "now"
        // backwards as history accumulates. Only across the live span; the
        // bottom quadrant stays dark.
        constexpr int count = 90;
        for (int i = 0; i <= count; ++i)
        {
            const float t = i / (float) count;             // 0 = oldest, 1 = now
            const float a = spanStart + t * (spanEnd - spanStart);
            const bool major = (i % 6) == 0;
            const float inner = major ? layout::rTickInner : layout::rTickInner + 5.0f;

            const auto colour = a < 0.0f ? tokens::accentRed : tokens::accentCyan;
            const bool lit = t >= (1.0f - fill);

            const auto p1 = centre.getPointOnCircumference (inner, a);
            const auto p2 = centre.getPointOnCircumference (layout::rTickOuter, a);
            g.setColour (colour.withAlpha (lit ? (major ? 0.95f : 0.58f) : 0.10f));
            g.drawLine (p1.x, p1.y, p2.x, p2.y, major ? 1.8f : 1.0f);
        }
    }

    /** One red/cyan pair at a given radius. Every band in the art is a thin
        bright core with a wide bloom, so that is the only way they are drawn. */
    void arcPair (juce::Graphics& g, juce::Point<float> centre, float radius,
                  float core, float spread, float intensity, int layers = 6)
    {
        auto red = paint::arcPath (centre, radius, spanStart, -gap);
        paint::glowPath (g, red, tokens::accentRed, core, spread, intensity, layers);

        auto cyan = paint::arcPath (centre, radius, gap, spanEnd);
        paint::glowPath (g, cyan, tokens::accentCyan, core, spread, intensity, layers);
    }

    void paintMainArcs (juce::Graphics& g, juce::Point<float> centre, float breathe)
    {
        // Outside in. The outer arc is the dominant band in the approved art -
        // thickest core and the widest bloom - and everything inside it steps
        // down, which is what gives the ring its depth.
        // Cores stay close to the measured band widths and the bloom is kept
        // TIGHT. A wide spread made the four layers bleed into one another and
        // the ring lost the dark separation that gives the reference its depth.
        arcPair (g, centre, layout::rOuterArcMid, 10.0f, 15.0f, 0.97f * breathe, 5);
        arcPair (g, centre, layout::rMidArcMid,    8.5f, 11.0f, 0.90f * breathe, 4);
        arcPair (g, centre, layout::rAccentMid,    2.4f,  6.0f, 0.78f * breathe, 3);
        arcPair (g, centre, layout::rInnerArcMid,  4.5f,  8.0f, 0.88f * breathe, 4);

        // A white-hot filament down the middle of each big arc. This is what
        // makes them read as lit tubes rather than flat coloured bands, and it
        // is clearly present in the reference on both the outer and mid arcs.
        for (float radius : { layout::rOuterArcMid, layout::rMidArcMid })
        {
            auto redHot = paint::arcPath (centre, radius, spanStart, -gap);
            paint::glowPath (g, redHot, juce::Colour (0xffffc9b4), 1.8f, 3.5f,
                             0.62f * breathe, 2);
            auto cyanHot = paint::arcPath (centre, radius, gap, spanEnd);
            paint::glowPath (g, cyanHot, juce::Colour (0xffd8f7ff), 1.8f, 3.5f,
                             0.62f * breathe, 2);
        }
    }

    void paintDashRing (juce::Graphics& g, juce::Point<float> centre)
    {
        constexpr int count = 72;
        for (int i = 0; i <= count; ++i)
        {
            const float t = i / (float) count;
            const float a = spanStart + t * (spanEnd - spanStart);
            const auto colour = a < 0.0f ? tokens::accentRed : tokens::accentCyan;

            const auto p1 = centre.getPointOnCircumference (layout::rDashInner, a);
            const auto p2 = centre.getPointOnCircumference (layout::rDashOuter, a);
            g.setColour (colour.withAlpha (0.58f));
            g.drawLine (p1.x, p1.y, p2.x, p2.y, 2.1f);
        }
    }

    void paintInnerDisc (juce::Graphics& g, juce::Point<float> centre)
    {
        const float r = layout::rDiscEdge;
        auto disc = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d1219), centre.x,
                                                 centre.y - r,
                                                 juce::Colour (0xff05080c), centre.x,
                                                 centre.y + r, false));
        g.fillEllipse (disc);

        // Perforated texture, clipped to the disc.
        g.saveState();
        juce::Path clip;
        clip.addEllipse (disc.reduced (3.0f));
        g.reduceClipRegion (clip);
        paint::perforate (g, disc, 7.0f, juce::Colours::white.withAlpha (0.028f), 1.7f);
        g.restoreState();

        g.setColour (tokens::strokeHi.withAlpha (0.55f));
        g.drawEllipse (disc.reduced (0.5f), 1.2f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (disc.expanded (2.5f), 2.0f);
    }

    void paintNowMarker (juce::Graphics& g, juce::Point<float> centre, float breathe)
    {
        // The bright bar at twelve o'clock, where red hands over to cyan: the
        // midpoint of the window. The approved art marks it, not "now".
        const auto p1 = centre.getPointOnCircumference (layout::rInnerArcMid - 6.0f, 0.0f);
        const auto p2 = centre.getPointOnCircumference (layout::rOuterArcMid + 10.0f, 0.0f);
        juce::Path marker;
        marker.startNewSubPath (p1);
        marker.lineTo (p2);
        paint::glowPath (g, marker, tokens::cyanPale, 3.4f, 9.0f, 0.75f + 0.25f * breathe, 4);
    }

    void paintSparks (juce::Graphics& g, juce::Point<float> centre)
    {
        // Specks drifting along the timeline toward "now".
        const float litStart = spanStart;
        const float radius = layout::rOuterArcMid;
        for (float sp : sparks)
        {
            const float a = litStart + sp * (spanEnd - litStart);
            const auto pt = centre.getPointOnCircumference (radius, a);
            const float fade = std::sin (sp * juce::MathConstants<float>::pi);
            const auto colour = a < 0.0f ? tokens::redHot : tokens::cyanPale;
            g.setColour (colour.withAlpha (0.55f * fade));
            g.fillEllipse (pt.x - 1.6f, pt.y - 1.6f, 3.2f, 3.2f);
            g.setColour (colour.withAlpha (0.18f * fade));
            g.fillEllipse (pt.x - 4.0f, pt.y - 4.0f, 8.0f, 8.0f);
        }
    }

    void paintReadout (juce::Graphics& g, const SessionState& s)
    {
        const auto off = juce::Point<int> (-getX(), -getY());

        paint::glowText (g, s.armed ? "BUFFER ACTIVE" : "BUFFER PAUSED",
                         layout::hudState.translated (off.x, off.y), Fonts::hudState(),
                         s.armed ? tokens::accentRed : tokens::textMuted,
                         juce::Justification::centred, 0.5f);

        paint::glowText (g, s.availableText(), layout::hudTime.translated (off.x, off.y),
                         Fonts::hudTime(), tokens::textPrimary,
                         juce::Justification::centred, 0.35f);

        g.setFont (Fonts::hudLabel());
        g.setColour (tokens::textSecond);
        g.drawText ("AVAILABLE", layout::hudAvailable.translated (off.x, off.y),
                    juce::Justification::centred, false);

        g.setFont (Fonts::hudMax());
        g.setColour (tokens::textMuted);
        g.drawText ("MAX " + s.maxText().trimCharactersAtStart ("0"),
                    layout::hudMax.translated (off.x, off.y),
                    juce::Justification::centred, false);
    }

    void paintRingLabels (juce::Graphics& g, const SessionState& s)
    {
        const auto off = juce::Point<int> (-getX(), -getY());

        // The ring spans the history actually available. The scale marks are
        // rounded down to the minute so they read as a clean ruler rather than
        // as a second copy of the running clock in the middle.
        const double span = s.bufferAvailable;
        auto mark = [] (double seconds)
        {
            const int minutes = (int) std::floor (seconds / 60.0);
            return "-" + juce::String (minutes) + ":00";
        };

        g.setFont (Fonts::ringTick());
        g.setColour (tokens::textSecond);
        g.drawText (mark (span * 0.5), layout::hudLabelTop.translated (off.x, off.y),
                    juce::Justification::centred, false);
        g.drawText (mark (span), layout::hudLabelLeft.translated (off.x, off.y),
                    juce::Justification::centredRight, false);

        auto right = layout::hudLabelRight.translated (off.x, off.y);
        g.setColour (tokens::textSecond);
        g.drawText ("0:00", right.removeFromTop (20), juce::Justification::centredLeft, false);
        g.setColour (tokens::accentRed);
        g.drawText ("NOW", right, juce::Justification::centredLeft, false);
    }

    void paintKeepChassis (juce::Graphics& g)
    {
        // The angled housing the KEEP LAST slab sits in, with the perforated
        // wings either side.
        auto r = layout::keepChassis.translated (-getX(), -getY()).toFloat();
        auto shell = paint::chamfered (r, 22.0f);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff151a22), r.getCentreX(), r.getY(),
                                                 juce::Colour (0xff0a0e14), r.getCentreX(),
                                                 r.getBottom(), false));
        g.fillPath (shell);
        g.setColour (tokens::stroke);
        g.strokePath (shell, juce::PathStrokeType (1.0f));

        auto btn = layout::keepButton.translated (-getX(), -getY()).toFloat();
        for (int side = 0; side < 2; ++side)
        {
            auto wing = side == 0
                ? juce::Rectangle<float> (r.getX() + 14.0f, r.getY() + 16.0f,
                                          btn.getX() - r.getX() - 26.0f, r.getHeight() - 32.0f)
                : juce::Rectangle<float> (btn.getRight() + 12.0f, r.getY() + 16.0f,
                                          r.getRight() - btn.getRight() - 26.0f,
                                          r.getHeight() - 32.0f);
            if (wing.getWidth() > 6.0f)
                paint::perforate (g, wing.withTrimmedRight (side == 0 ? 22.0f : 0.0f)
                                       .withTrimmedLeft (side == 1 ? 22.0f : 0.0f),
                                  8.0f, tokens::accentRed.withAlpha (0.42f), 2.4f);

            // The chevron bracket between the dot column and the slab.
            const float bx = side == 0 ? btn.getX() - 9.0f : btn.getRight() + 9.0f;
            const float dir = side == 0 ? -1.0f : 1.0f;
            juce::Path bracket;
            bracket.startNewSubPath (bx, btn.getY() - 2.0f);
            bracket.lineTo (bx + dir * 14.0f, btn.getCentreY());
            bracket.lineTo (bx, btn.getBottom() + 2.0f);
            paint::glowPath (g, bracket, tokens::accentRed, 3.4f, 9.0f, 0.8f, 4);
        }
    }

    KeepThatProcessor& processor;
    KeepLastButton keepLast;
    juce::OwnedArray<LengthButton> lengthButtons;

    double phase = 0.0;
    float pulse = 0.0f;
    float orbitPhase = 0.0f, pulsePhase = 0.0f;
    bool reduceMotion = false;
    std::array<float, 7> sparks {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BufferHudPanel)
};

} // namespace keepthat
