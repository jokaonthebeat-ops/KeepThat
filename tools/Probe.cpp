/*
    Probe.cpp - runs the analysis engines over a real audio file and prints
    what they decide.

    Written because the demo film exposed two detection errors against a track
    whose true key and tempo are known from its filename, and re-rendering a
    93-second film to check a one-line change is not a workable loop. This
    reads a file, runs KeyDetector and TempoDetector over it, and prints the
    answer in about a second.

    usage: probe <file.wav> [start=SEC] [length=SEC] [expect=150] [expectkey=A#m]
*/
#include <JuceHeader.h>
#include "../Source/capture/KeyDetector.h"
#include "../Source/capture/TempoDetector.h"

using namespace keepthat;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: probe <file> [start=] [length=] [expect=] [expectkey=]\n"); return 2; }

    juce::File f { juce::String (argv[1]) };   // braces: parens here declare a function
    double start = 0.0, length = 30.0, expectBpm = 0.0;
    juce::String expectKey;
    for (int i = 2; i < argc; ++i)
    {
        juce::String a (argv[i]);
        if (a.startsWith ("start="))       start  = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a.startsWith ("length=")) length = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a.startsWith ("expect=")) expectBpm = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a.startsWith ("expectkey=")) expectKey = a.fromFirstOccurrenceOf ("=", false, false);
    }

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { std::printf ("FAIL: cannot read %s\n", f.getFullPathName().toRawUTF8()); return 2; }

    const double rate = r->sampleRate;
    const juce::int64 from = (juce::int64) (start * rate);
    const int want = (int) juce::jmin ((double) (r->lengthInSamples - from), length * rate);
    if (want <= 0) { std::printf ("FAIL: nothing to read\n"); return 2; }

    juce::AudioBuffer<float> buf (2, want);
    r->read (&buf, 0, want, from, true, true);

    std::printf ("%s\n  %.1f s from %.1f s, %.0f Hz\n",
                 f.getFileName().toRawUTF8(), want / rate, start, rate);

    if (juce::SystemStats::getEnvironmentVariable ("PROBE_DEBUG", "") .isNotEmpty())
        KeyDetector::debugDump (buf, rate);

    const auto k = KeyDetector::detect (buf, rate);
    if (juce::SystemStats::getEnvironmentVariable ("PROBE_DEBUG", "").isNotEmpty())
        TempoDetector::debugDump (buf, rate);

    const auto t = TempoDetector::detect (buf, rate);

    std::printf ("  KEY   %-10s  confidence %.2f  %s\n",
                 k.describe().toRawUTF8(), k.confidence,
                 expectKey.isEmpty() ? ""
                   : (k.describe().replace (" ", "").startsWithIgnoreCase (
                        expectKey.replace ("m", "Min").replace (" ", "")) ? "OK" : "<-- MISMATCH"));
    std::printf ("  TEMPO %-10.1f  confidence %.2f  %s\n",
                 t.bpm, t.confidence,
                 expectBpm <= 0.0 ? ""
                   : (std::abs (t.bpm - expectBpm) < 2.0 ? "OK"
                      : juce::String ("<-- MISMATCH, expected "
                                      + juce::String (expectBpm, 0)).toRawUTF8()));
    return 0;
}
