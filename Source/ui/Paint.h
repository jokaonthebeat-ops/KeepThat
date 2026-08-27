/*
    Paint.h - the shared drawing vocabulary.

    Everything in KEEP THAT! is drawn live; the reference PNGs in Spec/ are for
    matching, never for blitting (JUCE_IMPLEMENTATION_SPEC, "Critical Rule").
    So the premium finish has to come from here: bevels, wells, neon bloom and
    brushed metal, all built from primitives.

    The bloom helpers stroke the same path several times at growing width and
    falling alpha. That is a cheap fake of a gaussian glow and it is what keeps
    the accents luminous without a blur pass - JUCE has no cheap blur, and a
    real one at 60 fps on six panels is not affordable.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"

namespace keepthat::paint
{

// -----------------------------------------------------------------------------
//  Neon
// -----------------------------------------------------------------------------

/** Strokes `path` several times, widening and fading, to fake a bloom. */
inline void glowPath (juce::Graphics& g, const juce::Path& path, juce::Colour colour,
                      float coreWidth, float spread = 6.0f, float intensity = 1.0f,
                      int layers = 4)
{
    for (int i = layers; i >= 1; --i)
    {
        const float t = i / (float) layers;
        const float w = coreWidth + spread * t;
        const float a = intensity * 0.16f * (1.0f - t) + 0.03f;
        g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, a)));
        g.strokePath (path, juce::PathStrokeType (w, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
    g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, intensity)));
    g.strokePath (path, juce::PathStrokeType (coreWidth, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

/** A soft radial halo - used behind the KEEP LAST button and the HUD ring. */
inline void halo (juce::Graphics& g, juce::Point<float> centre, float radius,
                  juce::Colour colour, float intensity = 0.35f)
{
    juce::ColourGradient grad (colour.withAlpha (intensity), centre.x, centre.y,
                               colour.withAlpha (0.0f), centre.x + radius, centre.y, true);
    grad.addColour (0.45, colour.withAlpha (intensity * 0.42f));
    grad.addColour (0.75, colour.withAlpha (intensity * 0.12f));
    g.setGradientFill (grad);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
}

/** Text with a matching bloom behind it. */
inline void glowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                      const juce::Font& font, juce::Colour colour,
                      juce::Justification just = juce::Justification::centred,
                      float intensity = 0.5f)
{
    g.setFont (font);
    g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, intensity * 0.30f)));
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx != 0 || dy != 0)
                g.drawText (text, area.translated (dx, dy), just, false);

    g.setColour (colour);
    g.drawText (text, area, just, false);
}

// -----------------------------------------------------------------------------
//  Surfaces
// -----------------------------------------------------------------------------

/** A raised panel: gradient face, hairline border, lit top edge, cast shadow. */
inline void panelSurface (juce::Graphics& g, juce::Rectangle<float> r, float corner,
                          juce::Colour top = tokens::panel,
                          juce::Colour bottom = tokens::panel2,
                          juce::Colour border = tokens::stroke)
{
    // Cast shadow first, outside the face.
    for (int i = 6; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.055f));
        g.fillRoundedRectangle (r.expanded ((float) i).translated (0.0f, i * 0.35f),
                                corner + i);
    }

    g.setGradientFill (juce::ColourGradient (top, r.getCentreX(), r.getY(),
                                             bottom, r.getCentreX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);

    // Lit top edge - the single cheapest cue that a surface is raised.
    juce::Path lip;
    lip.addRoundedRectangle (r.reduced (1.0f), corner - 1.0f);
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<int> ((int) r.getX(), (int) r.getY(),
                                              (int) r.getWidth(), (int) (r.getHeight() * 0.5f)));
    g.setColour (juce::Colours::white.withAlpha (0.055f));
    g.strokePath (lip, juce::PathStrokeType (1.4f));
    g.restoreState();

    g.setColour (border);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

/** A sunken well: waveform beds, meter beds, readout cells. */
inline void wellSurface (juce::Graphics& g, juce::Rectangle<float> r, float corner,
                         juce::Colour face = tokens::well)
{
    g.setGradientFill (juce::ColourGradient (face.darker (0.35f), r.getCentreX(), r.getY(),
                                             face.brighter (0.08f), r.getCentreX(), r.getBottom(),
                                             false));
    g.fillRoundedRectangle (r, corner);

    // Inner shadow along the top and left, highlight along the bottom.
    juce::Path inner;
    inner.addRoundedRectangle (r.reduced (0.5f), corner);
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<int> ((int) r.getX(), (int) r.getY(),
                                              (int) r.getWidth(), (int) (r.getHeight() * 0.45f)));
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (inner, juce::PathStrokeType (2.0f));
    g.restoreState();

    g.saveState();
    g.reduceClipRegion (juce::Rectangle<int> ((int) r.getX(),
                                              (int) (r.getBottom() - r.getHeight() * 0.35f),
                                              (int) r.getWidth(), (int) (r.getHeight() * 0.35f) + 1));
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.strokePath (inner, juce::PathStrokeType (1.0f));
    g.restoreState();

    g.setColour (tokens::stroke.withAlpha (0.75f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

/** The faint technical grid inside waveform wells. */
inline void wellGrid (juce::Graphics& g, juce::Rectangle<float> r,
                      int cols, int rowsCount, juce::Colour colour)
{
    g.setColour (colour);
    for (int i = 1; i < cols; ++i)
    {
        const float x = r.getX() + r.getWidth() * i / (float) cols;
        g.fillRect (x, r.getY(), 1.0f, r.getHeight());
    }
    for (int i = 1; i < rowsCount; ++i)
    {
        const float y = r.getY() + r.getHeight() * i / (float) rowsCount;
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
    }
}

/** Perforated texture - the HUD's inner disc and the chassis wings. */
inline void perforate (juce::Graphics& g, juce::Rectangle<float> r, float pitch,
                       juce::Colour colour, float dotSize = 1.6f)
{
    g.setColour (colour);
    for (float y = r.getY() + pitch * 0.5f; y < r.getBottom(); y += pitch)
        for (float x = r.getX() + pitch * 0.5f; x < r.getRight(); x += pitch)
            g.fillEllipse (x, y, dotSize, dotSize);
}

// -----------------------------------------------------------------------------
//  Metal
// -----------------------------------------------------------------------------

/** A brushed aluminium knob cap: vertical gradient, circular grain, specular. */
inline void metalCap (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;

    // Rim, so the cap reads as a machined part sitting in a recess.
    g.setColour (juce::Colour (0xff05070a));
    g.fillEllipse (r.expanded (2.0f));

    g.setGradientFill (juce::ColourGradient (tokens::metalHi, c.x, r.getY(),
                                             tokens::metalLo, c.x, r.getBottom(), false));
    g.fillEllipse (r);

    // Circular brushed grain.
    g.saveState();
    juce::Path clip;
    clip.addEllipse (r);
    g.reduceClipRegion (clip);
    for (float t = 0.16f; t < 1.0f; t += 0.085f)
    {
        const float rr = rad * t;
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 0.9f);
        g.setColour (juce::Colours::black.withAlpha (0.045f));
        g.drawEllipse (c.x - rr - 1.0f, c.y - rr - 1.0f, rr * 2.0f + 2.0f, rr * 2.0f + 2.0f, 0.7f);
    }
    // Specular sweep across the upper left.
    juce::ColourGradient spec (juce::Colours::white.withAlpha (0.22f), r.getX(), r.getY(),
                               juce::Colours::transparentWhite, c.x, c.y, false);
    g.setGradientFill (spec);
    g.fillEllipse (r);
    g.restoreState();

    // Edge definition.
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.drawEllipse (r.reduced (0.5f), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (r.expanded (1.5f), 1.4f);
}

// -----------------------------------------------------------------------------
//  Shapes
// -----------------------------------------------------------------------------

/** A rectangle with cut corners - the KEEP THAT! chassis motif. */
inline juce::Path chamfered (juce::Rectangle<float> r, float cut)
{
    juce::Path p;
    p.startNewSubPath (r.getX() + cut, r.getY());
    p.lineTo (r.getRight() - cut, r.getY());
    p.lineTo (r.getRight(), r.getY() + cut);
    p.lineTo (r.getRight(), r.getBottom() - cut);
    p.lineTo (r.getRight() - cut, r.getBottom());
    p.lineTo (r.getX() + cut, r.getBottom());
    p.lineTo (r.getX(), r.getBottom() - cut);
    p.lineTo (r.getX(), r.getY() + cut);
    p.closeSubPath();
    return p;
}

/** The KEEP LAST silhouette: chamfered, with the ends pulled to a shallow point. */
inline juce::Path capsuleHex (juce::Rectangle<float> r, float cut, float point)
{
    juce::Path p;
    const float midY = r.getCentreY();
    p.startNewSubPath (r.getX() + cut, r.getY());
    p.lineTo (r.getRight() - cut, r.getY());
    p.lineTo (r.getRight(), r.getY() + cut * 0.55f);
    p.lineTo (r.getRight() + point, midY);
    p.lineTo (r.getRight(), r.getBottom() - cut * 0.55f);
    p.lineTo (r.getRight() - cut, r.getBottom());
    p.lineTo (r.getX() + cut, r.getBottom());
    p.lineTo (r.getX(), r.getBottom() - cut * 0.55f);
    p.lineTo (r.getX() - point, midY);
    p.lineTo (r.getX(), r.getY() + cut * 0.55f);
    p.closeSubPath();
    return p;
}

/** An annular sector, for every arc in the HUD ring. */
inline juce::Path ringSegment (juce::Point<float> centre, float rInner, float rOuter,
                               float fromRadians, float toRadians)
{
    juce::Path p;
    p.addCentredArc (centre.x, centre.y, rOuter, rOuter, 0.0f, fromRadians, toRadians, true);
    p.addCentredArc (centre.x, centre.y, rInner, rInner, 0.0f, toRadians, fromRadians, false);
    p.closeSubPath();
    return p;
}

/** A stroked arc - `addCentredArc` with `startAsNewSubPath` set, kept honest. */
inline juce::Path arcPath (juce::Point<float> centre, float radius,
                           float fromRadians, float toRadians)
{
    juce::Path p;
    p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, fromRadians, toRadians, true);
    return p;
}

// -----------------------------------------------------------------------------
//  Envelopes
// -----------------------------------------------------------------------------

/** Fills a min/max envelope into `area`, mirrored around the centre line. */
inline void waveform (juce::Graphics& g, juce::Rectangle<float> area,
                      const float* lo, const float* hi, int count,
                      juce::Colour colour, float gain = 1.0f, bool withGlow = true)
{
    if (count <= 0 || area.getWidth() <= 0.0f)
        return;

    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.5f;
    const float step = area.getWidth() / (float) count;
    const float barW = juce::jmax (1.0f, step * 0.72f);

    juce::Path body;
    for (int i = 0; i < count; ++i)
    {
        const float x = area.getX() + i * step;
        const float top = midY - juce::jlimit (0.0f, 1.0f, hi[i] * gain) * halfH;
        const float bot = midY - juce::jlimit (-1.0f, 0.0f, lo[i] * gain) * halfH;
        body.addRectangle (x, top, barW, juce::jmax (1.0f, bot - top));
    }

    if (withGlow)
    {
        g.setColour (colour.withAlpha (0.20f));
        g.fillPath (body, juce::AffineTransform::scale (1.0f, 1.14f, 0.0f, midY));
    }
    g.setColour (colour);
    g.fillPath (body);

    // A brighter core along the centre reads as signal density.
    g.setColour (colour.brighter (0.5f).withAlpha (0.55f));
    g.fillRect (area.getX(), midY - 0.5f, area.getWidth(), 1.0f);
}

// -----------------------------------------------------------------------------
//  Misc
// -----------------------------------------------------------------------------

inline float dbToNorm (float db, float floorDb = -60.0f)
{
    return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
}

inline float gainToNorm (float gain, float floorDb = -60.0f)
{
    return dbToNorm (juce::Decibels::gainToDecibels (gain, floorDb), floorDb);
}

} // namespace keepthat::paint
