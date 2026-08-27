/*
    Assets.h - the v1.4 APPROVED FINAL artwork, loaded from the bundle.

    v1.4 is the current build authority. Its
    `00_APPROVED_REFERENCE/KEEP_THAT_Approved_UI_Latest_Logo_1491x1055.png` is
    the original approved design with the new logo dropped in - the layout did
    not change - and `13_LATEST_APPROVED_SLICED_ASSETS` supplies every control
    as an individual production PNG in that approved style. v1.3's component
    art was a different, chunkier look that did not match the reference, so it
    is superseded; only its filmstrips (knobs, meters, HUD motion) carry over,
    which is the priority order the v1.4 prompt sets out.

    The staging layout inside Contents/Resources/Assets mirrors the pack:

        chassis.png, chassis_2x.png, logo.png
        hud/       6 layers + orbit/pulse filmstrips
        buttons/   keep_last_*, capture_selector_*, save_*, transport_*,
                   rename_*, export_button_*, action_*_* shells
        switches/  toggle_[on|off][_hover|_disabled], toggle_compact_*
        meters/    vertical/horizontal filmstrips + tracks
        knobs/     macro / small / fine_tune_gold 128-frame strips
        viz/       waveform + timeline grids, trim handles, playhead,
                   confidence bars
        tiles/     recent_keep_tile_*, phrase_card_*, recovery_row_*
        icons/     PNG_64 icon family

    The rule stays what it was: art carries the chrome, live drawing carries
    the state. Values, waveforms, meters, captions and timers are all live
    JUCE painting on top of the art (13_JUCE_HANDOFF's production asset rule).

    Load failures are recorded and reported BY NAME - a wrong-looking build is
    far more often a load problem than an art problem. `make uishot` prints
    them and exits non-zero. Every component keeps its procedural fallback, so
    a missing file degrades visibly but never to a blank hole.
*/

#pragma once
#include <JuceHeader.h>

namespace keepthat
{

class Assets
{
public:
    /** Contents/Resources/Assets in the running bundle, or the source tree's
        Assets/ when running a development tool out of build/. */
    static juce::File assetsDirectory();

    /** Loads and caches a PNG by pack-relative path ("buttons/save_normal.png").
        Returns an invalid image on failure and records it. */
    static const juce::Image& image (const juce::String& relativePath);

    static bool has (const juce::String& relativePath);

    /** Existence check that does NOT record a miss. For probing optional
        states down a fallback chain - the v1.3 blank shells ship no
        "selected", for instance - where a miss is expected, not a fault. */
    static bool exists (const juce::String& relativePath);

    /** One frame of a vertically-stacked filmstrip, as a cheap view into the
        cached strip (no copy). `position` 0..1 picks the frame. */
    static juce::Image filmstripFrame (const juce::String& relativePath,
                                       int frameCount, float position);

    /** A cached copy of an asset - or one filmstrip frame of it - rendered at
        exactly `w` x `h`.

        This is the difference between an interface that idles near nothing and
        one that idles at 65 % of a core. Resampling a 586 px ring and six
        160 px knob frames on every one of 60 frames a second is most of a CPU;
        done once and blitted 1:1 thereafter it is free. MESSAGE THREAD only. */
    static const juce::Image& scaled (const juce::String& relativePath, int w, int h,
                                      int frameCount = 1, int frameIndex = 0);

    /** Draws `relativePath` scaled uniformly to fit `area`, centred. */
    static bool drawFitted (juce::Graphics&, const juce::String& relativePath,
                            juce::Rectangle<float> area, float opacity = 1.0f);

    /** Like drawFitted, but fits the asset's opaque CONTENT to `area` rather
        than its canvas. The state families are authored with generous
        transparent padding for their glow, so fitting the canvas leaves the
        control looking two sizes too small inside its slot. */
    static bool drawFittedTrimmed (juce::Graphics&, const juce::String& relativePath,
                                   juce::Rectangle<float> area, float opacity = 1.0f,
                                   float bleed = 0.0f);

    /** Stretches the asset's opaque CONTENT to exactly `area`. For the row and
        card shells, which are authored short and have to fill a wide slot. */
    static bool drawStretchedTrimmed (juce::Graphics&, const juce::String& relativePath,
                                      juce::Rectangle<float> area, float opacity = 1.0f);

    /** Draws `relativePath` stretched to exactly `area` (for bars/plates). */
    static bool drawStretched (juce::Graphics&, const juce::String& relativePath,
                               juce::Rectangle<float> area, float opacity = 1.0f);

    static int loadFailureCount();
    static juce::String describeFailures();

private:
    Assets() = delete;
};

// Pack-relative paths, in one place so a typo is a compile error rather than
// a silently missing image.
namespace art
{
    inline const char* logo         = "logo.png";

    // The approved ring with an EMPTY centre, so the live clock can be drawn
    // into it rather than covering a baked one.
    inline const char* hudRing      = "hud/ring.png";
    // v1.3's 64-frame orbit and 32-frame pulse are deliberately absent: they
    // were authored for that pack's chunky segmented wheel and laid gold
    // segments straight across this ring's thin arcs. The ring breathes from
    // its own colours instead.

    // Halved from the pack's 128 frames (see tools/png.py filmstrip). At the
    // sizes these knobs are drawn, 64 steps is well under one pixel of pointer
    // movement per frame, and it takes ~6 MB per installed format off a plugin
    // that is meant to be free. 12_LAYOUT/filmstrip_metadata.json still says
    // 128 - these constants are the truth for what actually ships.
    inline constexpr int knobFrames = 64;
    inline const char* knobMacro    = "knobs/macro_knob_64frames_160x160_vertical.png";
    inline const char* knobSmall    = "knobs/small_knob_64frames_112x112_vertical.png";
    inline const char* knobGold     = "knobs/fine_tune_gold_knob_64frames_160x160_vertical.png";

    inline const char* meterV       = "meters/meter_vertical_64frames_60x240_vertical.png";
    inline const char* meterH       = "meters/meter_horizontal_64frames_320x34_vertical.png";
    inline const char* trimLeft     = "meters/trim_handle_left.png";
    inline const char* trimRight    = "meters/trim_handle_right.png";
    inline const char* playhead     = "meters/playhead_marker.png";

    /** The eight capture selectors, labels baked in. */
    inline juce::String selector (const juce::String& id, bool selected)
    {
        return "selectors/" + id + (selected ? "_selected" : "_normal") + ".png";
    }

    /** keep_last_normal / _hover / _pressed / _disabled etc. */
    inline juce::String button (const juce::String& base, const juce::String& state)
    {
        return "buttons/" + base + "_" + state + ".png";
    }

    inline juce::String toggle (bool on, const juce::String& suffix = {})
    {
        return "switches/toggle_" + juce::String (on ? "on" : "off")
             + (suffix.isEmpty() ? "" : "_" + suffix) + ".png";
    }

    inline juce::String tile (const juce::String& name) { return "tiles/" + name + ".png"; }
    inline juce::String icon (const juce::String& name) { return "icons/" + name + ".png"; }
}

} // namespace keepthat
