#include "ClipLoader.h"
#include "ClipProcessor.h"

namespace keepthat
{

ClipLoader::ClipLoader() : juce::Thread ("KEEP THAT! clip loader")
{
    formats.registerBasicFormats();
    startThread (juce::Thread::Priority::low);   // never at the cost of audio
}

ClipLoader::~ClipLoader()
{
    signalThreadShouldExit();
    work.signal();
    stopThread (2000);
    cancelPendingUpdate();
}

void ClipLoader::request (juce::int64 token, const juce::File& file, bool urgent)
{
    if (! file.existsAsFile())
        return;

    {
        const juce::ScopedLock sl (queueLock);

        // Already queued or running - do not read the same file twice.
        if (inFlight == token)
            return;
        for (const auto& r : queue)
            if (r.token == token)
                return;

        // An urgent request is what the user is waiting on, so it goes to the
        // front; the look-ahead entries wait their turn behind it.
        if (urgent)
            queue.push_front ({ token, file });
        else
            queue.push_back ({ token, file });

        // The look-ahead must not grow without bound if somebody scrolls fast.
        while (queue.size() > 6)
            queue.pop_back();
    }
    work.signal();
}

bool ClipLoader::isLoading (juce::int64 token) const
{
    const juce::ScopedLock sl (queueLock);
    if (inFlight == token)
        return true;
    for (const auto& r : queue)
        if (r.token == token)
            return true;
    return false;
}

void ClipLoader::cancelPending()
{
    const juce::ScopedLock sl (queueLock);
    queue.clear();
}

void ClipLoader::run()
{
    while (! threadShouldExit())
    {
        Request next { 0, {} };
        {
            const juce::ScopedLock sl (queueLock);
            if (! queue.empty())
            {
                next = queue.front();
                queue.pop_front();
                inFlight = next.token;
            }
        }

        if (next.token == 0)
        {
            work.wait (250);
            continue;
        }

        Loaded loaded;
        loaded.token = next.token;

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (next.file));
        if (reader == nullptr)
        {
            loaded.failure = "Could not read " + next.file.getFileName();
        }
        else if (reader->lengthInSamples <= 0)
        {
            loaded.failure = next.file.getFileName() + " is empty";
        }
        else
        {
            auto buffer = std::make_shared<juce::AudioBuffer<float>> (
                (int) juce::jmax (1u, reader->numChannels),
                (int) reader->lengthInSamples);
            reader->read (buffer.get(), 0, buffer->getNumSamples(), 0, true, true);

            // Analyse while the audio is in hand: the thumbnail, peak and key
            // for a clip restored from an old session were never persisted (or
            // predate persisting them), and this thread is the right place to
            // recover them - it is already off the message thread and already
            // paid for the disk read. 128 bins matches capture-time thumbnails.
            clip::buildThumbnail (*buffer, 128, loaded.clip.thumbLo, loaded.clip.thumbHi);
            loaded.clip.peakDb = juce::Decibels::gainToDecibels (
                                     clip::peakMagnitude (*buffer), -100.0f);
            loaded.clip.key = KeyDetector::detect (*buffer, reader->sampleRate);

            loaded.clip.audio = std::move (buffer);
            loaded.clip.sampleRate = reader->sampleRate;
        }

        {
            const juce::ScopedLock sl (resultLock);
            results.push_back (std::move (loaded));
        }
        {
            const juce::ScopedLock sl (queueLock);
            inFlight = 0;
        }
        triggerAsyncUpdate();
    }
}

void ClipLoader::handleAsyncUpdate()
{
    std::vector<Loaded> ready;
    {
        const juce::ScopedLock sl (resultLock);
        ready.swap (results);
    }

    for (auto& r : ready)
    {
        if (r.clip.audio != nullptr)
        {
            if (onLoaded) onLoaded (r.token, std::move (r.clip));
        }
        else if (onFailed)
        {
            onFailed (r.token, r.failure);
        }
    }
}

} // namespace keepthat
