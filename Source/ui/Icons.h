/*
    Icons.h - every glyph in the interface, drawn as vector paths.

    The asset pack ships reference crops, not icon files, and the spec forbids
    blitting the reference art into the plugin - so these are rebuilt as paths.
    Each is authored inside a nominal 24 x 24 box and scaled to fit wherever it
    is used, which keeps them sharp at any editor scale and on any DPI.

    Paths are built on demand rather than cached: they are a few dozen segments
    each, and the repaint cost is nothing next to the gradients around them.
*/

#pragma once
#include <JuceHeader.h>

namespace keepthat::icons
{

using P = juce::Path;

// -----------------------------------------------------------------------------
//  Building blocks
// -----------------------------------------------------------------------------
inline void addBars (P& p, const float* heights, int n, float x0, float w, float centreY)
{
    const float step = w / (float) n;
    const float bw = juce::jmax (1.0f, step * 0.5f);
    for (int i = 0; i < n; ++i)
    {
        const float h = heights[i] * 0.5f;
        p.addRoundedRectangle (x0 + i * step + (step - bw) * 0.5f, centreY - h, bw, h * 2.0f,
                               bw * 0.4f);
    }
}

// -----------------------------------------------------------------------------
//  Header utilities
// -----------------------------------------------------------------------------
inline P save()          // floppy disk
{
    // Built as one outline plus two solid inserts. The previous version punched
    // the shutter and label out as holes, which left the glyph reading as an
    // "H" once the winding rule flipped.
    P p;
    P body;
    body.addRoundedRectangle (2.5f, 2.5f, 19.0f, 19.0f, 2.4f);
    P bodyInner;
    bodyInner.addRoundedRectangle (4.3f, 4.3f, 15.4f, 15.4f, 1.4f);
    body.setUsingNonZeroWinding (false);
    body.addPath (bodyInner);
    p.addPath (body);

    p.addRectangle (7.5f, 2.5f, 9.0f, 6.4f);                 // shutter
    P slot;
    slot.addRectangle (13.2f, 3.9f, 2.0f, 3.6f);
    p.setUsingNonZeroWinding (false);
    p.addPath (slot);

    P label;
    label.addRoundedRectangle (6.6f, 12.6f, 10.8f, 8.9f, 1.0f);
    P labelInner;
    labelInner.addRoundedRectangle (8.0f, 14.0f, 8.0f, 6.1f, 0.6f);
    label.setUsingNonZeroWinding (false);
    label.addPath (labelInner);
    p.addPath (label);
    return p;
}

inline P gear()
{
    P p;
    const auto centre = juce::Point<float> (12.0f, 12.0f);
    P tooth;
    tooth.addRoundedRectangle (10.6f, 0.6f, 2.8f, 5.6f, 1.2f);
    for (int i = 0; i < 8; ++i)
        p.addPath (tooth, juce::AffineTransform::rotation (i * juce::MathConstants<float>::pi / 4.0f,
                                                           centre.x, centre.y));
    p.addEllipse (4.0f, 4.0f, 16.0f, 16.0f);
    P hole;
    hole.addEllipse (8.6f, 8.6f, 6.8f, 6.8f);
    p.setUsingNonZeroWinding (false);
    p.addPath (hole);
    return p;
}

inline P help()
{
    P p;
    P ring;
    ring.addEllipse (2.0f, 2.0f, 20.0f, 20.0f);
    juce::PathStrokeType (1.9f).createStrokedPath (p, ring);

    // The hook of the question mark, drawn as a stroked arc plus a stem so it
    // stays legible at 18 px.
    P hook;
    hook.addCentredArc (12.0f, 9.4f, 3.5f, 3.4f, 0.0f,
                        -juce::MathConstants<float>::pi * 0.85f,
                        juce::MathConstants<float>::pi * 0.62f, true);
    hook.lineTo (12.0f, 14.8f);
    P hookStroked;
    juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (hookStroked, hook);
    p.addPath (hookStroked);
    p.addEllipse (10.75f, 16.6f, 2.5f, 2.5f);
    return p;
}

inline P undoArrow (bool mirrored)
{
    // A shallow arc with the head on its own tail. The head has to start where
    // the arc actually ends, or it floats off on its own.
    P body;
    body.startNewSubPath (4.6f, 14.4f);
    body.cubicTo (7.0f, 6.4f, 17.6f, 6.0f, 19.8f, 13.6f);
    P p;
    juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (p, body);

    P head;
    head.startNewSubPath (4.6f, 19.6f);
    head.lineTo (0.4f, 12.6f);
    head.lineTo (9.0f, 12.0f);
    head.closeSubPath();
    p.addPath (head);

    if (mirrored)
        p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f).translated (24.0f, 0.0f));
    return p;
}

inline P power()
{
    P ring;
    ring.addCentredArc (12.0f, 12.5f, 8.2f, 8.2f, 0.0f,
                        -juce::MathConstants<float>::pi * 0.78f,
                        juce::MathConstants<float>::pi * 0.78f, true);
    P p;
    juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (p, ring);

    P stem;
    stem.startNewSubPath (12.0f, 2.4f);
    stem.lineTo (12.0f, 11.0f);
    P stemStroked;
    juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (stemStroked, stem);
    p.addPath (stemStroked);
    return p;
}

// -----------------------------------------------------------------------------
//  Recovery tools
// -----------------------------------------------------------------------------
inline P waveBars()      // AUTO TRIM / DAW DRAG
{
    P p;
    static const float h[] = { 5.0f, 11.0f, 17.0f, 22.0f, 14.0f, 19.0f, 9.0f, 13.0f, 5.0f };
    addBars (p, h, 9, 2.0f, 20.0f, 12.0f);
    return p;
}

inline P waveGap()       // SILENCE DETECT - signal, gap, signal
{
    P p;
    static const float a[] = { 7.0f, 15.0f, 21.0f, 12.0f };
    static const float b[] = { 11.0f, 20.0f, 14.0f, 6.0f };
    addBars (p, a, 4, 1.5f, 8.0f, 12.0f);
    p.addRectangle (11.0f, 11.4f, 2.4f, 1.2f);       // the silence
    addBars (p, b, 4, 14.5f, 8.0f, 12.0f);
    return p;
}

inline P zeroCrossing()  // circle with a rising diagonal through it
{
    P p;
    P ring;
    ring.addEllipse (2.0f, 2.0f, 20.0f, 20.0f);
    juce::PathStrokeType (1.9f).createStrokedPath (p, ring);

    P slash;
    slash.startNewSubPath (4.6f, 17.4f);
    slash.lineTo (19.4f, 6.6f);
    P slashStroked;
    juce::PathStrokeType (1.9f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (slashStroked, slash);
    p.addPath (slashStroked);

    P axis;
    axis.startNewSubPath (3.0f, 12.0f);
    axis.lineTo (21.0f, 12.0f);
    P axisStroked;
    juce::PathStrokeType (1.0f).createStrokedPath (axisStroked, axis);
    p.addPath (axisStroked);
    return p;
}

inline P fadeCurve()
{
    P curve;
    curve.startNewSubPath (2.5f, 20.0f);
    curve.cubicTo (10.0f, 20.0f, 12.0f, 4.0f, 21.5f, 4.0f);
    P p;
    juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (p, curve);
    return p;
}

inline P normalizeBars()
{
    P p;
    static const float h[] = { 8.0f, 14.0f, 20.0f, 14.0f, 8.0f, 16.0f, 10.0f };
    addBars (p, h, 7, 2.5f, 19.0f, 12.0f);
    return p;
}

inline P download()      // DRAG EXPORT
{
    P p;
    P stem;
    stem.startNewSubPath (12.0f, 2.5f);
    stem.lineTo (12.0f, 13.5f);
    juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (p, stem);

    P head;
    head.startNewSubPath (6.4f, 10.4f);
    head.lineTo (12.0f, 16.6f);
    head.lineTo (17.6f, 10.4f);
    head.closeSubPath();
    p.addPath (head);

    P tray;
    tray.startNewSubPath (3.5f, 18.0f);
    tray.lineTo (3.5f, 21.0f);
    tray.lineTo (20.5f, 21.0f);
    tray.lineTo (20.5f, 18.0f);
    P trayStroked;
    juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::square).createStrokedPath (trayStroked, tray);
    p.addPath (trayStroked);
    return p;
}

// -----------------------------------------------------------------------------
//  Transport + actions
// -----------------------------------------------------------------------------
inline P play()
{
    P p;
    p.startNewSubPath (6.5f, 3.8f);
    p.lineTo (19.5f, 12.0f);
    p.lineTo (6.5f, 20.2f);
    p.closeSubPath();
    return p;
}

inline P stop()
{
    P p;
    p.addRoundedRectangle (5.0f, 5.0f, 14.0f, 14.0f, 1.6f);
    return p;
}

inline P scissors()
{
    P p;
    P blades;
    blades.startNewSubPath (5.0f, 3.0f);
    blades.lineTo (17.5f, 16.0f);
    blades.startNewSubPath (19.0f, 3.0f);
    blades.lineTo (6.5f, 16.0f);
    juce::PathStrokeType (1.9f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (p, blades);

    P holes;
    holes.addEllipse (3.0f, 15.5f, 6.0f, 6.0f);
    holes.addEllipse (15.0f, 15.5f, 6.0f, 6.0f);
    P holesStroked;
    juce::PathStrokeType (1.8f).createStrokedPath (holesStroked, holes);
    p.addPath (holesStroked);
    return p;
}

inline P pencil()
{
    P p;
    p.startNewSubPath (3.0f, 21.0f);
    p.lineTo (4.4f, 16.4f);
    p.lineTo (16.4f, 4.4f);
    p.lineTo (19.6f, 7.6f);
    p.lineTo (7.6f, 19.6f);
    p.closeSubPath();
    P nib;
    nib.startNewSubPath (17.4f, 3.4f);
    nib.lineTo (20.6f, 6.6f);
    nib.lineTo (22.0f, 5.2f);
    nib.lineTo (18.8f, 2.0f);
    nib.closeSubPath();
    p.addPath (nib);
    return p;
}

inline P waveDot()       // SAVE WAV: a waveform with a marker
{
    P p;
    static const float h[] = { 6.0f, 13.0f, 20.0f, 13.0f, 6.0f };
    addBars (p, h, 5, 3.0f, 18.0f, 12.0f);
    return p;
}

inline P dragArrow()     // DRAG TO DAW: a curved arrow leaving the panel
{
    P curve;
    curve.startNewSubPath (18.5f, 19.0f);
    curve.cubicTo (18.5f, 9.0f, 11.0f, 7.0f, 5.5f, 7.0f);
    P p;
    juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (p, curve);

    P head;
    head.startNewSubPath (9.5f, 2.0f);
    head.lineTo (3.0f, 7.0f);
    head.lineTo (9.5f, 12.0f);
    head.closeSubPath();
    p.addPath (head);
    return p;
}

// -----------------------------------------------------------------------------
//  Export destinations
// -----------------------------------------------------------------------------
inline P sampler()       // pad grid over keys
{
    P p;
    p.addRoundedRectangle (2.5f, 3.0f, 19.0f, 18.0f, 2.0f);
    P inner;
    inner.addRoundedRectangle (4.2f, 4.7f, 15.6f, 14.6f, 1.2f);
    p.setUsingNonZeroWinding (false);
    p.addPath (inner);

    P keys;
    for (int i = 0; i < 4; ++i)
        keys.addRectangle (5.6f + i * 3.4f, 6.2f, 1.6f, 11.6f);
    p.addPath (keys);
    return p;
}

inline P list()          // PLAYLIST
{
    P p;
    for (int i = 0; i < 3; ++i)
    {
        const float y = 5.0f + i * 6.0f;
        p.addEllipse (3.0f, y, 3.0f, 3.0f);
        p.addRoundedRectangle (8.5f, y + 0.4f, 12.5f, 2.2f, 1.1f);
    }
    return p;
}

inline P folder()
{
    P p;
    p.startNewSubPath (2.5f, 6.5f);
    p.lineTo (2.5f, 19.5f);
    p.lineTo (21.5f, 19.5f);
    p.lineTo (21.5f, 8.5f);
    p.lineTo (11.5f, 8.5f);
    p.lineTo (9.3f, 6.5f);
    p.closeSubPath();
    P inner;
    inner.startNewSubPath (4.3f, 8.3f);
    inner.lineTo (4.3f, 17.7f);
    inner.lineTo (19.7f, 17.7f);
    inner.lineTo (19.7f, 10.3f);
    inner.lineTo (10.8f, 10.3f);
    inner.lineTo (8.6f, 8.3f);
    inner.closeSubPath();
    p.setUsingNonZeroWinding (false);
    p.addPath (inner);
    return p;
}

inline P desktop()
{
    P p;
    p.addRoundedRectangle (2.0f, 4.0f, 20.0f, 13.5f, 1.6f);
    P inner;
    inner.addRoundedRectangle (3.7f, 5.7f, 16.6f, 10.1f, 0.8f);
    p.setUsingNonZeroWinding (false);
    p.addPath (inner);
    p.addRoundedRectangle (9.5f, 17.5f, 5.0f, 3.0f, 0.6f);
    p.addRoundedRectangle (6.5f, 20.0f, 11.0f, 1.8f, 0.9f);
    return p;
}

// -----------------------------------------------------------------------------
//  Cards
// -----------------------------------------------------------------------------
inline P heart()
{
    P p;
    p.startNewSubPath (12.0f, 20.5f);
    p.cubicTo (2.0f, 13.6f, 3.4f, 5.2f, 8.6f, 5.2f);
    p.cubicTo (10.6f, 5.2f, 11.6f, 6.6f, 12.0f, 7.6f);
    p.cubicTo (12.4f, 6.6f, 13.4f, 5.2f, 15.4f, 5.2f);
    p.cubicTo (20.6f, 5.2f, 22.0f, 13.6f, 12.0f, 20.5f);
    p.closeSubPath();
    return p;
}

inline P star()
{
    P p;
    const float cx = 12.0f, cy = 12.4f;
    for (int i = 0; i < 10; ++i)
    {
        const float a = -juce::MathConstants<float>::halfPi
                      + i * juce::MathConstants<float>::pi / 5.0f;
        const float r = (i % 2 == 0) ? 9.6f : 4.2f;
        const auto pt = juce::Point<float> (cx + std::cos (a) * r, cy + std::sin (a) * r);
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    p.closeSubPath();
    return p;
}

inline P trash()
{
    P p;
    p.addRoundedRectangle (4.5f, 6.5f, 15.0f, 15.0f, 1.8f);
    P inner;
    inner.addRoundedRectangle (6.3f, 8.3f, 11.4f, 11.4f, 0.9f);
    p.setUsingNonZeroWinding (false);
    p.addPath (inner);

    P lid;
    lid.addRoundedRectangle (2.5f, 4.0f, 19.0f, 2.2f, 1.1f);
    lid.addRoundedRectangle (9.0f, 1.8f, 6.0f, 2.2f, 1.0f);
    p.addPath (lid);

    P ribs;
    ribs.addRectangle (9.4f, 10.4f, 1.3f, 7.2f);
    ribs.addRectangle (13.3f, 10.4f, 1.3f, 7.2f);
    p.addPath (ribs);
    return p;
}

// -----------------------------------------------------------------------------
//  Chrome
// -----------------------------------------------------------------------------
inline P chevron (float radians)
{
    P v;
    v.startNewSubPath (9.5f, 4.5f);
    v.lineTo (16.0f, 12.0f);
    v.lineTo (9.5f, 19.5f);
    P p;
    juce::PathStrokeType (2.6f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (p, v);
    if (radians != 0.0f)
        p.applyTransform (juce::AffineTransform::rotation (radians, 12.0f, 12.0f));
    return p;
}

inline P info()
{
    P p;
    P ring;
    ring.addEllipse (1.8f, 1.8f, 20.4f, 20.4f);
    juce::PathStrokeType (1.6f).createStrokedPath (p, ring);
    p.addEllipse (10.7f, 5.6f, 2.6f, 2.6f);
    p.addRoundedRectangle (10.7f, 10.2f, 2.6f, 8.2f, 1.3f);
    return p;
}

/** The KEEP THAT! brand mark: a broken capture ring with an off-centre core. */
inline P brandMark()
{
    P p;

    // Outer ring, open at the lower left where the "tail" leaves it.
    P outer;
    outer.addCentredArc (12.0f, 12.0f, 10.2f, 10.2f, 0.0f,
                         -juce::MathConstants<float>::pi * 0.62f,
                         juce::MathConstants<float>::pi * 1.16f, true);
    juce::PathStrokeType (2.3f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (p, outer);

    // Inner ring, opened on the opposite side so the two reads as motion.
    P inner;
    inner.addCentredArc (12.0f, 12.0f, 5.6f, 5.6f, 0.0f,
                         juce::MathConstants<float>::pi * 0.22f,
                         juce::MathConstants<float>::pi * 1.72f, true);
    P innerStroked;
    juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (innerStroked, inner);
    p.addPath (innerStroked);

    // Core and the satellite that gives the mark its record-head reading.
    p.addEllipse (10.1f, 10.1f, 3.8f, 3.8f);
    p.addEllipse (15.6f, 5.6f, 3.0f, 3.0f);

    // The tail leaving the ring at the lower left.
    P tail;
    tail.startNewSubPath (5.0f, 17.6f);
    tail.lineTo (1.8f, 21.4f);
    P tailStroked;
    juce::PathStrokeType (2.3f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded).createStrokedPath (tailStroked, tail);
    p.addPath (tailStroked);
    return p;
}

// -----------------------------------------------------------------------------
//  Draw a 24 x 24 glyph into an arbitrary rectangle.
// -----------------------------------------------------------------------------
inline void draw (juce::Graphics& g, const P& path, juce::Rectangle<float> area,
                  juce::Colour colour, float alpha = 1.0f)
{
    g.setColour (colour.withMultipliedAlpha (alpha));
    g.fillPath (path, path.getTransformToScaleToFit (area, true));
}

/** Same, with a bloom - used for lit/selected states. */
inline void drawGlowing (juce::Graphics& g, const P& path, juce::Rectangle<float> area,
                         juce::Colour colour, float intensity = 0.6f)
{
    const auto t = path.getTransformToScaleToFit (area, true);
    P scaled (path);
    scaled.applyTransform (t);

    g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, intensity * 0.35f)));
    g.strokePath (scaled, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    g.setColour (colour);
    g.fillPath (scaled);
}

} // namespace keepthat::icons
