/*
    KeepThatEditor - the approved 1491 x 1055 interface.

    One fixed-size content component holds every panel in design coordinates;
    the editor applies a single uniform AffineTransform scale and centres the
    result. X and Y are never scaled independently, so the aspect ratio is
    locked at 1491:1055 and nothing can shear.
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/Paint.h"
#include "ui/Widgets.h"
#include "ui/AnimationClock.h"
#include "ui/HeaderComponent.h"
#include "ui/LiveInputPanel.h"
#include "ui/BufferHudPanel.h"
#include "ui/RecoveryToolsPanel.h"
#include "ui/CapturePreviewPanel.h"
#include "ui/RecentKeepsPanel.h"
#include "ui/BottomControlStrip.h"
#include "ui/ActionPanels.h"
#include "ui/OverlayPanels.h"
#include "state/PresetManager.h"

namespace keepthat
{

class KeepThatEditor : public juce::AudioProcessorEditor
{
public:
    explicit KeepThatEditor (KeepThatProcessor&);
    ~KeepThatEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Drives every display once, synchronously - used by `make uishot`. */
    void refreshDisplays();

private:
    class ContentComponent;
    class DebugOverlay;

    std::unique_ptr<ContentComponent> content;
    juce::TooltipWindow tooltips { this, 650 };
    float currentScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeepThatEditor)
};

} // namespace keepthat
