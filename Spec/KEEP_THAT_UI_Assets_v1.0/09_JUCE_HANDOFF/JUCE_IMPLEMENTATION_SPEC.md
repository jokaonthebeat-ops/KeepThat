# KEEP THAT! JUCE Implementation Specification

## Product Purpose
KEEP THAT! is an always-on idea-capture plugin. It continuously stores recent incoming audio into a rolling buffer, then lets the user recover the last 1 bar, 2 bars, 4 bars, 8 bars, 15 sec, 30 sec, 60 sec, or an automatically detected phrase.

## Critical Rule
Do **not** flatten the approved screenshot into the plugin as the final UI. Recreate the look using live JUCE components and custom painting. The screenshot and cropped references are for visual matching only.

## Core UI Modules

### 1. Header
- Premium KEEP THAT! logo
- Subtitle: "Always-On Idea Capture"
- Preset navigation with previous and next arrows
- Save, Settings, Help, Undo, Redo, Power icon buttons

### 2. Live Input Panel
- Source selector
- Stereo input meters
- Mini input waveform display
- Readouts: BPM, Key, Peak, RMS, Mode, Input Status
- "Always Listening" armed indicator

### 3. Rolling Buffer HUD
- Large circular dual-color ring (red/orange on left, cyan on right)
- Tick marks and time labels around the ring
- Center readout: Buffer Active, Available Time, Max Time
- Animated ring segments showing elapsed/available history
- Subtle particle glows and breathing light motion

### 4. Keep Last Action Center
- Large glowing **KEEP LAST** button
- Two rows of capture size selectors: 1 BAR / 2 BARS / 4 BARS / 8 BARS and 15 SEC / 30 SEC / 60 SEC / PHRASE
- Selected states must glow red/orange

### 5. Recovery Tools Panel
- Toggle rows: Auto Trim, Silence Detect, Zero Crossing, Fade In/Out, Normalize, Drag Export
- Fade In/Out needs compact knob/dial controls or paired rotary controls
- Phrase Detected card with waveform preview, suggested bars, confidence meter, start and end readout

### 6. Capture Preview Timeline
- Large waveform preview panel
- Trim handles left/right
- Playhead line
- Mode switch for time or bars:beats
- Current capture range label
- Smooth zoom-safe waveform rendering

### 7. Capture Action Columns
- Left: Play, Stop, Trim
- Right: Rename, Save WAV, Drag to DAW

### 8. Export / Destination
- Grid buttons: DAW Drag, Sampler, Playlist, Folder, Desktop
- Selected destination button should receive a bright red outline and internal glow

### 9. Recent Keeps Browser
- Horizontal clip card strip
- Each card shows name, mini waveform, duration, play button, favorite icon and delete icon
- First selected card highlighted in red
- Must support scrolling when item count exceeds visible area

### 10. Bottom Macro Controls
- Premium rotary controls: Buffer Length, Sensitivity, Auto Trim, Preview Mix, Fade, Output
- Output section includes L/R horizontal meters and Mute
- Footer tagline: "NEVER LOSE THE MOMENT"

## Motion / Animation Requirements
- 60 FPS preferred, 30 FPS fallback low-power mode
- HUD ring pulses subtly when armed
- Input waveform scrolls smoothly
- Meters respond smoothly with peak hold
- Selected buttons and toggles have subtle glow animation
- Recent keep tiles can animate selection changes

## DSP / Functional Architecture (post-UI milestone)
- Lock-free circular audio buffer on the audio thread
- Configurable max buffer length: 1 to 8 minutes
- Host tempo and playhead sync when available
- Time- or bar-based recovery extraction
- Phrase detection using amplitude + silence-gap heuristics
- Zero crossing trim and fade-in/out processing off the audio thread
- Background waveform thumbnail creation
- WAV export and drag-out file creation from a worker thread
- Recent keeps browser stored in local session state

## Styling
- Background: graphite/black brushed tech surface
- Main accents: red/orange capture glow, cyan waveform/data glow, gold micro-accents
- Avoid generic flat UI; use subtle 3D depth and polished bevels
- Typography should feel premium and technical, not playful or toy-like
