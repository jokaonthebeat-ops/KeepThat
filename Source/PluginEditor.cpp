#include "PluginEditor.h"
#include "Assets.h"
#include "export/WavExporter.h"

namespace keepthat
{

// -----------------------------------------------------------------------------
//  DebugOverlay - development aid: a 100 px grid, component bounds and the
//  current scale. Hidden; toggled with Cmd/Ctrl+Shift+D.
// -----------------------------------------------------------------------------
class KeepThatEditor::DebugOverlay : public juce::Component
{
public:
    DebugOverlay()
    {
        setInterceptsMouseClicks (false, false);
        setVisible (false);
    }

    float scaleToShow = 1.0f;

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::magenta.withAlpha (0.10f));
        for (int x = 0; x < Design::width; x += 100)
            g.fillRect (x, 0, 1, Design::height);
        for (int y = 0; y < Design::height; y += 100)
            g.fillRect (0, y, Design::width, 1);

        if (auto* parent = getParentComponent())
            for (auto* sibling : parent->getChildren())
                if (sibling != this && sibling->isVisible())
                    drawBounds (g, *sibling, sibling->getBounds(), 0);

        g.setColour (juce::Colours::magenta);
        g.setFont (Fonts::make (13.0f, false, true));
        g.drawText ("DEBUG  scale " + juce::String (scaleToShow, 3) + "   base 1491x1055",
                    12, Design::height - 26, 400, 18, juce::Justification::centredLeft);
    }

private:
    void drawBounds (juce::Graphics& g, juce::Component& c, juce::Rectangle<int> r, int depth)
    {
        g.setColour (juce::Colours::magenta.withAlpha (depth == 0 ? 0.75f : 0.30f));
        g.drawRect (r, 1);

        if (depth == 0)
        {
            g.setFont (Fonts::make (9.0f));
            g.drawText (c.getName().isNotEmpty() ? c.getName() : c.getTitle(),
                        r.getX() + 3, r.getY() + 1, 220, 11, juce::Justification::centredLeft);
        }

        for (auto* child : c.getChildren())
            if (child->isVisible())
                drawBounds (g, *child, child->getBounds().translated (r.getX(), r.getY()),
                            depth + 1);
    }
};

// -----------------------------------------------------------------------------
//  ContentComponent - the fixed 1491 x 1055 design canvas.
// -----------------------------------------------------------------------------
class KeepThatEditor::ContentComponent : public juce::Component
{
public:
    explicit ContentComponent (KeepThatProcessor& p)
        : processor (p), header (p), liveInput (p), hud (p), recovery (p),
          preview (p), transport (p), exportPanel (p), keeps (p), bottom (p)
    {
        setOpaque (true);
        componentArtwork() = Assets::has (art::logo);

        addAndMakeVisible (header);
        addAndMakeVisible (liveInput);
        addAndMakeVisible (hud);
        addAndMakeVisible (recovery);
        addAndMakeVisible (transport);
        addAndMakeVisible (preview);
        addAndMakeVisible (actions);
        addAndMakeVisible (exportPanel);
        addAndMakeVisible (keeps);
        addAndMakeVisible (bottom);
        addAndMakeVisible (footer);
        addChildComponent (settingsOverlay);
        addChildComponent (helpOverlay);
        addChildComponent (debugOverlay);

        header.setName ("Header");
        liveInput.setName ("Live input");
        hud.setName ("Buffer HUD");
        recovery.setName ("Recovery tools");
        preview.setName ("Capture preview");
        keeps.setName ("Recent keeps");
        bottom.setName ("Bottom controls");

        hud.onKeepLast = [this] { keepLastPressed(); };
        transport.onTransport = [this] { preview.repaint(); };
        transport.onPlay = [this] { playSelected(); };
        preview.onSeek = [this] (float at) { seekTo (at); };
        transport.onStop = [this] { stopPlayback(); };
        actions.onRename = [this] { renameSelected(); };
        actions.onSaveWav = [this] { saveSelected(); };
        actions.onDragToDaw = [this] (juce::Component* c) { dragSelectedToDaw (c); };
        actions.onDragHint  = [this]
        { footer.flashMessage ("Hold DRAG TO DAW and pull it onto a track"); };
        keeps.onSelect = [this] (int index) { loadKeep (index); };
        keeps.onDragOut = [this] (int index, juce::Component* c)
        {
            // Dragging a card acts on THAT card, not on whatever happened to
            // be selected before the press.
            processor.session().selectedKeep = index;
            loadKeep (index);
            dragSelectedToDaw (c);
        };
        keeps.onChanged = [this] { keeps.repaint(); };
        header.onArmedChanged = [this] { hud.repaint(); liveInput.repaint(); };
        header.onHistoryChanged = [this] (juce::String what)
        {
            footer.flashMessage (what);
            keeps.repaint();
            preview.repaint();
            // An undo can change which clip is selected, so the preview has to
            // follow it rather than keep showing the one that went away.
            loadKeep (processor.session().selectedKeep);
        };
        exportPanel.onFolderChanged = [this] (juce::String folder)
        { footer.flashMessage ("Destination set to " + folder); };

        header.onSave     = [this] { savePreset(); };
        header.onSettings = [this] { settingsOverlay.show(); };
        header.onHelp     = [this] { helpOverlay.show(); };
        header.onPresetStep = [this] (int delta)
        {
            if (presets.step (delta))
            {
                footer.flashMessage ("Preset: " + processor.session().presetName);
                syncFromSession();
            }
            else
            {
                footer.flashMessage ("No presets saved yet");
            }
        };

        settingsOverlay.onPerformanceChanged = [this] { applyPerformanceMode(); };
        settingsOverlay.onBufferCleared = [this]
        {
            processor.session().lastMessage = "Buffer cleared - counting from 0:00";
        };
        keeps.onPlay = [this] (int) { playSelected(); };

        processor.onClipLoaded = [this] (juce::int64)
        {
            preview.repaint();
            keeps.repaint();
            if (processor.session().lastMessage.isNotEmpty())
                footer.flashMessage (processor.session().lastMessage);
        };

        // A capture landing has to refresh the panels that show it.
        processor.onCaptureApplied = [this]
        {
            keeps.repaint();
            preview.repaint();
            footer.flashMessage (processor.session().lastMessage);
        };

        clock.onFrame = [this] (double dt) { frame (dt); };
        applyPerformanceMode();

        setSize (Design::width, Design::height);
    }

    ~ContentComponent() override { clock.stop(); }

    void applyPerformanceMode()
    {
        // ANIMATION_SPEC: 60 fps preferred, 30 fps low-power fallback, plus
        // reduce-motion. See the note on lowPowerMode in CaptureModel.h.
        const auto& s = processor.session();
        clock.stop();
        clock.start (s.lowPowerMode ? 30 : 60);
        hud.setReduceMotion (s.reduceMotion);
    }

    /** Pushes session-level choices back onto the controls after a preset
        load, which changes them behind the widgets' backs. */
    void syncFromSession()
    {
        hud.syncSelection();
        exportPanel.refresh();
        preview.repaint();
        header.repaint();
        keeps.repaint();
    }

    void savePreset()
    {
        auto name = processor.session().presetName.trim();
        if (name.isEmpty())
            name = "Preset " + juce::String (presets.names().size() + 1);

        if (presets.save (name) != juce::File())
            footer.flashMessage ("Saved preset: " + name);
        else
            footer.flashMessage ("Could not save the preset");
        header.repaint();
    }

    void resized() override
    {
        header.setBounds (layout::header);
        liveInput.setBounds (layout::liveInput);
        hud.setBounds (layout::bufferHud);
        recovery.setBounds (layout::recoveryTools);
        transport.setBounds (layout::transport);
        preview.setBounds (layout::capturePreview);
        actions.setBounds (layout::actionStack);
        exportPanel.setBounds (layout::exportDest);
        keeps.setBounds (layout::recentKeeps);
        bottom.setBounds (layout::bottomControls);
        footer.setBounds (layout::footer);
        settingsOverlay.setBounds (getLocalBounds());
        helpOverlay.setBounds (getLocalBounds());
        debugOverlay.setBounds (getLocalBounds());
    }

    void paint (juce::Graphics& g) override
    {
        paintShell (g);
    }

    void frame (double dt)
    {
        // Message-thread housekeeping: frees buffers the audio thread handed
        // back, refreshes the buffer clock, and keeps phrase detection current.
        processor.tickMessageThread();

        // The playhead comes from the preview player, which is the only thing
        // that knows where playback actually is.
        auto& s = processor.session();
        const float head = processor.preview().playheadPosition();
        if (! juce::approximatelyEqual (head, s.playhead))
        {
            s.playhead = head;
            preview.repaint();
        }

        liveInput.update (dt);
        hud.update (dt);
        recovery.update();
        preview.update (dt);
        keeps.update();
        bottom.update (dt);

        if (++headerDivider >= 12)      // slow-changing text, ~5 Hz is plenty
        {
            headerDivider = 0;
            header.refresh();
        }
    }

    void toggleDebug (float scale)
    {
        debugOverlay.scaleToShow = scale;
        debugOverlay.setVisible (! debugOverlay.isVisible());
        debugOverlay.repaint();
    }

private:
    /** The chassis behind every panel.

        Drawn, not blitted. 02_CHASSIS is a different arrangement from the
        approved reference (see the note on componentArtwork() in Theme.h), so
        the ground and the panel plates are painted at the approved rects and
        only the component art is used on top.
    */
    void paintShell (juce::Graphics& g)
    {
        // Cached whole. The panels are not opaque, so JUCE repaints this
        // behind every one of them - sixty times a second that was three
        // full-canvas gradient fills plus a 620 px radial halo per panel, and
        // it was most of the CPU the plugin used sitting idle.
        if (shell.isNull())
        {
            shell = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), false);
            juce::Graphics sg (shell);
            auto r = getLocalBounds().toFloat();

            sg.setGradientFill (juce::ColourGradient (juce::Colour (0xff0c1119), r.getCentreX(),
                                                      r.getY(), tokens::bg0, r.getCentreX(),
                                                      r.getBottom(), false));
            sg.fillAll();

            paint::halo (sg, { layout::ringArtCentreX, layout::ringArtCentreY }, 620.0f,
                         juce::Colour (0xff1b2430), 0.55f);

            juce::ColourGradient vignette (juce::Colours::transparentBlack, r.getCentreX(),
                                           r.getCentreY(), juce::Colours::black.withAlpha (0.55f),
                                           r.getX(), r.getBottom(), true);
            sg.setGradientFill (vignette);
            sg.fillAll();

            sg.setColour (juce::Colours::white.withAlpha (0.012f));
            for (int i = -getHeight(); i < getWidth(); i += 3)
                sg.drawLine ((float) i, 0.0f, (float) (i + getHeight()), (float) getHeight(), 1.0f);
        }
        g.drawImageAt (shell, 0, 0);
    }

    void keepLastPressed()
    {
        // This is the whole product in one call: pull the selected length out
        // of the rolling buffer, on a worker, and hand back a real clip.
        const auto& s = processor.session();
        const auto& table = captureLengths();
        const int index = juce::jlimit (0, (int) table.size() - 1,
                                        s.selectedLength >= 4 ? s.selectedSeconds
                                                              : s.selectedLength);
        if (! processor.captureLast (table[(size_t) index]))
            footer.flashMessage (processor.session().lastMessage);
    }

    void loadKeep (int index)
    {
        auto& s = processor.session();
        if (index < 0 || index >= (int) s.keeps.size())
            return;

        auto& clip = s.keeps[(size_t) index];

        // A clip restored from a previous session is metadata only. Reading
        // its WAV happens in the background: doing it here would stall the
        // interface for as long as the disk takes, which on a cold external
        // drive is long enough to look broken.
        if (! processor.ensureAudio (clip, true))
        {
            s.previewLoading = true;
            s.previewLo.clear();
            s.previewHi.clear();
            preview.repaint();

            // Selecting one card usually means a neighbour is next, and
            // reading those while the user listens costs nothing.
            for (int offset : { 1, -1, 2 })
            {
                const int at = index + offset;
                if (at >= 0 && at < (int) s.keeps.size())
                    processor.ensureAudio (s.keeps[(size_t) at], false);
            }
            return;
        }

        s.previewLoading = false;
        s.loadPreviewFrom (clip);
        preview.repaint();
        keeps.repaint();
    }

    /** The currently selected clip, or nullptr. */
    CaptureClip* selectedClip()
    {
        auto& s = processor.session();
        if (s.selectedKeep < 0 || s.selectedKeep >= (int) s.keeps.size())
            return nullptr;
        return &s.keeps[(size_t) s.selectedKeep];
    }

    void playSelected()
    {
        auto* clip = selectedClip();
        if (clip == nullptr)
        {
            footer.flashMessage ("Nothing to play yet");
            return;
        }
        if (! processor.ensureAudio (*clip, true))
        {
            footer.flashMessage ("Reading " + clip->name + "...");
            return;
        }
        const auto& s = processor.session();
        processor.preview().play (clip->audio, clip->sampleRate, s.trimLeft, s.trimRight);
    }

    void stopPlayback() { processor.preview().stop(); }

    void seekTo (float normalised)
    {
        auto* clip = selectedClip();
        if (clip == nullptr || ! clip->hasAudio())
            return;
        const auto& s = processor.session();
        processor.preview().play (clip->audio, clip->sampleRate,
                                  juce::jlimit (s.trimLeft, s.trimRight, normalised),
                                  s.trimRight);
    }

    void saveSelected()
    {
        auto* clip = selectedClip();
        if (clip == nullptr)
        {
            footer.flashMessage ("Nothing to save yet");
            return;
        }
        if (! processor.ensureAudio (*clip, true))
        {
            footer.flashMessage ("Reading " + clip->name + "...");
            return;
        }

        auto& session = processor.session();
        auto dir = WavExporter::directoryFor (session.destination, session);
        if (dir == juce::File())                       // DAW DRAG is not a folder
            dir = WavExporter::directoryFor (Destination::folder, session);

        auto target = dir.getChildFile (WavExporter::safeFileName (clip->name) + ".wav")
                         .getNonexistentSibling();

        if (WavExporter::writeWav (*clip, target))
        {
            clip->file = target;

            // PLAYLIST maintains a real .m3u, so the button does what it says.
            if (session.destination == Destination::playlist && session.writePlaylistFile)
                WavExporter::addToPlaylist (dir, target);

            footer.flashMessage ("Saved to " + target.getParentDirectory().getFileName());
        }
        else
        {
            footer.flashMessage ("Could not write " + target.getFullPathName());
        }
    }

    void dragSelectedToDaw (juce::Component* source)
    {
        auto* clip = selectedClip();
        if (clip == nullptr)
        {
            footer.flashMessage ("Nothing to drag yet");
            return;
        }
        if (! processor.ensureAudio (*clip, true))
        {
            footer.flashMessage ("Reading " + clip->name + "...");
            return;
        }
        if (! processor.settings().dragExportEnabled)
        {
            footer.flashMessage ("Drag Export is switched off");
            return;
        }
        if (! WavExporter::beginDrag (*clip, source))
            footer.flashMessage ("Could not prepare the drag file");
    }

    void renameSelected()
    {
        auto* clip = selectedClip();
        if (clip == nullptr)
            return;

        // Inline rename: the card's own label becomes editable rather than
        // opening a modal, which a plugin should avoid.
        keeps.beginRename (processor.session().selectedKeep);
    }

    KeepThatProcessor& processor;

    HeaderComponent header;
    LiveInputPanel liveInput;
    BufferHudPanel hud;
    RecoveryToolsPanel recovery;
    CapturePreviewPanel preview;
    TransportColumn transport;
    CaptureActionColumn actions;
    ExportDestinationPanel exportPanel;
    RecentKeepsPanel keeps;
    BottomControlStrip bottom;
    FooterBar footer;
    DebugOverlay debugOverlay;

    PresetManager presets { processor.parameters(), processor.session() };
    SettingsOverlay settingsOverlay { processor };
    HelpOverlay helpOverlay;

    AnimationClock clock;
    juce::Image shell;
    int headerDivider = 0;
};

// -----------------------------------------------------------------------------
//  Editor
// -----------------------------------------------------------------------------
KeepThatEditor::KeepThatEditor (KeepThatProcessor& p)
    : AudioProcessorEditor (&p)
{
    content = std::make_unique<ContentComponent> (p);
    addAndMakeVisible (*content);

    setResizable (true, true);
    if (auto* constrainer = getConstrainer())
    {
        constrainer->setFixedAspectRatio ((double) Design::aspect);
        constrainer->setSizeLimits (Design::minWidth, Design::minHeight,
                                    Design::maxWidth, Design::maxHeight);
    }
    setSize (Design::width, Design::height);
}

KeepThatEditor::~KeepThatEditor() = default;

void KeepThatEditor::resized()
{
    // One uniform scale, centred - never independent X and Y.
    currentScale = juce::jmin (getWidth()  / (float) Design::width,
                               getHeight() / (float) Design::height);

    content->setTransform (juce::AffineTransform::scale (currentScale));
    content->setBounds (0, 0, Design::width, Design::height);

    const int shownW = juce::roundToInt (Design::width  * currentScale);
    const int shownH = juce::roundToInt (Design::height * currentScale);
    content->setTopLeftPosition (juce::roundToInt ((getWidth()  - shownW) * 0.5f / currentScale),
                                 juce::roundToInt ((getHeight() - shownH) * 0.5f / currentScale));
}

void KeepThatEditor::paint (juce::Graphics& g)
{
    // Only ever visible in the letterbox strips when the host forces a size
    // the aspect ratio cannot fill exactly.
    g.fillAll (tokens::bg0);
}

bool KeepThatEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'D')
    {
        content->toggleDebug (currentScale);
        return true;
    }
    return false;
}

void KeepThatEditor::refreshDisplays()
{
    content->frame (1.0 / 60.0);
}

} // namespace keepthat
