/*
    Film.cpp - renders the KEEP THAT! demo film.

    Not a loop of the interface idling: a scripted piece that drives the REAL
    plug-in. Every capture in it is made by invoking the actual KEEP LAST
    handler, the capture engine runs on its own worker thread, and the frame
    loop drains it the moment it finishes - so the time the plug-in spends
    working on screen is the time it really takes. The trim handles, the
    rename, the recovery-tool toggles and the WAV write are all the same code
    paths a user drives.

    Encodes STRAIGHT to H.264 - there is no intermediate image sequence, and
    the writer carries VIDEO ONLY. Audio is added afterwards by tools/mux.m.
    That split is not tidiness: an AVAssetWriter with two inputs throttles one
    while the other is starved, so appending all the video and then all the
    audio wedges the video input in a sleep loop for ever. Interleaving them by
    hand was tried too and still stalled. One input never stalls.

    Encodes STRAIGHT to H.264 - there is no intermediate image sequence. The
    first version wrote 2790 JPEGs (635 MB) for a second tool to read back and
    decode, which cost a JPEG encode and a JPEG decode per frame plus the whole
    file set written and re-read. Painting directly into the encoder's pixel
    buffer removes all of it, and the audio is muxed in the same pass.

    usage:
      film <out.mp4> [fps=30] [audio=path.wav] [audiostart=12] [seconds=93] [size=WxH]
*/
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

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

void drawLower (juce::Graphics& g, int W, int H, const Lower& l, double now, bool reel)
{
    const double in = now - l.from;
    const double out = l.to - now;
    if (in < 0.0 || out < 0.0) return;

    const float a = juce::jmin (easeIn (in), easeIn (out, 0.35));
    if (a <= 0.01f) return;

    const float slide = (1.0f - easeIn (in)) * 34.0f;

    // A reel is read at arm's length on a phone, so the plate is wider, taller
    // and set much larger - the landscape sizes shrink to nothing there.
    const int x = reel ? 44 : 72;
    const int y = (reel ? H - 430 : H - 128) + (int) slide;
    const int w = reel ? W - 88 : juce::jmin (W - 144, 900);
    const int h = reel ? 210 : 96;

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

    auto text = r.reduced (reel ? 34.0f : 26.0f, reel ? 26.0f : 16.0f);
    g.setColour (tokens::textPrimary);
    g.setFont (Fonts::panelTitle().withHeight (reel ? 52.0f : 30.0f));
    g.drawText (l.title, text.removeFromTop (reel ? 62.0f : 36.0f),
                juce::Justification::centredLeft, false);
    g.setColour (tokens::textSecond);
    g.setFont (Fonts::small().withHeight (reel ? 30.0f : 18.0f));
    g.drawFittedText (l.sub, text.toNearestInt(), juce::Justification::topLeft, 3);
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

/** Wraps AVAssetWriter so a JUCE Image can be handed straight to H.264. */
struct MovieWriter
{
    AVAssetWriter* writer = nil;
    AVAssetWriterInput* video = nil;
    AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;
    int w = 0, h = 0, fps = 30, count = 0;

    bool open (const juce::String& path, int width, int height, int frameRate)
    {
        w = width & ~1; h = height & ~1; fps = frameRate;     // H.264 wants even
        NSString* p = [NSString stringWithUTF8String: path.toRawUTF8()];
        [[NSFileManager defaultManager] removeItemAtPath: p error: nil];

        NSError* err = nil;
        writer = [AVAssetWriter assetWriterWithURL: [NSURL fileURLWithPath: p]
                                          fileType: AVFileTypeMPEG4 error: &err];
        if (writer == nil) return false;

        video = [AVAssetWriterInput assetWriterInputWithMediaType: AVMediaTypeVideo
                 outputSettings: @{
                     AVVideoCodecKey  : AVVideoCodecTypeH264,
                     AVVideoWidthKey  : @(w),
                     AVVideoHeightKey : @(h),
                     AVVideoCompressionPropertiesKey : @{
                         AVVideoAverageBitRateKey      : @(12000000),
                         AVVideoMaxKeyFrameIntervalKey : @(frameRate * 2),
                         AVVideoProfileLevelKey        : AVVideoProfileLevelH264HighAutoLevel,
                         AVVideoAllowFrameReorderingKey: @NO } }];
        video.expectsMediaDataInRealTime = NO;

        adaptor = [AVAssetWriterInputPixelBufferAdaptor
                   assetWriterInputPixelBufferAdaptorWithAssetWriterInput: video
                   sourcePixelBufferAttributes: @{
                       (id) kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                       (id) kCVPixelBufferWidthKey  : @(w),
                       (id) kCVPixelBufferHeightKey : @(h) }];
        [writer addInput: video];
        return true;
    }

    bool start()
    {
        if (! [writer startWriting]) return false;
        [writer startSessionAtSourceTime: kCMTimeZero];
        return true;
    }

    /** JUCE's ARGB image is BGRA in memory on a little-endian machine, which
        is exactly the pixel format asked for above - so each row is a straight
        memcpy with no conversion. */
    bool append (const juce::Image& img)
    {
        CVPixelBufferRef pb = nullptr;
        if (CVPixelBufferPoolCreatePixelBuffer (nullptr, adaptor.pixelBufferPool, &pb) != kCVReturnSuccess)
            return false;

        CVPixelBufferLockBaseAddress (pb, 0);
        {
            juce::Image::BitmapData src (img, juce::Image::BitmapData::readOnly);
            auto* dst = (juce::uint8*) CVPixelBufferGetBaseAddress (pb);
            const size_t dstStride = CVPixelBufferGetBytesPerRow (pb);
            const size_t bytes = (size_t) juce::jmin (w * 4, src.lineStride);
            for (int y = 0; y < h && y < img.getHeight(); ++y)
                std::memcpy (dst + (size_t) y * dstStride, src.getLinePointer (y), bytes);
        }
        CVPixelBufferUnlockBaseAddress (pb, 0);

        while (! video.isReadyForMoreMediaData)
            [NSThread sleepForTimeInterval: 0.001];

        const bool ok = [adaptor appendPixelBuffer: pb
                              withPresentationTime: CMTimeMake (count, fps)];
        CVPixelBufferRelease (pb);
        ++count;
        return ok;
    }

    bool close()
    {
        [video markAsFinished];
        __block bool done = false;
        [writer finishWritingWithCompletionHandler: ^{ done = true; }];
        while (! done) [NSThread sleepForTimeInterval: 0.01];
        return writer.status == AVAssetWriterStatusCompleted;
    }
};

/** Which part of the interface a vertical cut should be looking at.

    A phone showing the whole 1491 px editor puts every label under six pixels
    tall - unreadable, and the point of a reel is that it reads at arm's length
    while scrolling. So each act zooms to the panel it is actually about, and
    only the opening and closing beats show the whole instrument. */
juce::Rectangle<int> focusFor (double t)
{
    struct Shot { double from, to; int x, y, w, h; };
    // Crops are kept near 2:1 or squarer. Anything much wider becomes a thin
    // strip in a 9:16 slot and defeats the point of zooming in at all.
    static const Shot shots[] = {
        {  0.0, 18.0,  430,  70,  660,  520 },   // HUD, KEEP LAST and lengths
        { 18.0, 27.0,  470, 380,  580,  200 },   // the capture-length buttons
        { 27.0, 45.0,    8, 735,  700,  170 },   // the keeps rack filling up
        { 45.0, 56.0,  120, 578,  660,  195 },   // the waveform, for trimming
        { 56.0, 65.0,    8, 735,  560,  170 },   // a keep card, for renaming
        { 65.0, 74.0, 1080, 105,  410,  480 },   // recovery tools
        { 74.0, 83.0, 1145, 575,  345,  325 },   // export destinations
        { 83.0, 999.0, 430,  70,  660,  520 },   // back to the HUD
    };
    for (const auto& s : shots)
        if (t >= s.from && t < s.to)
            return { s.x, s.y, s.w, s.h };
    return { 0, 0, 1491, 1055 };
}

// ------------------------------------------------------------------- main ---
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String outPath = argc > 1 ? argv[1] : "film.mp4";
    // 1920x1080 to match the other Diamond Loopz films. The editor is 1491x1055
    // (1.41:1), so it is scaled into the frame with a band underneath that the
    // lower thirds own - captions laid straight over a full-bleed interface
    // land on top of the controls.
    int fps = 30, W = 1920, H = 1080;
    double totalSeconds = 93.0, audioStart = 0.0;   // audiostart skips an intro
    double stillAt = -1.0;                          // still=SEC dumps one PNG
    bool reel = false;                              // shape=reel -> 1080x1920
    juce::File audioFile;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a.startsWith ("fps="))      fps = juce::jlimit (12, 60, a.fromFirstOccurrenceOf ("=", false, false).getIntValue());
        else if (a.startsWith ("seconds=")) totalSeconds = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a.startsWith ("audio="))   audioFile = juce::File (a.fromFirstOccurrenceOf ("=", false, false));
        else if (a.startsWith ("audiostart=")) audioStart = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a.startsWith ("still="))   stillAt = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a == "shape=reel")         reel = true;
        else if (a.startsWith ("size="))
        {
            auto s = a.fromFirstOccurrenceOf ("=", false, false);
            W = s.upToFirstOccurrenceOf ("x", false, false).getIntValue();
            H = s.fromFirstOccurrenceOf ("x", false, false).getIntValue();
        }
    }

    if (reel) { W = 1080; H = 1920; }

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

    const double openerEnd = 5.0, closerStart = totalSeconds - 6.5;

    MovieWriter movie;
    if (! movie.open (outPath, W, H, fps))
    {
        std::printf ("FAIL: could not open %s for writing\n", outPath.toRawUTF8());
        return 2;
    }
    if (! movie.start())
    {
        std::printf ("FAIL: could not start the writer\n");
        return 2;
    }

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

            // The editor is 1.41:1 and the film is 16:9, so there is always a
            // pillarbox. Rather than leave it dead, the interface itself is
            // blown up to COVER the frame and knocked right back - the side
            // bars carry the plug-in's own colour and the frame reads as one
            // image instead of a screenshot on a black card.
            g.setColour (juce::Colour (0xff05070b));
            g.fillAll();
            {
                const float cover = juce::jmax ((float) W / Design::width,
                                                (float) H / Design::height) * 1.06f;
                const float bw = Design::width * cover, bh = Design::height * cover;
                g.setOpacity (0.16f);
                g.drawImage (plugin, juce::Rectangle<float> ((W - bw) * 0.5f, (H - bh) * 0.5f, bw, bh),
                             juce::RectanglePlacement::stretchToFit);
                g.setOpacity (1.0f);
            }
            juce::ColourGradient halo (juce::Colour (0xff0b111a).withAlpha (0.55f), W * 0.5f, H * 0.36f,
                                       juce::Colour (0xff05070b).withAlpha (0.97f), W * 0.5f, H * 1.05f, true);
            g.setGradientFill (halo);
            g.fillAll();

            juce::Rectangle<int> src (0, 0, Design::width, Design::height);
            int dx, dy, dw, dh;

            if (reel)
            {
                // Every panel in this interface is wide, so one crop can never
                // fill a 9:16 frame - it just leaves a letterboxed strip in a
                // tall empty screen. Two stacked images do fill it, and they
                // do a better job anyway: the whole instrument on top so the
                // viewer knows what they are looking at, and the panel the act
                // is actually about blown up underneath so it reads on a phone.
                if (Assets::has ("logo.png"))
                {
                    const auto& logo = Assets::image ("logo.png");
                    const float lw = W * 0.62f;
                    const float lh = lw * (float) logo.getHeight() / (float) logo.getWidth();
                    g.setOpacity (1.0f);
                    g.drawImage (logo, juce::Rectangle<float> ((W - lw) * 0.5f, 74.0f, lw, lh),
                                 juce::RectanglePlacement::centred);
                }

                dw = W - 48;
                dh = (int) std::lround (dw * (double) Design::height / Design::width);
                dx = 24;
                dy = 268;

                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRect (dx - 2, dy - 2, dw + 4, dh + 4);
                g.setOpacity (1.0f);
                g.drawImage (plugin, dx, dy, dw, dh, 0, 0, Design::width, Design::height);

                // The feature showcase, entirely BELOW the interface.
                //
                // Nothing is drawn over the plug-in - it was previously
                // outlined to show where the detail came from, and that box
                // sat on top of the controls it was meant to be pointing at.
                // The showcase has to be readable without covering anything.
                auto det = focusFor (now);
                const double aspect = (double) det.getWidth() / det.getHeight();

                // Fit inside the slot BOTH ways. Capping the height alone left
                // the width untouched and squashed the picture - a 700x560
                // crop of the HUD was coming out at half its proper height.
                const int slotW = W - 48;
                const int slotH = H - (dy + dh + 46) - 470;   // above the caption
                int detW = slotW, detH = (int) std::lround (slotW / aspect);
                if (detH > slotH)
                {
                    detH = slotH;
                    detW = (int) std::lround (slotH * aspect);
                }
                const int detX = (W - detW) / 2;
                const int detY = dy + dh + 46 + (slotH - detH) / 2;

                g.setColour (juce::Colour (0xff0b1017));
                g.fillRect (detX - 3, detY - 3, detW + 6, detH + 6);
                g.setColour (tokens::accentCyan.withAlpha (0.45f));
                g.drawRect (detX - 3, detY - 3, detW + 6, detH + 6, 2);
                g.setOpacity (1.0f);
                g.drawImage (plugin, detX, detY, detW, detH,
                             det.getX(), det.getY(), det.getWidth(), det.getHeight());
            }
            else
            {
                const int band = 132, top = 20;
                const float sc = juce::jmin ((W - 120.0f) / Design::width,
                                             (H - band - top - 12.0f) / Design::height);
                dw = (int) (Design::width * sc); dh = (int) (Design::height * sc);
                dx = (W - dw) / 2; dy = top;
            }

            if (! reel)
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRect (dx - 2, dy - 2, dw + 4, dh + 4);

                // MUST reset the opacity before drawing the interface. In JUCE
                // the fill colour's alpha is what drawImage uses as its
                // opacity, so the 0.55 set for the shadow above was also being
                // applied to the plug-in - the film came out at half brightness.
                g.setOpacity (1.0f);
                g.drawImage (plugin, dx, dy, dw, dh,
                             src.getX(), src.getY(), src.getWidth(), src.getHeight());
            }

            for (const auto& l : lowers)
                drawLower (g, W, H, l, now, reel);

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

        // A single frame written out so the composition can be checked
        // without scrubbing the film.
        if (stillAt >= 0.0 && now >= stillAt && now < stillAt + 1.0 / fps)
        {
            auto png = juce::File (outPath).withFileExtension ("still.png");
            if (auto st = std::unique_ptr<juce::FileOutputStream> (png.createOutputStream()))
            {
                st->setPosition (0); st->truncate();
                juce::PNGImageFormat fmt;
                fmt.writeImageToStream (frame, *st);
                std::printf ("  still at %.1fs -> %s\n", now, png.getFullPathName().toRawUTF8());
            }
        }

        if (! movie.append (frame))
        {
            std::printf ("FAIL: encoder rejected frame %d\n", f);
            return 2;
        }

        if (f % 300 == 0)
            std::printf ("  %5d / %d  (%5.1fs)  keeps=%d\n", f, totalFrames, now,
                         (int) processor.session().keeps.size());
    }

    if (! movie.close())
    {
        std::printf ("FAIL: could not finish %s\n", outPath.toRawUTF8());
        return 2;
    }

    const auto sz = juce::File (outPath).getSize() / 1048576.0;
    std::printf ("wrote %s  (%d frames, %dx%d @ %d fps, %.1f s, %.1f MB%s)\n",
                 outPath.toRawUTF8(), totalFrames, W, H, fps, totalSeconds, sz,
                 ", silent - add audio with tools/mux");
    std::printf ("final keep count: %d\n", (int) processor.session().keeps.size());
    return 0;
}
