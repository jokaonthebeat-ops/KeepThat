/*
    Film.cpp - renders the KEEP THAT! demo film.

    Not a loop of the interface idling: a scripted piece that drives the REAL
    plug-in. Every capture in it is made by invoking the actual KEEP LAST
    handler, the capture engine runs on its own worker thread, and the frame
    loop drains it the moment it finishes - so the time the plug-in spends
    working on screen is the time it really takes. The trim handles, the
    rename, the recovery-tool toggles and the WAV write are all the same code
    paths a user drives.

    Output is a numbered JPEG sequence, encoded to H.264 by tools/mkvideo.m.
    JPEG rather than PNG because a 93-second film at 30 fps is 2800 frames and
    PNG masters would be several gigabytes.

    usage:
      film <out-prefix> [fps=30] [audio=path.wav] [seconds=93] [size=WxH]
*/
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Assets.h"
#include "../Source/ui/Theme.h"

using namespace keepthat;

namespace
{
constexpr double kRate = 48000.0;

// ---------------------------------------------------------------- material --
/** Generates the audio the film is "played into". Parameters change between
    acts so the eight captures in the rack do not all share one waveform - a
    rack of eight identical thumbnails would look like a mock-up. */
struct Material
{
    double phase = 0.0, phase2 = 0.0;
    int    index = 0;
    juce::Random rng { 0x4b546831 };

    double root = 55.0;        // bass root, Hz
    double bpm = 124.0;
    float  density = 1.0f;     // how busy the top end is
    float  drive = 1.0f;       // overall level

    void nextVariation()
    {
        static const double roots[] = { 55.0, 61.74, 49.0, 65.41, 58.27, 73.42, 46.25, 69.3 };
        root = roots[(size_t) (index++ % 8)];
        density = 0.6f + rng.nextFloat() * 0.9f;
        drive = 0.8f + rng.nextFloat() * 0.35f;
    }

    void render (juce::AudioBuffer<float>& buf, int n, juce::int64 startSample)
    {
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (startSample + i) / kRate;
            const double beat = 60.0 / bpm;
            const double inBeat = std::fmod (t, beat);
            const double inBar = std::fmod (t, beat * 4.0);

            // kick on the beat, snare on 2 and 4
            const float kick = (float) std::exp (-inBeat * 9.0);
            const float snare = (inBar > beat * 0.98 && inBar < beat * 1.35)
                             || (inBar > beat * 2.98 && inBar < beat * 3.35)
                                 ? (float) std::exp (-std::fmod (inBar, beat) * 26.0) : 0.0f;

            phase += 2.0 * juce::MathConstants<double>::pi * (root + 5.0 * kick) / kRate;
            phase2 += 2.0 * juce::MathConstants<double>::pi * (root * 3.0) / kRate;

            float v = 0.40f * (0.35f + 0.65f * kick) * (float) std::sin (phase);
            v += 0.10f * density * (float) std::sin (phase2)
                       * (float) std::exp (-inBeat * 3.0);

            const float noise = rng.nextFloat() * 2.0f - 1.0f;
            v += 0.16f * snare * noise;
            v += 0.02f * density * noise;

            v *= drive;
            buf.setSample (0, i, v);
            buf.setSample (1, i, v * 0.95f);
        }
    }
};

// ----------------------------------------------------------------- overlay --
struct Lower
{
    double from = 0.0, to = 0.0;
    juce::String title, sub;
};

float easeIn (double t, double d = 0.45)
{
    const double x = juce::jlimit (0.0, 1.0, t / d);
    return (float) (1.0 - std::pow (1.0 - x, 3.0));
}

void drawLower (juce::Graphics& g, int W, int H, const Lower& l, double now)
{
    const double in = now - l.from;
    const double out = l.to - now;
    if (in < 0.0 || out < 0.0) return;

    const float a = juce::jmin (easeIn (in), easeIn (out, 0.35));
    if (a <= 0.01f) return;

    const float slide = (1.0f - easeIn (in)) * 34.0f;
    const int x = 72, y = H - 128 + (int) slide;      // inside the caption band
    const int w = juce::jmin (W - 144, 900), h = 96;

    juce::Graphics::ScopedSaveState ss (g);
    g.setOpacity (a);

    // A dark plate lit on its left edge - the same language as the plug-in's
    // own panels, rather than a generic broadcast box.
    juce::Rectangle<float> r ((float) x, (float) y, (float) w, (float) h);
    g.setColour (juce::Colour (0xff0b1017).withAlpha (0.94f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (tokens::stroke);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (tokens::accentRed);
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getY() + 8.0f, 3.0f, r.getHeight() - 16.0f));

    auto text = r.reduced (26.0f, 16.0f);
    g.setColour (tokens::textPrimary);
    g.setFont (Fonts::panelTitle().withHeight (30.0f));
    g.drawText (l.title, text.removeFromTop (36.0f), juce::Justification::centredLeft, false);
    g.setColour (tokens::textSecond);
    g.setFont (Fonts::small().withHeight (18.0f));
    g.drawText (l.sub, text, juce::Justification::topLeft, false);
}

/** Opener and closer share a card so the film starts and ends on the same
    mark rather than two different treatments. */
void drawCard (juce::Graphics& g, int W, int H, float alpha,
               const juce::String& line, const juce::String& fine)
{
    if (alpha <= 0.005f) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.setOpacity (juce::jlimit (0.0f, 1.0f, alpha));

    g.setColour (juce::Colour (0xff05070b));
    g.fillAll();

    // the ring's red/cyan split, as a hairline behind the mark
    const float cy = H * 0.5f;
    juce::ColourGradient grad (tokens::accentRed.withAlpha (0.0f), 0.0f, cy,
                               tokens::accentCyan.withAlpha (0.0f), (float) W, cy, false);
    grad.addColour (0.30, tokens::accentRed.withAlpha (0.55f));
    grad.addColour (0.50, juce::Colours::white.withAlpha (0.75f));
    grad.addColour (0.70, tokens::accentCyan.withAlpha (0.55f));
    g.setGradientFill (grad);
    g.fillRect (0.0f, cy + 96.0f, (float) W, 1.0f);

    if (Assets::has ("logo.png"))
    {
        const auto& logo = Assets::image ("logo.png");
        const float lw = juce::jmin (740.0f, W * 0.52f);
        const float lh = lw * (float) logo.getHeight() / (float) logo.getWidth();
        g.drawImage (logo, juce::Rectangle<float> ((W - lw) * 0.5f, cy - lh - 26.0f, lw, lh),
                     juce::RectanglePlacement::centred);
    }

    int ty = (int) cy + 120;
    if (line.isNotEmpty())
    {
        // The logo already carries "ALWAYS-ON IDEA CAPTURE", so the opener does
        // not repeat it - only the closer sets this line.
        g.setColour (tokens::accentGold);
        g.setFont (Fonts::panelTitle().withHeight (30.0f).withExtraKerningFactor (0.34f));
        g.drawText (line, juce::Rectangle<int> (0, ty, W, 44), juce::Justification::centred, false);
        ty += 52;
    }

    if (fine.isNotEmpty())
    {
        g.setColour (line.isEmpty() ? tokens::textSecond : tokens::textMuted);
        g.setFont (Fonts::small().withHeight (line.isEmpty() ? 24.0f : 20.0f));
        g.drawFittedText (fine, juce::Rectangle<int> (W / 2 - 520, ty, 1040, 70),
                          juce::Justification::centredTop, 2);
    }
}
} // namespace

// ------------------------------------------------------------------- main ---
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String outPrefix = argc > 1 ? argv[1] : "film";
    // 1920x1080 to match the other Diamond Loopz films. The editor is 1491x1055
    // (1.41:1), so it is scaled into the frame with a band underneath that the
    // lower thirds own - captions laid straight over a full-bleed interface
    // land on top of the controls.
    int fps = 30, W = 1920, H = 1080;
    double totalSeconds = 93.0, audioStart = 0.0;   // audiostart skips an intro
    juce::File audioFile;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a.startsWith ("fps="))      fps = juce::jlimit (12, 60, a.fromFirstOccurrenceOf ("=", false, false).getIntValue());
        else if (a.startsWith ("seconds=")) totalSeconds = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a.startsWith ("audio="))   audioFile = juce::File (a.fromFirstOccurrenceOf ("=", false, false));
        else if (a.startsWith ("audiostart=")) audioStart = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a.startsWith ("size="))
        {
            auto s = a.fromFirstOccurrenceOf ("=", false, false);
            W = s.upToFirstOccurrenceOf ("x", false, false).getIntValue();
            H = s.fromFirstOccurrenceOf ("x", false, false).getIntValue();
        }
    }

    const int samplesPerFrame = (int) std::llround (kRate / fps);
    const int totalFrames = (int) std::llround (totalSeconds * fps);

    KeepThatProcessor processor;
    processor.setPlayConfigDetails (2, 2, kRate, samplesPerFrame);
    processor.prepareToPlay (kRate, samplesPerFrame);

    // Never write demo WAVs into the user's real captures folder.
    auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("KeepThatFilm");
    scratch.createDirectory();
    for (auto& d : processor.session().destinationFolder)
        d = scratch;

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* ktEditor = dynamic_cast<KeepThatEditor*> (editor.get());
    if (ktEditor == nullptr) { std::printf ("FAIL: no editor\n"); return 2; }
    // The EDITOR is always its design size; W/H are the film canvas it gets
    // composited into. Setting the editor to the canvas size stretched its
    // layout and cropped the right-hand panels.
    editor->setSize (Design::width, Design::height);

    // ---- helpers to drive the real controls -------------------------------
    auto findButton = [&editor] (const juce::String& name) -> juce::Button*
    {
        juce::Button* found = nullptr;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* b = dynamic_cast<juce::Button*> (child))
                    if (found == nullptr && b->getName() == name)
                        found = b;
                walk (*child);
            }
        };
        walk (*editor);
        return found;
    };
    auto click = [&findButton] (const juce::String& name)
    {
        if (auto* b = findButton (name))
            if (b->onClick != nullptr)
                b->onClick();              // triggerClick posts asynchronously
    };
    auto findEditorField = [&editor]() -> juce::TextEditor*
    {
        juce::TextEditor* found = nullptr;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* t = dynamic_cast<juce::TextEditor*> (child))
                    if (found == nullptr) found = t;
                walk (*child);
            }
        };
        walk (*editor);
        return found;
    };

    Material mat;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (audioFile != juce::File())
    {
        if (! audioFile.existsAsFile())
        {
            std::printf ("FAIL: audio file not found: %s\n",
                         audioFile.getFullPathName().toRawUTF8());
            return 2;
        }
        reader.reset (formats.createReaderFor (audioFile));
        if (reader == nullptr)
        {
            // Falling back to the synthesised material silently would produce
            // a film that looks fine and is not the material that was asked
            // for. Better to stop.
            std::printf ("FAIL: could not decode %s (MP3 needs JUCE_USE_MP3AUDIOFORMAT;"
                         " convert to WAV with afconvert)\n",
                         audioFile.getFileName().toRawUTF8());
            return 2;
        }
        std::printf ("audio: %s  (%.1f s, %d ch, %.0f Hz)\n",
                     audioFile.getFileName().toRawUTF8(),
                     reader->lengthInSamples / reader->sampleRate,
                     (int) reader->numChannels, reader->sampleRate);
    }

    juce::int64 samplePos = 0;
    juce::int64 readPos = reader != nullptr
                            ? (juce::int64) (audioStart * reader->sampleRate) : 0;
    auto pushAudio = [&] ()
    {
        juce::AudioBuffer<float> buf (2, samplesPerFrame);
        if (reader != nullptr)
        {
            if (readPos + samplesPerFrame >= (juce::int64) reader->lengthInSamples)
                readPos = 0;                        // loop the supplied material
            reader->read (&buf, 0, samplesPerFrame, readPos, true, true);
            readPos += samplesPerFrame;
        }
        else
        {
            mat.render (buf, samplesPerFrame, samplePos);
        }
        samplePos += samplesPerFrame;
        juce::MidiBuffer midi;
        processor.processBlock (buf, midi);
    };

    // ---- the script --------------------------------------------------------
    struct Beat { double at; std::function<void()> go; bool done = false; };
    std::vector<Beat> beats;
    std::vector<Lower> lowers;
    auto& S = processor.session();

    auto beat = [&beats] (double at, std::function<void()> go) { beats.push_back ({ at, std::move (go) }); };
    auto lower = [&lowers] (double from, double to, juce::String t, juce::String s)
                 { lowers.push_back ({ from, to, std::move (t), std::move (s) }); };

    // ACT 1 - the plug-in, empty and already listening
    lower (5.0, 10.0, "ALWAYS-ON IDEA CAPTURE",
           "Nothing to arm. Nothing to press. It is already listening.");

    // ACT 2 - the first capture
    lower (11.0, 17.5, "KEEP LAST", "Recovers what you just played - after you played it.");
    beat (13.0, [&] { click ("4 BARS"); });
    beat (14.2, [&] { click ("KEEP LAST"); });

    // ACT 3 - choose the unit
    lower (18.5, 26.0, "CHOOSE YOUR UNIT", "Bars follow the host tempo. Or seconds. Or the last phrase.");
    { const char* n[] = { "1 BAR", "2 BARS", "8 BARS", "15 SEC", "30 SEC", "60 SEC", "PHRASE", "4 BARS" };
      for (int i = 0; i < 8; ++i) beat (19.0 + i * 0.85, [&click, n, i] { click (n[i]); }); }

    // ACT 4 - fill the rack. The buffer restart is turned off for this stretch
    // so eight captures can be made back to back; it is turned on again in
    // ACT 8, which is where it gets explained.
    beat (26.5, [&] { S.restartBufferAfterKeep = false; });
    lower (27.0, 44.0, "A HUNDRED KEEPS", "Each one with its length, its key and its own waveform.");
    for (int i = 0; i < 7; ++i)
        beat (28.0 + i * 2.4, [&mat, &click] { mat.nextVariation(); click ("KEEP LAST"); });

    // ACT 5 - trim
    lower (45.0, 55.0, "TRIM IT DOWN", "Drag the handles across the capture, then commit the span.");
    beat (45.5, [&] { S.selectedKeep = 0; S.trimLeft = 0.0f; S.trimRight = 1.0f; });
    for (int i = 0; i <= 30; ++i)
        beat (46.5 + i * 0.08, [&S, i] { S.trimLeft  = 0.0f + 0.18f * (i / 30.0f); });
    for (int i = 0; i <= 30; ++i)
        beat (49.2 + i * 0.08, [&S, i] { S.trimRight = 1.0f - 0.24f * (i / 30.0f); });
    beat (52.2, [&] { click ("TRIM"); });

    // ACT 6 - rename, typed in place
    lower (56.0, 64.0, "RENAME", "In place, on the card - and undoable, like everything else.");
    beat (56.5, [&] { click ("RENAME"); });
    { const juce::String target = "Late Night Hook";
      for (int i = 1; i <= target.length(); ++i)
          beat (57.4 + i * 0.075, [&findEditorField, target, i]
                { if (auto* te = findEditorField()) te->setText (target.substring (0, i), false); }); }
    beat (59.6, [&] { if (auto* te = findEditorField()) if (te->onReturnKey) te->onReturnKey(); });

    // ACT 7 - the recovery tools
    lower (65.0, 73.0, "RECOVERY TOOLS", "Trim, fades and normalise - applied as the capture is made.");
    beat (65.5, [&] { click ("NORMALIZE"); });
    beat (67.0, [&] { click ("AUTO TRIM"); });
    beat (68.5, [&] { click ("AUTO TRIM"); });
    beat (70.0, [&] { click ("SILENCE DETECT"); });
    beat (71.2, [&] { click ("SILENCE DETECT"); });

    // ACT 8 - out of the plug-in, and the restarting clock
    lower (74.0, 82.0, "OUT OF THE PLUG-IN", "A 24-bit WAV, or drag the capture straight into your DAW.");
    beat (74.5, [&] { click ("SAVE WAV"); });
    beat (77.0, [&] { S.lastMessage = "Drag to DAW - drop it straight on a track"; });
    beat (79.5, [&] { S.restartBufferAfterKeep = true; mat.nextVariation(); click ("KEEP LAST"); });
    lower (83.0, 88.0, "THE CLOCK RESTARTS", "So the buffer reads time since your last idea, not since you opened it.");

    std::sort (beats.begin(), beats.end(), [] (const Beat& a, const Beat& b) { return a.at < b.at; });

    // ---- settle, then render ----------------------------------------------
    for (int i = 0; i < 40; ++i) { pushAudio(); ktEditor->refreshDisplays(); }

    juce::JPEGImageFormat jpeg;
    jpeg.setQuality (0.92f);
    const double openerEnd = 5.0, closerStart = totalSeconds - 6.5;

    for (int f = 0; f < totalFrames; ++f)
    {
        const double now = (double) f / fps;

        pushAudio();
        for (auto& b : beats)
            if (! b.done && now >= b.at) { b.go(); b.done = true; }

        processor.engine().drainCompletedForTesting();
        ktEditor->refreshDisplays();

        juce::Image plugin (juce::Image::ARGB, Design::width, Design::height, true);
        {
            juce::Graphics pg (plugin);
            editor->paintEntireComponent (pg, true);
        }

        juce::Image frame (juce::Image::ARGB, W, H, true);
        {
            juce::Graphics g (frame);

            // Ground, lifted slightly behind the interface so the pillarbox
            // does not read as dead space.
            g.setColour (juce::Colour (0xff05070b));
            g.fillAll();
            juce::ColourGradient halo (juce::Colour (0xff141b25), W * 0.5f, H * 0.34f,
                                       juce::Colour (0xff05070b), W * 0.5f, H * 1.0f, true);
            g.setGradientFill (halo);
            g.fillAll();

            const int band = 132, top = 20;
            const float sc = juce::jmin ((W - 120.0f) / Design::width,
                                         (H - band - top - 12.0f) / Design::height);
            const int dw = (int) (Design::width * sc), dh = (int) (Design::height * sc);
            const int dx = (W - dw) / 2, dy = top;

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRect (dx - 2, dy - 2, dw + 4, dh + 4);
            g.drawImage (plugin, juce::Rectangle<float> ((float) dx, (float) dy,
                                                         (float) dw, (float) dh),
                         juce::RectanglePlacement::stretchToFit);

            for (const auto& l : lowers)
                drawLower (g, W, H, l, now);

            if (now < openerEnd)
            {
                const float a = now < openerEnd - 0.9 ? 1.0f
                              : 1.0f - (float) ((now - (openerEnd - 0.9)) / 0.9);
                drawCard (g, W, H, a, "ALWAYS-ON IDEA CAPTURE",
                          "The best take is usually the one nobody was recording.");
            }
            if (now > closerStart)
            {
                const float a = juce::jlimit (0.0f, 1.0f, (float) ((now - closerStart) / 1.1));
                drawCard (g, W, H, a, "NEVER LOSE THE MOMENT",
                          "VST3  .  AU  .  Standalone  .  macOS universal + Windows      Free");
            }
        }

        auto file = juce::File::getCurrentWorkingDirectory()
                        .getChildFile (outPrefix + juce::String::formatted ("_%05d.jpg", f));
        if (auto st = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
        {
            st->setPosition (0); st->truncate();
            jpeg.writeImageToStream (frame, *st);
        }

        if (f % 150 == 0)
            std::printf ("  %5d / %d  (%5.1fs)  keeps=%d\n", f, totalFrames, now,
                         (int) processor.session().keeps.size());
    }

    std::printf ("wrote %d frames  %s_00000..%05d.jpg  (%dx%d @ %d fps, %.1f s)\n",
                 totalFrames, outPrefix.toRawUTF8(), totalFrames - 1, W, H, fps, totalSeconds);
    std::printf ("final keep count: %d\n", (int) processor.session().keeps.size());
    return 0;
}
