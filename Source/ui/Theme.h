/*
    Theme.h - colour tokens, font resolution and the design-space layout.

    Every rectangle here is in the approved 1491 x 1055 design coordinate
    system (Spec/.../08_LAYOUT/layout_1491x1055.json). The editor scales one
    fixed-size content component uniformly, so nothing below ever needs
    runtime scaling maths.

    Two authorities, in this order:

      * layout_1491x1055.json / control_map.csv for panel-level bounds
      * the approved mockup itself for anything inside a panel

    Where the two disagree the mockup wins - it is the declared visual
    authority - and the JSON value is kept in a comment so the drift is on the
    record rather than lost. Values marked "measured" were read off
    00_REFERENCE/KEEP_THAT_Approved_UI_1491x1055.png with tools/png.py.

    NOTE: juce::Rectangle has no constexpr constructor in JUCE 9, so these
    tables are `inline const`, not `constexpr`.
*/

#pragma once
#include <JuceHeader.h>

namespace keepthat
{

// True when the v1.3 PRO component artwork loaded - logo, HUD layers, knob
// filmstrips, button state families, toggles, meters, tiles and icons. Every
// control prefers the art and keeps its procedural drawing as the fallback.
//
// NOTE this does NOT cover 02_CHASSIS. That plate is drawn to a different
// arrangement than the approved reference and layout_1491x1055.json - its
// capture-preview, recent-keeps and bottom-control plates are all narrower -
// and both of those are the declared visual authority, with the QA gate being
// a 50% overlay against the reference. So panel plates stay procedural at the
// approved rects and the chassis is kept in Spec/ for reference only.
inline bool& componentArtwork()
{
    static bool on = true;
    return on;
}

// Set by the headless tools (make uishot) so display timers keep updating
// without a visible window peer; isShowing() is false headlessly.
inline bool& headlessRefreshMode()
{
    static bool mode = false;
    return mode;
}

// -----------------------------------------------------------------------------
//  Colour tokens.
//
//  The four in 09_JUCE_HANDOFF/KEEP_THAT_DesignTokens.h verbatim, plus the
//  intermediate values the mockup uses that the token file does not name.
// -----------------------------------------------------------------------------
namespace tokens
{
    // --- supplied (KEEP_THAT_DesignTokens.h) ---------------------------------
    inline const juce::Colour bg          { 0xff090c12 };
    inline const juce::Colour panel       { 0xff10151d };
    inline const juce::Colour panel2      { 0xff0d1219 };
    inline const juce::Colour accentRed   { 0xffff4b2e };
    inline const juce::Colour accentCyan  { 0xff00d7ff };
    inline const juce::Colour accentGold  { 0xffffb24a };
    inline const juce::Colour textPrimary { 0xfff2f4f7 };
    inline const juce::Colour textSecond  { 0xffbcc6d2 };
    inline const juce::Colour textMuted   { 0xff8190a2 };

    // --- measured off the mockup ---------------------------------------------
    inline const juce::Colour bg0         { 0xff05070b };   // outer chassis
    inline const juce::Colour well        { 0xff070a0f };   // sunken wells
    inline const juce::Colour panel3      { 0xff141b25 };   // raised rows
    inline const juce::Colour stroke      { 0xff1e2733 };   // panel borders
    inline const juce::Colour strokeHi    { 0xff2c3a49 };   // lit borders
    inline const juce::Colour redDeep     { 0xffc42a12 };   // gradient foot
    inline const juce::Colour redHot      { 0xffff7a4d };   // gradient crown
    inline const juce::Colour cyanDeep    { 0xff0784b4 };
    inline const juce::Colour cyanPale    { 0xff9ceeff };
    inline const juce::Colour green       { 0xff44e08a };   // INPUT GOOD
    inline const juce::Colour metalHi     { 0xffe8ecf1 };   // knob cap crown
    inline const juce::Colour metalLo     { 0xff3d444d };   // knob cap foot
}

// -----------------------------------------------------------------------------
//  Fonts - system lookup only, no bundled files. The mockup's face is a
//  condensed technical grotesque; Inter / SF Pro Display are the closest
//  things that ship on a customer's Mac.
// -----------------------------------------------------------------------------
struct Fonts
{
    static const juce::String& family()
    {
        static const juce::String resolved = []
        {
            const juce::StringArray installed = juce::Font::findAllTypefaceNames();
            for (const char* want : { "Inter Display", "Inter", "SF Pro Display",
                                      "Helvetica Neue", "Arial" })
                if (installed.contains (juce::String (want)))
                    return juce::String (want);
            return juce::Font (juce::FontOptions{}).getTypefaceName();
        }();
        return resolved;
    }

    static juce::Font make (float px, bool medium = false, bool bold = false)
    {
        if (bold)
            return juce::Font (juce::FontOptions (family(), px, juce::Font::bold));

        if (medium)
        {
            juce::Font f (juce::FontOptions (family(), "Medium", px));
            if (f.getTypefacePtr() != nullptr && f.getTypefaceStyle() == "Medium")
                return f;
        }
        return juce::Font (juce::FontOptions (family(), px, juce::Font::plain));
    }

    // Typography roles. Sizes are design-space pixels, measured off the mockup.
    static juce::Font logo()        { return make (54.0f, false, true).withExtraKerningFactor (-0.02f)
                                                                     .withHorizontalScale (0.94f); }
    static juce::Font logoSub()     { return make (17.0f, true).withExtraKerningFactor (0.02f); }
    static juce::Font panelTitle()  { return make (19.0f, false, true).withExtraKerningFactor (0.045f); }
    static juce::Font presetName()  { return make (21.0f, true); }
    static juce::Font rowTitle()    { return make (15.0f, false, true).withExtraKerningFactor (0.035f); }
    static juce::Font rowSub()      { return make (11.5f, true); }
    static juce::Font fieldLabel()  { return make (12.0f, false, true).withExtraKerningFactor (0.07f); }
    static juce::Font fieldValue()  { return make (21.0f, true); }
    static juce::Font tiny()        { return make (10.0f, true).withExtraKerningFactor (0.05f); }
    static juce::Font small()       { return make (11.5f, true).withExtraKerningFactor (0.03f); }
    static juce::Font buttonLabel() { return make (14.0f, false, true).withExtraKerningFactor (0.04f); }
    static juce::Font iconCaption() { return make (10.5f, false, true).withExtraKerningFactor (0.06f); }
    static juce::Font keepLast()    { return make (64.0f, false, true).withExtraKerningFactor (0.02f)
                                                                     .withHorizontalScale (0.95f); }
    static juce::Font hudTime()     { return make (74.0f, false, true).withExtraKerningFactor (-0.01f)
                                                                     .withHorizontalScale (0.88f); }
    static juce::Font hudLabel()    { return make (26.0f, false, true).withExtraKerningFactor (0.08f); }
    static juce::Font hudState()    { return make (20.0f, false, true).withExtraKerningFactor (0.10f); }
    static juce::Font hudMax()      { return make (17.0f, true).withExtraKerningFactor (0.06f); }
    static juce::Font ringTick()    { return make (12.0f, true).withExtraKerningFactor (0.04f); }
    static juce::Font knobTitle()   { return make (12.5f, false, true).withExtraKerningFactor (0.075f); }
    static juce::Font knobValue()   { return make (17.0f, true); }
    static juce::Font knobRange()   { return make (10.5f, true); }
    static juce::Font cardName()    { return make (12.0f, true); }
    static juce::Font footer()      { return make (13.0f, true).withExtraKerningFactor (0.42f); }
};

struct Design
{
    static constexpr int width  = 1491;
    static constexpr int height = 1055;
    static constexpr float aspect = 1491.0f / 1055.0f;
    static constexpr int minWidth  = 1044;   // 70 %
    static constexpr int minHeight = 739;
    static constexpr int maxWidth  = 2237;   // 150 %
    static constexpr int maxHeight = 1583;
    static constexpr float corner  = 14.0f;  // KEEP_THAT_DesignTokens.h
};

// -----------------------------------------------------------------------------
//  Layout
// -----------------------------------------------------------------------------
namespace layout
{
    using R = juce::Rectangle<int>;

    // --- panel bounds ------------------------------------------------------
    //
    // These start from layout_1491x1055.json but close the dead areas it
    // leaves: 90 px under LIVE INPUT, 30 under RECOVERY TOOLS, and an L-shaped
    // hole to the right of RECENT KEEPS and under EXPORT. Panels now run to a
    // consistent 8 px gutter and the canvas reads as one filled surface.
    // Internal contents were re-spaced to match rather than left floating at
    // the top of a taller box.
    inline const R header          {   10,   10, 1471,  90 };
    inline const R liveInput       {   15,  115,  415, 470 };   // was h 385
    inline const R bufferHud       {  442,   84,  630, 501 };
    inline const R recoveryTools   { 1086,  114,  392, 471 };   // was h 454
    inline const R transport       {   15,  592,  105, 150 };
    inline const R capturePreview  {  128,  592, 1024, 150 };
    inline const R captureActions  { 1160,  592,   54, 150 };
    inline const R exportDest      { 1222,  592,  256, 293 };   // was h 200
    inline const R recentKeeps     {   15,  750, 1199, 135 };   // was w 1177
    inline const R bottomControls  {   15,  893, 1463, 125 };
    inline const R footer          {   10, 1018, 1471,  30 };

    // --- header internals (control_map.csv + measured) ---------------------
    inline const R logoMark        {   22,   22,   72,  72 };
    inline const R logoText        {  118,   18,  340,  76 };
    // The v1.4 logo is one piece - mark, wordmark and rule together - so it
    // gets the whole left block of the header and fills it. Sized against the
    // approved reference, whose logo measures 396 x 103 starting at x=15 and
    // runs a few pixels past the header's nominal bottom edge, as this does.
    inline const R logoArt         {   15,    4,  420,  100 };
    inline const R presetBar       {  508,   26,  458,  50 };   // measured
    inline const R headerUtils     { 1030,   18,  452,  66 };   // 6 icon cells

    // --- live input --------------------------------------------------------
    inline const R liveTitle       {   34,  132,  200,  28 };
    inline const R alwaysListening {  250,  134,  172,  22 };
    inline const R sourceLabel     {   34,  174,   70,  22 };
    inline const R sourceBox       {  112,  170,  300,  30 };
    inline const R meterScale      {   26,  222,   22,  248 };
    inline const R meterPair       {   52,  216,   62,  254 };
    inline const R inputWave       {  126,  216,  286,  112 };
    inline const R readoutGrid     {  126,  340,  286,  130 };
    inline const R modeCell        {   26,  492,  222,   54 };
    inline const R inputCell       {  268,  492,  144,   54 };

    // --- buffer HUD --------------------------------------------------------
    // Ring centre and radii, measured off the approved mockup by taking radial
    // profiles at 205 and 335 degrees with tools/png.py and reading off where
    // the bright bands actually fall. Both rays agree, which is what makes
    // these trustworthy. The layers, outside in:
    //
    //     200..216   the DOMINANT outer arc - thick, bright, heavy bloom
    //     184..198   sparse tick ring, sitting between the two big arcs
    //     170..182   the mid arc
    //     155..159   a thin accent line
    //     136..147   the inner bright arc
    //     118..132   fine dashed segment ring
    //     107        inner disc edge
    //
    // The first entry is the one an earlier pass missed completely: the ring
    // was built to stop at 176, so the biggest, brightest band in the approved
    // art was simply absent and the whole HUD read too small and too sparse.
    // Every bright band is a thin core plus wide bloom, never a solid fill.
    inline constexpr float ringCentreX   = 745.0f;
    inline constexpr float ringCentreY   = 310.0f;
    inline constexpr float rDiscEdge     = 107.0f;
    inline constexpr float rDashInner    = 118.0f;
    inline constexpr float rDashOuter    = 132.0f;
    inline constexpr float rInnerArcMid  = 141.0f;   // bright arc, core ~8 px
    inline constexpr float rAccentMid    = 157.0f;   // thin accent line
    inline constexpr float rMidArcMid    = 176.0f;   // mid arc, core ~12 px
    inline constexpr float rTickInner    = 184.0f;
    inline constexpr float rTickOuter    = 198.0f;
    inline constexpr float rOuterArcMid  = 208.0f;   // dominant arc, core ~16 px

    // Where the v1.4 ring slice is drawn.
    //
    // Fitted by measurement, not by eye. The slice and the approved reference
    // are the same artwork, so three landmarks pin it exactly: the leftmost
    // and rightmost lit pixels give the scale, and the topmost gives the
    // vertical offset. Against the reference those sit at x 522 and 993 and
    // y 109; an earlier placement put them at 531, 964 and 162 - a ring 8.8 %
    // too small and 40 px too low, which is what made the HUD look wrong.
    //
    //     side   = 521 x (993-522)/(964-531) = 567
    //     centre = (755, 270)
    //
    // The art's own top rows are transparent, so drawing from above the
    // panel's top edge costs nothing visible.
    inline constexpr float ringArtSide = 567.0f;
    inline constexpr float ringArtCentreX = 755.0f;
    inline constexpr float ringArtCentreY = 270.0f;
    inline const R ringArt {
        (int) std::round (ringArtCentreX - ringArtSide * 0.5f),
        (int) std::round (ringArtCentreY - ringArtSide * 0.5f),
        (int) std::round (ringArtSide),
        (int) std::round (ringArtSide)
    };

    // The ring art's own scale says -4:00 at nine o'clock, so the ring
    // represents a four-minute window. Buffer fill is measured against THAT,
    // not against the 8-minute maximum - otherwise a full ring would be
    // claiming history the labels do not cover.
    inline constexpr double ringSpanSeconds = 240.0;

    // The artwork's TICK RING - the dashes that run right around, between the
    // inner and outer neon arc pairs. Measured off Assets/hud/ring.png as
    // fractions of the art's side (each dash is radial, so the band has real
    // depth). The elapsed-time marker rides this band.
    inline constexpr float ringTickInnerF = 0.232f;
    inline constexpr float ringTickOuterF = 0.285f;

    inline const R hudState        {  625,  230,  240,  28 };
    inline const R hudTime         {  595,  256,  300,  70 };
    inline const R hudAvailable    {  605,  324,  280,  32 };
    inline const R hudMax          {  625,  358,  240,  24 };
    inline const R hudLabelTop     {  700,   86,   90,  20 };
    inline const R hudLabelLeft    {  484,  300,   60,  20 };
    inline const R hudLabelRight   {  948,  272,   50,  40 };   // "0:00" + "NOW"

    // --- keep last + capture length ---------------------------------------
    inline const R keepChassis     {  462,  388,  562, 112 };   // wings + well
    // v1.4's keep_last_* slices are 1010 x 203 (4.98:1) against control_map's
    // 545 x 88 (6.2:1). The height between the HUD readout and the selector
    // rows is the binding constraint, so the slot keeps the approved centre
    // line and takes the art's aspect: 520 x 104 at y 400.
    inline const R keepButton      {  486,  400,  520,  104 };
    // Two rows of four, 116 px wide on a 118.5 pitch (measured at y=508/546).
    inline const R lengthRow1      {  511,  505,  468,  36 };
    inline const R lengthRow2      {  511,  545,  468,  36 };
    inline constexpr int lengthCols = 4;

    // --- recovery tools ----------------------------------------------------
    inline const R recoveryTitle   { 1102,  132,  260,  28 };
    inline const R recoveryInfo    { 1428,  134,   24,  24 };
    inline const R recoveryStack   { 1096,  164,  372, 312 };   // 6 rows
    inline constexpr int recoveryRows = 6;
    inline const R phraseCard      { 1096,  486,  372,  92 };

    // --- capture preview ---------------------------------------------------
    inline const R previewTitle    {  145,  598,  180,  24 };
    inline const R previewRange    {  333,  600,  320,  20 };
    inline const R previewModeLbl  {  966,  600,   50,  20 };
    inline const R previewModeBox  { 1024,  596,  100,  26 };
    inline const R previewWave     {  141,  624,  998,   80 };
    inline const R previewTimeRule {  141,  704,  998,   17 };
    inline const R previewBarRule  {  141,  721,  998,   18 };

    // --- action columns ----------------------------------------------------
    inline const R transportStack  {   20,  596,   96, 140 };   // PLAY/STOP/TRIM
    // Cut to the art's aspect. v1.4's rename / save_wav / drag_to_daw slices
    // are square buttons with their label already on them, so a 62-wide cell
    // holding 48-wide art left a gap down both sides that read as a missing
    // background.
    inline const R actionStack     { 1160,  592,   54, 150 };   // RENAME/WAV/DRAG

    // --- export / destination ---------------------------------------------
    // The panel now runs the full height from the capture row down to the
    // bottom strip, so its five buttons have real room instead of a strip of
    // dead canvas underneath them.
    inline const R exportTitle     { 1232,  602,  240,  24 };
    inline const R exportRow1      { 1232,  634,  236,  116 };  // 2 cells
    inline const R exportRow2      { 1232,  758,  236,  116 };  // 3 cells

    // --- recent keeps ------------------------------------------------------
    inline const R keepsTitle      {   32,  760,  220,   26 };
    inline const R keepsCount      { 1100,  764,   96,   18 };
    inline const R keepsStrip      {   22,  794, 1185,   82 };
    inline constexpr int keepsVisible = 8;

    // --- bottom controls ---------------------------------------------------
    inline const R macroGroup      {   15,  893,  920, 125 };   // 5 knob cells
    inline const R outputGroup     {  941,  893,  537, 125 };
    inline constexpr int macroCells = 5;
    inline const R outputKnobCell  {  948,  897,  150, 118 };
    inline const R outputMeters    { 1112,  916,  292,   62 };
    // The approved art draws MUTE as a filled button with the word inside,
    // not an icon with a caption underneath.
    inline const R muteButton      { 1412,  920,   64,   46 };

    // --- footer ------------------------------------------------------------
    inline const R footerVersion   {   22, 1026,   90,  20 };
    inline const R footerTagline   {  520, 1024,  450,  22 };
    inline const R footerYear      { 1378, 1026,   58,  22 };
    inline const R footerMark      { 1446, 1024,   28,  24 };

    // Splits a row into `n` equal cells with `gap` between them.
    inline R cell (juce::Rectangle<int> row, int index, int n, int gap)
    {
        const float w = (row.getWidth() - gap * (n - 1)) / (float) n;
        return { row.getX() + (int) std::round (index * (w + gap)), row.getY(),
                 (int) std::round (w), row.getHeight() };
    }
}

} // namespace keepthat
