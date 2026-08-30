#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "export/WavExporter.h"

namespace keepthat
{

KeepThatProcessor::KeepThatProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KEEPTHAT", params::createLayout())
{
    // Temp WAVs from a previous run that ended badly. Doing this at startup is
    // the spec's "delete abandoned temporary files on startup".
    WavExporter::sweepTempFiles (24);

    captureEngine.onCaptureReady  = [this] (CaptureClip c) { applyCapture (std::move (c)); };
    captureEngine.onPhraseReady   = [this] (PhraseVerdict v) { state.phrase = std::move (v); };
    captureEngine.onKeyReady      = [this] (KeyResult k) { state.key = k.describe(); };
    captureEngine.onTempoReady    = [this] (TempoResult t)
    {
        // Only ever fills in for a host that gave us nothing - the engine
        // does not even run the estimate otherwise.
        if (! hostBpmValid.load (std::memory_order_relaxed))
            state.bpm = t.bpm;
    };

    clipLoader.onLoaded = [this] (juce::int64 id, ClipLoader::LoadedClip loaded)
    {
        auto* clip = state.findById (id);
        if (clip == nullptr)
            return;                       // deleted while the disk was busy

        clip->audio = std::move (loaded.audio);
        clip->sampleRate = loaded.sampleRate;
        clip->seconds = clip->audio->getNumSamples() / juce::jmax (1.0, loaded.sampleRate);

        // Fill in whatever this clip is missing - a keep restored from an old
        // session arrived as bare metadata. Only MISSING fields: a key that
        // was detected at capture time is not second-guessed by a re-read.
        if (clip->thumbLo.empty())
        {
            clip->thumbLo = std::move (loaded.thumbLo);
            clip->thumbHi = std::move (loaded.thumbHi);
        }
        if (clip->peakDb <= -99.0f)
            clip->peakDb = loaded.peakDb;
        if (clip->key == "--" && loaded.key.detected)
            clip->key = loaded.key.describe();

        // Only refresh the preview if this is still the clip on screen.
        if (state.selectedKeep >= 0 && state.selectedKeep < (int) state.keeps.size()
            && state.keeps[(size_t) state.selectedKeep].id == id)
        {
            state.previewLoading = false;
            state.loadPreviewFrom (*clip);
        }
        if (onClipLoaded)
            onClipLoaded (id);
    };

    clipLoader.onFailed = [this] (juce::int64 id, juce::String reason)
    {
        state.previewLoading = false;
        state.lastMessage = reason;
        if (onClipLoaded)
            onClipLoaded (id);
    };
    captureEngine.onCaptureFailed = [this] (juce::String why) { state.lastMessage = why; };
}

KeepThatProcessor::~KeepThatProcessor()
{
    // Anything still in the temp directory belonged to this session and was
    // never saved anywhere the user asked for.
    WavExporter::sweepTempFiles (0);
}

void KeepThatProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    preparedRate = sampleRate;

    // Sized to the maximum the UI offers, not the current knob position:
    // shrinking on a knob move would throw away history the user is about to
    // ask for. ROLLING_BUFFER_ENGINE_SPEC calls for exactly this preallocation.
    ring.prepare (sampleRate, getTotalNumInputChannels(), 8.0 * 60.0);
    state.bufferMax = ring.maxSeconds();

    captureEngine.prepare (sampleRate, getTotalNumInputChannels());
    previewPlayer.prepare (sampleRate);

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (settings().outputGainDb, -60.0f));

    sawAudio.store (false, std::memory_order_relaxed);
}

bool KeepThatProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

void KeepThatProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        audio.clear (ch, 0, audio.getNumSamples());

    if (audio.getNumSamples() == 0)
        return;

    // Capture FIRST, so what the user recovers is exactly what arrived - before
    // the preview mix or the output gain touches it.
    ring.write (audio);
    meters.push (audio);
    liveTrail.push (audio);

    if (! sawAudio.load (std::memory_order_relaxed))
        sawAudio.store (true, std::memory_order_relaxed);

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
            {
                hostBpm.store (*bpm, std::memory_order_relaxed);
                hostBpmValid.store (true, std::memory_order_relaxed);
            }
            if (auto ppq = pos->getPpqPosition())
                hostPpq.store (*ppq, std::memory_order_relaxed);
            if (auto sig = pos->getTimeSignature())
            {
                hostTimeSigNum.store (sig->numerator, std::memory_order_relaxed);
                hostTimeSigDen.store (sig->denominator, std::memory_order_relaxed);
            }
        }
    }

    // Preview auditions on top of the pass-through, at PREVIEW MIX.
    const auto s = settings();
    previewPlayer.process (audio, s.previewMix);

    // Output gain and mute. Smoothed, so a knob move does not step the signal.
    outputGain.setTargetValue (s.mute ? 0.0f
                                      : juce::Decibels::decibelsToGain (s.outputGainDb, -60.0f));
    if (outputGain.isSmoothing())
    {
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            const float g = outputGain.getNextValue();
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                audio.getWritePointer (ch)[i] *= g;
        }
    }
    else
    {
        audio.applyGain (outputGain.getCurrentValue());
    }

    outMeters.push (audio);
}

HostTiming KeepThatProcessor::hostTiming() const noexcept
{
    HostTiming t;
    t.valid = hostBpmValid.load (std::memory_order_relaxed);
    t.bpm = hostBpm.load (std::memory_order_relaxed);
    t.ppqPosition = hostPpq.load (std::memory_order_relaxed);
    t.timeSigNumerator = hostTimeSigNum.load (std::memory_order_relaxed);
    t.timeSigDenominator = hostTimeSigDen.load (std::memory_order_relaxed);

    // With no host tempo, bar-based recovery still has to mean something, so
    // it falls back to the session's own BPM (the spec's "fall back to seconds
    // when host timing is unavailable" - here, to a stated tempo).
    if (! t.valid || t.bpm <= 0.0)
        t.bpm = state.bpm > 0.0 ? state.bpm : 120.0;

    return t;
}

bool KeepThatProcessor::captureLast (CaptureLength length)
{
    if (ring.availableSeconds() < 0.05)
    {
        state.lastMessage = "Nothing in the buffer yet";
        return false;
    }
    return captureEngine.requestCapture (ring, length, hostTiming(), settings());
}

void KeepThatProcessor::scanForPhrase()
{
    // 24 seconds, not 10. Phrase detection is happy with a short window - it
    // is looking for the last idea - but the same job also feeds KEY and
    // TEMPO on its deep pass, and ten seconds is simply not enough onset
    // history to fix a tempo. Measured against a 150 BPM track: a 10 s window
    // reported ~100 BPM every time (the two-thirds level), 15 s got it right
    // half the time, and 24 s got it right consistently. The window was the
    // whole difference - the detector was fine.
    //
    // It costs nothing extra in practice: the deep pass runs on every fourth
    // scan, so this is a longer FFT sweep every twelve seconds on a worker
    // thread, and the audio thread never sees it.
    captureEngine.requestPhraseScan (ring, 24.0, hostTiming(), settings());
}

bool KeepThatProcessor::perform (juce::UndoableAction* action, const juce::String& name)
{
    undoManager.beginNewTransaction (name);
    return undoManager.perform (action, name);
}

bool KeepThatProcessor::ensureAudio (CaptureClip& clip, bool urgent)
{
    if (clip.hasAudio())
        return true;

    if (clip.file.existsAsFile())
    {
        if (clip.id == 0)
            clip.id = state.nextClipId++;
        clipLoader.request (clip.id, clip.file, urgent);
    }
    return false;
}

void KeepThatProcessor::applyCapture (CaptureClip clip)
{
    clip.createdAtMs = juce::Time::currentTimeMillis();
    clip.id = state.nextClipId++;
    const auto label = clip.durationText();

    // Recorded as an undoable step, so an accidental press is reversible and
    // the capacity limit cannot silently destroy the oldest keep.
    perform (new AddKeepAction (state, std::move (clip)), "Capture");

    state.loadPreviewFrom (state.keeps.front());
    state.lastMessage = "Kept " + label;

    // Start the window again, so the buffer clock measures "since I kept
    // that" rather than "since I opened the plug-in". Recording does not
    // stop - the ring never stops - it just drops what it was holding.
    if (state.restartBufferAfterKeep)
    {
        clearBuffer();
        state.lastMessage = "Kept " + label + " - buffer restarted";
    }

    if (onCaptureApplied)
        onCaptureApplied();
}

void KeepThatProcessor::tickMessageThread()
{
    previewPlayer.collectRetired();

    if (hasSeenAudio())
    {
        state.bufferAvailable = ring.availableSeconds();
        state.bufferMax = ring.maxSeconds();
    }

    // Keep the PHRASE DETECTED card current without ever competing with a real
    // capture - requestPhraseScan declines while the engine is busy.
    //
    // Every 3 s over a 10 s window, not every 1.5 s over 20 s. The card does
    // not need sub-second freshness, and key and tempo detection each run a
    // few hundred FFTs per scan - at the old cadence that was continuous
    // background work for a readout that barely changes.
    const auto now = juce::Time::currentTimeMillis();
    if (now - lastPhraseScanMs > 3000 && ring.availableSeconds() > 1.0)
    {
        lastPhraseScanMs = now;
        scanForPhrase();
    }
}

// -----------------------------------------------------------------------------
//  State. Parameters ride in the APVTS tree; everything else is session data.
//  THREADING_EXPORT_SPEC: "store metadata, not raw audio".
// -----------------------------------------------------------------------------
void KeepThatProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = apvts.copyState();
    tree.setProperty ("stateVersion", params::stateVersion, nullptr);

    juce::ValueTree session ("SESSION");
    session.setProperty ("preset", state.presetName, nullptr);
    session.setProperty ("source", state.sourceName, nullptr);
    session.setProperty ("armed", state.armed, nullptr);
    session.setProperty ("restartBufferAfterKeep", state.restartBufferAfterKeep, nullptr);
    session.setProperty ("selectedLength", state.selectedLength, nullptr);
    session.setProperty ("selectedSeconds", state.selectedSeconds, nullptr);
    session.setProperty ("destination", (int) state.destination, nullptr);
    session.setProperty ("showBarsBeats", state.showBarsBeats, nullptr);
    session.setProperty ("selectedKeep", state.selectedKeep, nullptr);
    // The SETTINGS toggles. Not persisting these meant every one of them
    // quietly reset each time the session reopened.
    session.setProperty ("lowPowerMode", state.lowPowerMode, nullptr);
    session.setProperty ("reduceMotion", state.reduceMotion, nullptr);
    session.setProperty ("writePlaylistFile", state.writePlaylistFile, nullptr);
    for (int i = 0; i < 5; ++i)
        if (state.destinationFolder[i] != juce::File())
            session.setProperty ("destFolder" + juce::String (i),
                                 state.destinationFolder[i].getFullPathName(), nullptr);

    // Only clips the user actually saved carry a path worth restoring; an
    // unsaved capture lives in a temp file that will be swept.
    juce::ValueTree keeps ("KEEPS");
    for (const auto& clip : state.keeps)
    {
        if (clip.file == juce::File() || ! clip.file.existsAsFile())
            continue;

        juce::ValueTree k ("KEEP");
        k.setProperty ("name", clip.name, nullptr);
        k.setProperty ("seconds", clip.seconds, nullptr);
        k.setProperty ("favourite", clip.favourite, nullptr);
        k.setProperty ("file", clip.file.getFullPathName(), nullptr);
        k.setProperty ("createdAt", clip.createdAtMs, nullptr);
        k.setProperty ("key", clip.key, nullptr);
        k.setProperty ("bars", clip.detectedBars, nullptr);
        k.setProperty ("peakDb", clip.peakDb, nullptr);
        keeps.appendChild (k, nullptr);
    }
    session.appendChild (keeps, nullptr);
    tree.appendChild (session, nullptr);

    juce::MemoryOutputStream stream (destData, false);
    tree.writeToStream (stream);
}

void KeepThatProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! tree.isValid() || tree.getType() != apvts.state.getType())
        return;

    auto session = tree.getChildWithName ("SESSION");
    apvts.replaceState (tree);

    if (! session.isValid())
        return;

    state.presetName      = session.getProperty ("preset", state.presetName).toString();
    state.sourceName      = session.getProperty ("source", state.sourceName).toString();
    state.armed           = session.getProperty ("armed", state.armed);
    state.restartBufferAfterKeep = session.getProperty ("restartBufferAfterKeep",
                                                        state.restartBufferAfterKeep);
    state.selectedLength  = session.getProperty ("selectedLength", state.selectedLength);
    state.selectedSeconds = session.getProperty ("selectedSeconds", state.selectedSeconds);
    state.destination     = (Destination) (int) session.getProperty ("destination",
                                                                     (int) state.destination);
    state.showBarsBeats   = session.getProperty ("showBarsBeats", state.showBarsBeats);
    state.lowPowerMode    = session.getProperty ("lowPowerMode", state.lowPowerMode);
    state.reduceMotion    = session.getProperty ("reduceMotion", state.reduceMotion);
    state.writePlaylistFile = session.getProperty ("writePlaylistFile", state.writePlaylistFile);
    for (int i = 0; i < 5; ++i)
    {
        const auto path = session.getProperty ("destFolder" + juce::String (i)).toString();
        if (path.isNotEmpty())
            state.destinationFolder[i] = juce::File (path);
    }

    auto keeps = session.getChildWithName ("KEEPS");
    if (! keeps.isValid())
        return;

    // Restored clips are metadata only: the audio is re-read from its WAV on
    // demand, so a session with a hundred keeps does not drag a hundred
    // buffers into memory on load.
    std::vector<CaptureClip> restored;
    for (const auto& k : keeps)
    {
        CaptureClip clip;
        clip.name = k.getProperty ("name").toString();
        clip.seconds = k.getProperty ("seconds");
        clip.favourite = k.getProperty ("favourite");
        clip.file = juce::File (k.getProperty ("file").toString());
        clip.createdAtMs = k.getProperty ("createdAt");
        clip.key = k.getProperty ("key", "--").toString();
        clip.detectedBars = k.getProperty ("bars", 0.0);
        clip.peakDb = k.getProperty ("peakDb", -100.0f);
        clip.id = state.nextClipId++;
        if (clip.file.existsAsFile())
            restored.push_back (std::move (clip));
    }
    if (! restored.empty())
    {
        state.keeps = std::move (restored);
        state.selectedKeep = juce::jlimit (0, (int) state.keeps.size() - 1,
                                (int) session.getProperty ("selectedKeep", 0));
    }
}

juce::AudioProcessorEditor* KeepThatProcessor::createEditor()
{
    return new KeepThatEditor (*this);
}

} // namespace keepthat

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new keepthat::KeepThatProcessor();
}
