# Claude Master Build Prompt — KEEP THAT! v1.0

You are building the **KEEP THAT!** JUCE plugin UI and workflow shell. Use the supplied assets pack, reference screenshots and coordinate files as the sole visual authority.

## Mission
Create a **cutting-edge, premium, animated, interactive plugin UI** that matches the approved KEEP THAT! mockup at exactly **1491 x 1055**. The product is a free, real utility plugin for recovering recent audio from an always-on rolling buffer.

## First milestone only
For the first milestone, build the complete UI and interaction shell with realistic placeholder data. Do **not** spend time on final DSP or full export workflow until the UI screenshot is visually approved.

## Non-negotiable rules
1. Match the approved layout extremely closely.
2. Do not use default JUCE widgets visually.
3. Recreate the panel shell, glows, typography hierarchy and controls with a premium finish.
4. Keep the interface lively: animated ring, moving waveforms, responsive meters, subtle glows.
5. Preserve the exact panel structure.
6. Use a locked aspect ratio.
7. Implement hover/pressed/selected/disabled states for all major controls.

## Required live elements in milestone 1
- Header logo and preset bar
- Rolling Buffer HUD with animated dual-color ring
- Large KEEP LAST button
- Capture length selector buttons
- Live Input panel with moving meters and waveform
- Recovery Tools stack with functioning toggles
- Phrase Detected card
- Capture Preview waveform with trim handles
- Left transport column
- Right capture action column
- Export Destination button grid
- Recent Keeps horizontal browser
- Bottom macro knobs and output meter
- Footer status / tagline

## Visual style
- High-end futuristic audio software, not game UI and not mobile app UI
- Deep black / graphite background
- Hot red/orange capture energy for primary actions
- Cyan/blue for waveforms, metering and data
- Subtle gold/orange micro-accent lines for luxury detail
- Rich depth, bevels and neon glows without looking tacky

## Delivery checklist for milestone 1
- Plugin editor opens at 1491 x 1055
- Screenshot exported at 1491 x 1055
- Overlay comparison with approved mockup completed
- All panel positions aligned
- All core controls visually finished
- Placeholder animation running

## Files to use
- 00_REFERENCE/KEEP_THAT_Approved_UI_1491x1055.png
- 00_REFERENCE/KEEP_THAT_Annotated_Layout_v1.0.png
- 00_REFERENCE/KEEP_THAT_UI_Wireframe.png
- 08_LAYOUT/layout_1491x1055.json
- 08_LAYOUT/control_map.csv
- 09_JUCE_HANDOFF/KEEP_THAT_Layout.h
- 09_JUCE_HANDOFF/KEEP_THAT_DesignTokens.h
- All panel/control crop references under 01_BRAND and 03_COMPONENTS

## Technical notes
- Audio thread must stay real-time safe.
- Use a lock-free circular buffer for the eventual rolling capture engine.
- Use background workers for waveform thumbnails and file writing.
- Design milestone 1 so data can later connect cleanly to the real engine.

Build the complete polished UI first.
