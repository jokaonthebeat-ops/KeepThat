/*
    WavExporter.h - writing captures to disk, and handing them to the OS drag.

    Everything here is worker-thread work (THREADING_EXPORT_SPEC: "Worker pool:
    ... WAV writing"), except `beginDrag`, which must run on the message thread
    because that is where the drag originates - but it writes its file FIRST
    and only then starts the drag, exactly as the spec requires ("create a
    stable temporary WAV before beginning the OS drag operation"). A drag that
    starts before the file is complete hands the host a truncated or missing
    file, and hosts cache that.

    Temporary files are tracked so they can be swept on startup and shutdown.
    A capture the user never saved should not still be occupying their disk
    next month.
*/

#pragma once
#include <JuceHeader.h>
#include "../capture/CaptureModel.h"

namespace keepthat
{

class WavExporter
{
public:
    /** The directory drag files and unsaved captures live in. Created lazily. */
    static juce::File tempDirectory();

    /** Deletes temp WAVs from previous runs. Called on construction and on
        shutdown; safe to call at any time. `olderThanHours` protects files a
        second instance may still be dragging. */
    static void sweepTempFiles (int olderThanHours = 0);

    /** Writes `clip` to `destination` as a 24-bit WAV at its own sample rate.
        Returns false and leaves nothing behind on failure. WORKER THREAD. */
    static bool writeWav (const CaptureClip& clip, const juce::File& destination);

    /** Writes `clip` into the temp directory under a filesystem-safe version
        of its name, and returns the file. Empty on failure. WORKER THREAD. */
    static juce::File writeTempWav (const CaptureClip& clip);

    /** The built-in default folder for a destination. Empty for `dawDrag`,
        which is not a folder. */
    static juce::File defaultDirectoryFor (Destination);

    /** The folder a destination actually writes to: the user's choice if they
        set one, otherwise the default. */
    static juce::File directoryFor (Destination, const SessionState&);

    /** Appends `file` to the playlist .m3u in `folder`, creating it if needed
        and skipping duplicates. This is what makes PLAYLIST a playlist rather
        than a second folder with a different name. */
    static bool addToPlaylist (const juce::File& folder, const juce::File& file);

    /** Opens the folder in the OS file browser. */
    static void reveal (const juce::File&);

    /** MESSAGE THREAD. Writes a temp WAV then starts the OS drag from
        `source`. Returns false if the file could not be written - in which
        case no drag is started, rather than one that hands over nothing. */
    static bool beginDrag (const CaptureClip& clip, juce::Component* source);

    /** A filename that will survive every filesystem the user might be on. */
    static juce::String safeFileName (const juce::String& name);

private:
    WavExporter() = delete;
};

} // namespace keepthat
