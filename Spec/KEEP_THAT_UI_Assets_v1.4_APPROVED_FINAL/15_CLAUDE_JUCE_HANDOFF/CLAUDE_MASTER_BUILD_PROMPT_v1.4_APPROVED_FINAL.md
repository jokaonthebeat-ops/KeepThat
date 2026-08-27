# Claude Master Build Prompt — KEEP THAT! v1.4 APPROVED FINAL

Build the KEEP THAT! JUCE plugin using this complete assets package.

## Visual authority
The exact approved layout is:
- `00_APPROVED_REFERENCE/KEEP_THAT_Approved_UI_Latest_Logo_1491x1055.png`
- `00_APPROVED_REFERENCE/KEEP_THAT_Annotated_Layout_1491x1055.png`

The only approved logo is:
- `01_BRAND/SOURCE_OF_TRUTH/KEEP_THAT_Latest_Approved_Logo_Source.png`
- plus its derived exports under `01_BRAND`.

Do not use, recreate, or restore any former KEEP THAT! logo.

## Asset priority
1. Use individual production PNG files under `13_LATEST_APPROVED_SLICED_ASSETS`.
2. Use layered HUD and filmstrip assets already included under `03_HUD`, `04_KNOBS`, `07_METERS`, and `08_VISUALIZERS`.
3. Use full source sheets under `12_LATEST_APPROVED_ASSET_SHEETS` only when another crop or state must be derived.
4. Use exact panel and coordinate resources under `10_PANELS`, `14_LAYOUT`, and the handoff documentation.

## Required UI milestone
- Editor opens at exactly 1491 x 1055.
- Locked 1491:1055 resizing.
- Exact panel placement from the approved UI.
- Latest approved logo rendered at the upper left.
- Animated rolling-buffer HUD with live text and timer values.
- Functional KEEP LAST button with normal, hover, pressed, and disabled states.
- Functional 1 BAR, 2 BARS, 4 BARS, 8 BARS, 15 SEC, 30 SEC, 60 SEC, and PHRASE selectors.
- Play, Stop, and Trim controls.
- Recovery-tool toggles.
- Capture timeline with live waveform, playhead, and trim handles.
- Recent Keeps cards and action buttons.
- Premium knob filmstrips and segmented meters.
- No default JUCE visual widgets.

## Functional architecture
Keep the audio thread real-time safe. Use a lock-free rolling circular buffer. Perform waveform thumbnail generation, trim processing, WAV writing, and drag-file creation away from the audio thread.

## Critical rule
Do not flatten the approved screenshot into the plugin. Build the UI from the supplied separated assets and live JUCE components.
