# Claude Master Build Prompt — KEEP THAT! v1.3 PRO

Build the complete **KEEP THAT!** JUCE plugin using the supplied v1.3 Pro assets. This is a real free utility, not a demo. It continuously stores recent audio in a rolling buffer and lets the user recover the last bars, seconds, or detected phrase.

## Visual authority
- `14_REFERENCE/KEEP_THAT_Approved_UI_1491x1055.png`
- `14_REFERENCE/KEEP_THAT_Annotated_Layout_v1.0.png`
- `12_LAYOUT/layout_1491x1055.json`
- `12_LAYOUT/control_map.csv`

## Production asset rule
Use the actual assets in folders `01_BRAND` through `11_ICONS`. Do not use the approved screenshot as a flattened plugin background. The `02_CHASSIS` file is the reusable static shell; all values, waveforms, meters, labels, timers, toggles and cards must remain live JUCE components.

## First milestone
Deliver the exact animated UI at 1491 x 1055 with realistic placeholder data. The milestone must include:
- premium header logo and utility controls
- animated layered rolling-buffer HUD
- KEEP LAST normal/hover/pressed states
- capture selector states
- moving input waveforms and meters
- recovery rows and toggles
- editable capture timeline with trim handles and playhead
- transport and export controls
- scrolling Recent Keeps cards
- 128-frame bottom knobs
- horizontal output meters
- responsive resizing with locked aspect ratio
- 60 FPS normal mode, 30 FPS low-power mode, Reduce Motion option

## Second milestone
Connect the real engine:
- lock-free rolling audio buffer
- host BPM and PPQ synchronization
- bar and second recovery modes
- phrase detection
- trim-to-zero-crossing
- fade and normalize processing off the audio thread
- WAV export and OS drag-and-drop
- recent keeps session storage

## Engineering rules
- Never allocate, lock, write files or create thumbnails on the audio thread.
- Use background jobs for export, waveform generation and analysis.
- Use APVTS for user-facing parameters.
- Persist non-parameter state in a versioned ValueTree.
- Provide AU, VST3 and Standalone targets for macOS; VST3 and Standalone for Windows.

## Quality gate
Export a screenshot at exactly 1491 x 1055 and compare it at 50% opacity over the approved UI. Correct layout, scale, spacing, control size and visual hierarchy before the DSP milestone is approved.
