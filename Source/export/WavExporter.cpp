#include "WavExporter.h"

namespace keepthat
{

juce::File WavExporter::tempDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("KEEP THAT!");
    dir.createDirectory();
    return dir;
}

void WavExporter::sweepTempFiles (int olderThanHours)
{
    auto dir = tempDirectory();
    if (! dir.isDirectory())
        return;

    const auto cutoff = juce::Time::getCurrentTime()
                          - juce::RelativeTime::hours (olderThanHours);

    for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.wav"))
    {
        const auto file = entry.getFile();
        // A file being dragged right now by another instance must survive, so
        // anything newer than the cutoff is left alone.
        if (olderThanHours <= 0 || file.getLastModificationTime() < cutoff)
            file.deleteFile();
    }
}

juce::String WavExporter::safeFileName (const juce::String& name)
{
    auto cleaned = juce::File::createLegalFileName (name).trim();
    // createLegalFileName still allows leading dots and empty results, both of
    // which produce files the user cannot find.
    while (cleaned.startsWithChar ('.'))
        cleaned = cleaned.substring (1);
    if (cleaned.isEmpty())
        cleaned = "Keep";
    return cleaned.substring (0, 100);
}

bool WavExporter::writeWav (const CaptureClip& clip, const juce::File& destination)
{
    if (! clip.hasAudio() || destination == juce::File())
        return false;

    destination.getParentDirectory().createDirectory();

    // Write to a sibling temp file and rename on success, so a failure part
    // way through never leaves a half-written WAV where a good one used to be.
    auto staging = destination.getSiblingFile (destination.getFileNameWithoutExtension()
                                               + ".partial-" + juce::String (juce::Random::getSystemRandom().nextInt (99999))
                                               + ".wav");

    {
        auto stream = std::unique_ptr<juce::FileOutputStream> (staging.createOutputStream());
        if (stream == nullptr)
            return false;

        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (stream.get(),
                                    clip.sampleRate > 0.0 ? clip.sampleRate : 48000.0,
                                    (unsigned int) clip.audio->getNumChannels(),
                                    24, {}, 0));
        if (writer == nullptr)
        {
            staging.deleteFile();
            return false;
        }
        stream.release();                 // the writer owns it now

        if (! writer->writeFromAudioSampleBuffer (*clip.audio, 0, clip.audio->getNumSamples()))
        {
            writer.reset();
            staging.deleteFile();
            return false;
        }
    }                                     // writer flushes and closes here

    destination.deleteFile();
    if (! staging.moveFileTo (destination))
    {
        staging.deleteFile();
        return false;
    }
    return true;
}

juce::File WavExporter::writeTempWav (const CaptureClip& clip)
{
    auto target = tempDirectory().getChildFile (safeFileName (clip.name) + ".wav");
    target = target.getNonexistentSibling();
    return writeWav (clip, target) ? target : juce::File();
}

juce::File WavExporter::directoryFor (Destination destination, const SessionState& state)
{
    const int index = juce::jlimit (0, 4, (int) destination);
    const auto chosen = state.destinationFolder[index];
    if (chosen != juce::File() && chosen.isDirectory())
        return chosen;
    return defaultDirectoryFor (destination);
}

bool WavExporter::addToPlaylist (const juce::File& folder, const juce::File& file)
{
    if (folder == juce::File() || ! file.existsAsFile())
        return false;

    auto playlist = folder.getChildFile ("KEEP THAT!.m3u");
    const auto line = file.getFullPathName();

    // Skip a path already listed, so re-saving the same clip does not grow the
    // playlist without bound.
    if (playlist.existsAsFile() && playlist.loadFileAsString().contains (line))
        return true;

    if (! playlist.existsAsFile())
        playlist.replaceWithText ("#EXTM3U\n");

    return playlist.appendText (line + "\n");
}

void WavExporter::reveal (const juce::File& target)
{
    if (target.exists())
        target.revealToUser();
}

juce::File WavExporter::defaultDirectoryFor (Destination destination)
{
    using F = juce::File;
    switch (destination)
    {
        case Destination::desktop:
            return F::getSpecialLocation (F::userDesktopDirectory);

        case Destination::folder:
            return F::getSpecialLocation (F::userMusicDirectory)
                       .getChildFile ("KEEP THAT!");

        case Destination::sampler:
            // A default a sampler can be pointed at. The user can repoint this
            // at their own sampler's library folder, which is the honest form
            // of "sampler integration" for a plugin that cannot know which
            // sampler they use.
            return F::getSpecialLocation (F::userMusicDirectory)
                       .getChildFile ("KEEP THAT!").getChildFile ("Sampler");

        case Destination::playlist:
            return F::getSpecialLocation (F::userMusicDirectory)
                       .getChildFile ("KEEP THAT!").getChildFile ("Playlist");

        case Destination::dawDrag:
        default:
            return {};                    // not a folder
    }
}

bool WavExporter::beginDrag (const CaptureClip& clip, juce::Component* source)
{
    // The file has to be complete before the drag starts: a host that reads a
    // partial WAV will cache the broken result.
    const auto file = writeTempWav (clip);
    if (file == juce::File())
        return false;

    juce::StringArray files;
    files.add (file.getFullPathName());

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (source))
        container->performExternalDragDropOfFiles (files, false, source);
    else
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, source);

    return true;
}

} // namespace keepthat
