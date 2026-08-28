// -----------------------------------------------------------------------------
//  Renders the KEEP THAT! editor to a PNG without opening a window.
//
//    make uishot                             -> build/KeepThat-ui.png, 1491x1055
//    make uishot ARGS="out.png min"          -> 1044x739
//    make uishot ARGS="out.png max"          -> 2237x1583
//    make uishot ARGS="out.png 1200x849"     -> an explicit size
//    make uishot ARGS="out.png def signal"   -> feed test audio first, so the
//                                               meters, trail and ring run live
//    make uishot ARGS="out.png def fill=270" -> feed 270 s, so the rolling
//                                               buffer genuinely reaches the
//                                               approved 04:27 state
//    make uishot ARGS="out.png def demo"     -> populate the approved mockup's
//                                               recent keeps (a real instance
//                                               starts empty)
//    make uishot ARGS="out.png def settle=N" -> N animation frames before the
//                                               capture (phase control)
//
//  A deterministic frame count means two runs of the same command produce the
//  same pixels, which is what makes the overlay comparison against the approved
//  mockup meaningful.
// -----------------------------------------------------------------------------

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Assets.h"
#include <cstdio>

using namespace keepthat;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    headlessRefreshMode() = true;

    const juce::String outName = argc > 1 ? argv[1] : "KeepThat-ui.png";
    const juce::String sizeArg = argc > 2 ? juce::String (argv[2]).toLowerCase() : "def";

    bool feed = false, demo = false;
    juce::String overlay;
    int settleFrames = 90;
    double fillSeconds = 6.0;
    juce::File audioFile;        // audio=FILE -> shoot against real material
    double audioStart = 0.0;
    int frameCount = 0;          // frames=N -> render an animation, not a still
    int keepAtFrame = -1;        // keepat=F -> press KEEP LAST on frame F
    for (int i = 3; i < argc; ++i)
    {
        const juce::String a (juce::String (argv[i]).toLowerCase());
        if (a.startsWith ("settle="))
            settleFrames = juce::jlimit (1, 4000,
                              a.fromFirstOccurrenceOf ("=", false, false).getIntValue());
        else if (a.startsWith ("fill="))
        {
            fillSeconds = juce::jlimit (0.1, 600.0,
                              a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue());
            feed = true;
        }
        else if (a.contains ("demo"))
            demo = true;
        else if (a.contains ("settings"))
            overlay = "Settings";
        else if (a.contains ("help"))
            overlay = "Help";
        else if (a.startsWith ("frames="))
        {
            frameCount = juce::jlimit (1, 3000,
                             a.fromFirstOccurrenceOf ("=", false, false).getIntValue());
            feed = true;
        }
        else if (a.startsWith ("audio="))
        {
            audioFile = juce::File (juce::String (argv[i]).fromFirstOccurrenceOf ("=", false, false));
            feed = true;
        }
        else if (a.startsWith ("audiostart="))
            audioStart = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a.startsWith ("keepat="))
            keepAtFrame = a.fromFirstOccurrenceOf ("=", false, false).getIntValue();
        else if (a.contains ("signal"))
            feed = true;
    }

    int width = Design::width, height = Design::height;
    if (sizeArg == "min")       { width = Design::minWidth;  height = Design::minHeight; }
    else if (sizeArg == "max")  { width = Design::maxWidth;  height = Design::maxHeight; }
    else if (sizeArg.containsChar ('x'))
    {
        width  = sizeArg.upToFirstOccurrenceOf ("x", false, false).getIntValue();
        height = sizeArg.fromFirstOccurrenceOf ("x", false, false).getIntValue();
    }

    KeepThatProcessor processor;
    // In animation mode a UI frame is exactly 800 samples (48 kHz / 60 fps),
    // so one audio block per rendered frame keeps the meters, the buffer clock
    // and the sweep marker all telling the same time.
    const int blockSize = frameCount > 0 ? 800 : 512;
    processor.setPlayConfigDetails (2, 2, 48000.0, blockSize);
    processor.prepareToPlay (48000.0, blockSize);

    // A real instance starts with no captures, which is the honest state and
    // what a customer sees on first open. The reference and marketing shots
    // want the approved mockup's populated state, so they ask for it.
    if (demo)
    {
        PlaceholderData::populate (processor.session());
        processor.session().loadPreviewFrom (processor.session().keeps.front());
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
    {
        std::printf ("FAIL: createEditor returned null\n");
        return 2;
    }

    auto* ktEditor = dynamic_cast<KeepThatEditor*> (editor.get());
    if (ktEditor == nullptr)
    {
        std::printf ("FAIL: editor is not a KeepThatEditor\n");
        return 2;
    }
    editor->setSize (width, height);

    // A 124 BPM loop with kick transients and a bass line - enough musical
    // truth for the meters and the scrolling trail to look like real material.
    // A 124 BPM loop with kick transients and a bass line - enough musical
    // truth for the meters and the scrolling trail to look like real material.
    juce::Random random (0x4b54);
    double bassPhase = 0.0;
    float lp = 0.0f;
    int sampleIndex = 0;

    // Shooting against the real track means the KEY and BPM readouts in a
    // screenshot are the ones a customer would see, not a synthetic stand-in.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader;
    if (audioFile != juce::File())
    {
        reader.reset (formats.createReaderFor (audioFile));
        if (reader == nullptr)
        {
            std::printf ("FAIL: could not read %s\n", audioFile.getFullPathName().toRawUTF8());
            return 2;
        }
    }
    juce::int64 readPos = reader != nullptr
                            ? (juce::int64) (audioStart * reader->sampleRate) : 0;

    auto pushBlock = [&] (int numSamples)
    {
        juce::AudioBuffer<float> audio (2, numSamples);
        juce::MidiBuffer midi;

        if (reader != nullptr)
        {
            if (readPos + numSamples >= (juce::int64) reader->lengthInSamples)
                readPos = 0;
            reader->read (&audio, 0, numSamples, readPos, true, true);
            readPos += numSamples;
            sampleIndex += numSamples;
            processor.processBlock (audio, midi);
            return;
        }
        for (int i = 0; i < numSamples; ++i)
        {
            const double t = sampleIndex / 48000.0;
            const double beatLen = 60.0 / 124.0;
            const double beatPos = std::fmod (t, beatLen);
            const float kick = (float) std::exp (-beatPos * 8.0);

            bassPhase += 2.0 * juce::MathConstants<double>::pi
                           * (55.0 + 6.0 * kick) / 48000.0;
            float v = 0.42f * (0.4f + 0.6f * kick) * (float) std::sin (bassPhase);

            const float white = random.nextFloat() * 2.0f - 1.0f;
            lp += 0.1f * (white - lp);
            v += 0.06f * lp + 0.10f * kick * white;

            audio.setSample (0, i, v);
            audio.setSample (1, i, v * 0.94f);
            ++sampleIndex;
        }
        processor.processBlock (audio, midi);
    };

    if (feed)
        for (int block = 0, blocks = (int) (48000.0 * fillSeconds / blockSize);
             block < blocks; ++block)
            pushBlock (blockSize);

    // Open an overlay if one was asked for, by pressing its header button -
    // the same path a user takes, so the shot cannot show a state the
    // interface cannot actually reach.
    if (overlay.isNotEmpty())
    {
        std::function<void (juce::Component&)> press = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* b = dynamic_cast<juce::Button*> (child))
                    if (b->getName() == overlay && b->onClick != nullptr)
                        b->onClick();       // triggerClick posts asynchronously
                press (*child);
            }
        };
        press (*editor);
    }

    // Let the animated displays settle: meters ballistics, the ring's breathe
    // phase, the scroll easing.
    for (int i = 0; i < settleFrames; ++i)
        ktEditor->refreshDisplays();

    // ---- animation mode ----------------------------------------------------
    // Renders a numbered frame sequence instead of one still. Each frame is
    // one 800-sample block of audio plus one UI refresh, so the sequence plays
    // back at 60 fps as real time.
    if (frameCount > 0)
    {
        juce::Button* keepButton = nullptr;
        std::function<void (juce::Component&)> findKeep = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* b = dynamic_cast<juce::Button*> (child))
                    if (b->getName() == "KEEP LAST")
                        keepButton = b;
                findKeep (*child);
            }
        };
        findKeep (*editor);

        const auto base = outName.upToLastOccurrenceOf (".png", false, false);
        juce::PNGImageFormat png;

        for (int f = 0; f < frameCount; ++f)
        {
            pushBlock (blockSize);

            if (f == keepAtFrame && keepButton != nullptr && keepButton->onClick != nullptr)
                keepButton->onClick();      // triggerClick posts asynchronously

            // A capture finishes on a worker thread and is normally delivered
            // by the message loop, which a headless tool has not got. Draining
            // every frame hands it over as soon as it is ready, so the busy
            // state and the clip landing both appear in the sequence exactly
            // as long as they really take.
            processor.engine().drainCompletedForTesting();

            ktEditor->refreshDisplays();

            juce::Image frame (juce::Image::ARGB, width, height, true);
            {
                juce::Graphics g (frame);
                editor->paintEntireComponent (g, true);
            }

            auto file = juce::File::getCurrentWorkingDirectory()
                            .getChildFile (base + juce::String::formatted ("_%04d.png", f));
            if (auto st = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
            {
                st->setPosition (0);
                st->truncate();
                png.writeImageToStream (frame, *st);
            }
        }

        std::printf ("wrote %d frames  %s_0000..%04d.png  (%dx%d)\n",
                     frameCount, base.toRawUTF8(), frameCount - 1, width, height);
        return 0;
    }

    juce::Image image (juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }

    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (outName);
    if (auto stream = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, *stream))
        {
            std::printf ("FAIL: could not encode %s\n", outName.toRawUTF8());
            return 2;
        }
    }
    else
    {
        std::printf ("FAIL: could not open %s for writing\n", outName.toRawUTF8());
        return 2;
    }

    std::printf ("wrote %s (%dx%d)%s\n", out.getFullPathName().toRawUTF8(), width, height,
                 feed ? juce::String ("  [fed " + juce::String (fillSeconds, 1)
                                      + " s of signal]").toRawUTF8() : "");

    // A wrong-looking build is far more often a load problem than an art
    // problem, so missing artwork is named and made a non-zero exit rather
    // than quietly falling back to the procedural drawing.
    if (Assets::loadFailureCount() > 0)
    {
        std::printf ("\nWARNING: %d asset(s) failed to load:\n  %s\n",
                     Assets::loadFailureCount(), Assets::describeFailures().toRawUTF8());
        return 1;
    }

    std::printf ("all artwork loaded from %s\n",
                 Assets::assetsDirectory().getFullPathName().toRawUTF8());
    return 0;
}
