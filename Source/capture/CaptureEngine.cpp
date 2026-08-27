#include "CaptureEngine.h"
#include "ClipProcessor.h"

namespace keepthat
{

CaptureEngine::CaptureEngine() : juce::Thread ("KEEP THAT! capture")
{
    startThread (juce::Thread::Priority::normal);
}

CaptureEngine::~CaptureEngine()
{
    signalThreadShouldExit();
    work.signal();
    stopThread (2000);
    cancelPendingUpdate();
}

void CaptureEngine::prepare (double sampleRate, int channels)
{
    preparedRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    preparedChannels = juce::jlimit (1, 2, channels);
}

bool CaptureEngine::snapshot (const RollingBuffer& ring, double seconds,
                              juce::AudioBuffer<float>& dest)
{
    // readLast returns 0 when the writer lapped us mid-copy. Retry rather than
    // lock: at worst we lose a few hundred microseconds, and the audio thread
    // is never made to wait.
    for (int attempt = 0; attempt < 8; ++attempt)
        if (ring.readLast (seconds, dest) > 0)
            return true;
    return false;
}

bool CaptureEngine::requestCapture (const RollingBuffer& ring, CaptureLength length,
                                    const HostTiming& timing,
                                    const params::Settings& settings)
{
    if (captureBusy.exchange (true, std::memory_order_acq_rel))
        return false;                     // one capture in flight already

    auto job = std::make_unique<Job>();
    job->length = length;
    job->timing = timing;
    job->settings = settings;
    job->sampleRate = preparedRate;

    // How much audio to pull. PHRASE takes a generous window and lets the
    // detector decide where the phrase actually starts.
    double seconds = length.seconds (timing.valid && timing.bpm > 0.0 ? timing.bpm : 120.0);
    if (length.unit == LengthUnit::bars && timing.barSeconds() > 0.0)
        seconds = length.amount * timing.barSeconds();      // real time signature
    if (length.unit == LengthUnit::phrase)
        seconds = 20.0;

    seconds = juce::jlimit (0.05, ring.availableSeconds(), seconds);
    if (seconds < 0.05)
    {
        captureBusy.store (false, std::memory_order_release);
        const juce::ScopedLock sl (resultLock);
        failures.add ("Nothing in the buffer yet");
        triggerAsyncUpdate();
        return false;
    }

    // The snapshot is taken HERE, on the press, not when the worker gets to
    // it. So "the last four bars" always means the four bars before the
    // button went down, even if a phrase scan is finishing first.
    if (! snapshot (ring, seconds, job->audio))
    {
        captureBusy.store (false, std::memory_order_release);
        const juce::ScopedLock sl (resultLock);
        failures.add ("Buffer was being written - try again");
        triggerAsyncUpdate();
        return false;
    }

    {
        const juce::ScopedLock sl (jobLock);
        // Displaces a pending scan; a scan already running finishes first and
        // this runs straight after.
        pending = std::move (job);
    }
    work.signal();
    return true;
}

bool CaptureEngine::requestPhraseScan (const RollingBuffer& ring, double seconds,
                                       const HostTiming& timing,
                                       const params::Settings& settings)
{
    if (captureBusy.load (std::memory_order_relaxed))
        return false;                     // never compete with a real capture
    if (scanBusy.exchange (true, std::memory_order_acq_rel))
        return false;

    auto job = std::make_unique<Job>();
    job->phraseScanOnly = true;
    job->timing = timing;
    job->settings = settings;
    job->sampleRate = preparedRate;

    seconds = juce::jlimit (0.05, ring.availableSeconds(), seconds);
    if (seconds < 0.5 || ! snapshot (ring, seconds, job->audio))
    {
        scanBusy.store (false, std::memory_order_release);
        return false;
    }

    {
        const juce::ScopedLock sl (jobLock);
        if (pending != nullptr && ! pending->phraseScanOnly)
        {
            // A capture arrived while we were preparing - it wins.
            scanBusy.store (false, std::memory_order_release);
            return false;
        }
        pending = std::move (job);
    }
    work.signal();
    return true;
}

void CaptureEngine::run()
{
    while (! threadShouldExit())
    {
        work.wait (250);
        if (threadShouldExit())
            break;

        std::unique_ptr<Job> job;
        {
            const juce::ScopedLock sl (jobLock);
            job = std::move (pending);
        }
        if (job == nullptr)
            continue;

        const bool wasScan = job->phraseScanOnly;

        if (wasScan)
        {
            auto verdict = scanPhrase (*job);
            // Key detection rides along with the scan: it wants the same
            // recent audio, and running it on its own timer would double the
            // work for no benefit.
            // Key and tempo move far more slowly than the phrase does, and
            // each costs a few hundred FFTs, so they run on every fourth scan
            // rather than on all of them.
            const bool deepScan = (++scanCounter % 4) == 1;

            KeyResult key;
            if (deepScan)
                key = KeyDetector::detect (job->audio, job->sampleRate);

            // Only worth the work when the host has not told us. In a DAW the
            // host's tempo is authoritative and an estimate would just be a
            // second opinion nobody asked for.
            TempoResult tempo;
            if (deepScan && ! job->timing.valid)
                tempo = TempoDetector::detect (job->audio, job->sampleRate);

            const juce::ScopedLock sl (resultLock);
            readyPhrases.push_back (std::move (verdict));
            if (deepScan && key.detected)
                readyKeys.push_back (key);
            if (tempo.detected)
                readyTempos.push_back (tempo);
        }
        else
        {
            auto clip = process (*job);
            const juce::ScopedLock sl (resultLock);
            if (clip.seconds > 0.0)
                readyClips.push_back (std::move (clip));
            else
                failures.add ("Capture came back empty");
        }

        (wasScan ? scanBusy : captureBusy).store (false, std::memory_order_release);
        triggerAsyncUpdate();
    }
}

PhraseVerdict CaptureEngine::scanPhrase (Job& job)
{
    PhraseDetector::Options options;
    options.sensitivity = job.settings.sensitivity;

    const auto found = PhraseDetector::detect (job.audio, job.sampleRate,
                                               job.timing.valid ? job.timing.bpm : 0.0,
                                               options);
    PhraseVerdict verdict;
    verdict.detected = found.detected;
    verdict.confidence = found.confidence;
    verdict.suggestedBars = found.suggestedBars;

    if (found.detected)
    {
        // Stamps are relative to now, so they read as negative offsets - the
        // same convention the capture preview uses.
        const double total = job.audio.getNumSamples() / job.sampleRate;
        verdict.startSeconds = -(total - found.startSample / job.sampleRate);
        verdict.endSeconds   = -(total - found.endSample   / job.sampleRate);

        juce::AudioBuffer<float> phrase;
        clip::extractRange (job.audio, found.startSample, found.endSample, phrase);
        clip::buildThumbnail (phrase, 150, verdict.thumbLo, verdict.thumbHi);
    }
    return verdict;
}

CaptureClip CaptureEngine::process (Job& job)
{
    CaptureClip result;
    if (job.audio.getNumSamples() == 0)
        return result;

    const double rate = job.sampleRate;
    auto& settings = job.settings;

    int start = 0, end = job.audio.getNumSamples();

    // --- phrase mode decides its own bounds -------------------------------
    if (job.length.unit == LengthUnit::phrase)
    {
        PhraseDetector::Options options;
        options.sensitivity = settings.sensitivity;
        const auto found = PhraseDetector::detect (job.audio, rate,
                                                   job.timing.valid ? job.timing.bpm : 0.0,
                                                   options);
        if (found.detected && found.length() > 0)
        {
            start = found.startSample;
            end = found.endSample;
            result.detectedBars = found.suggestedBars;
            result.confidence = found.confidence;
        }
        else
        {
            // No phrase found: fall back to the last four seconds rather than
            // handing back twenty seconds of mostly nothing.
            start = juce::jmax (0, end - (int) std::round (rate * 4.0));
        }
    }

    // --- silence trim -----------------------------------------------------
    juce::AudioBuffer<float> working;
    clip::extractRange (job.audio, start, end, working);

    if (settings.autoTrimEnabled && working.getNumSamples() > 0)
    {
        // The gate follows the clip's own peak, so a quiet take is trimmed on
        // its own terms rather than against an absolute level.
        const float peak = clip::peakMagnitude (working);
        const float threshold = juce::jmax (1.0e-4f, peak * 0.06f);

        auto trim = clip::trimSilence (working, threshold, settings.autoTrimAmount, rate);
        if (settings.zeroCrossingEnabled)
            clip::snapToZeroCrossings (working, trim, rate);

        if (trim.length() > 8)
        {
            juce::AudioBuffer<float> trimmed;
            clip::extractRange (working, trim.start, trim.end, trimmed);
            working = std::move (trimmed);
            result.trimmedToZeroCrossing = trim.movedForZeroCrossing;
        }
    }

    if (working.getNumSamples() < 8)
        return result;                    // nothing usable survived

    // --- fades and normalize ---------------------------------------------
    if (settings.fadeEnabled)
        clip::applyFades (working, settings.fadeMilliseconds, rate);

    if (settings.normalizeEnabled)
        clip::normalise (working, settings.normalizeTargetDb);

    // --- finish -----------------------------------------------------------
    result.audio = std::make_shared<juce::AudioBuffer<float>> (std::move (working));
    result.sampleRate = rate;
    result.seconds = result.audio->getNumSamples() / rate;
    result.peakDb = juce::Decibels::gainToDecibels (clip::peakMagnitude (*result.audio), -100.0f);
    result.name = "Keep " + juce::String (++captureCounter);
    result.key = KeyDetector::detect (*result.audio, rate).describe();
    result.isFromLiveBuffer = true;
    clip::buildThumbnail (*result.audio, 128, result.thumbLo, result.thumbHi);

    if (job.length.unit != LengthUnit::phrase && job.timing.barSeconds() > 0.0)
    {
        result.detectedBars = result.seconds / job.timing.barSeconds();
    }
    else if (result.detectedBars <= 0.0 && ! job.timing.valid)
    {
        // No host tempo: estimate one from the capture itself so the clip
        // still carries a bar count rather than nothing.
        const auto tempo = TempoDetector::detect (*result.audio, rate);
        if (tempo.detected)
            result.detectedBars = result.seconds / (4.0 * 60.0 / tempo.bpm);
    }

    return result;
}

void CaptureEngine::handleAsyncUpdate()
{
    std::vector<CaptureClip> clips;
    std::vector<PhraseVerdict> phrases;
    std::vector<KeyResult> keys;
    std::vector<TempoResult> tempos;
    juce::StringArray errors;
    {
        const juce::ScopedLock sl (resultLock);
        clips.swap (readyClips);
        phrases.swap (readyPhrases);
        keys.swap (readyKeys);
        tempos.swap (readyTempos);
        errors.swapWith (failures);
    }

    for (auto& c : clips)
        if (onCaptureReady) onCaptureReady (std::move (c));

    for (auto& p : phrases)
        if (onPhraseReady) onPhraseReady (std::move (p));

    for (auto& k : keys)
        if (onKeyReady) onKeyReady (k);

    for (auto& t : tempos)
        if (onTempoReady) onTempoReady (t);

    for (const auto& e : errors)
        if (onCaptureFailed) onCaptureFailed (e);
}

} // namespace keepthat
