/*
    SessionHistory.h - undoable session actions.

    The undo manager here covers SESSION actions, not parameters. That split is
    deliberate: hooking an UndoManager to the APVTS means every automation
    frame a host sends becomes an undo step, and the history fills with noise
    the user never performed. What actually needs undoing is the destructive
    stuff - deleting a keep, renaming one, losing one to the capacity limit.

    Deleting a keep is the important case. Without this, one mis-click on a
    card's trash icon permanently loses a take the plugin exists to protect.
*/

#pragma once
#include <JuceHeader.h>
#include "../capture/CaptureModel.h"

namespace keepthat
{

/** Removes a keep, remembering enough to put it back exactly where it was. */
class DeleteKeepAction : public juce::UndoableAction
{
public:
    DeleteKeepAction (SessionState& s, int indexToRemove)
        : state (s), index (indexToRemove) {}

    bool perform() override
    {
        if (index < 0 || index >= (int) state.keeps.size())
            return false;

        removed = state.keeps[(size_t) index];
        state.keeps.erase (state.keeps.begin() + index);
        state.selectedKeep = juce::jlimit (0, juce::jmax (0, (int) state.keeps.size() - 1),
                                           state.selectedKeep);
        return true;
    }

    bool undo() override
    {
        const int at = juce::jlimit (0, (int) state.keeps.size(), index);
        state.keeps.insert (state.keeps.begin() + at, removed);
        state.selectedKeep = at;
        return true;
    }

    /** The clip's audio is held by the action while it is undoable, so the
        estimate is honest about what is being kept alive. */
    int getSizeInUnits() override
    {
        return removed.hasAudio()
                 ? (int) (removed.audio->getNumSamples() * removed.audio->getNumChannels()
                          * sizeof (float) / 1024) : 1;
    }

private:
    SessionState& state;
    int index;
    CaptureClip removed;
};

class RenameKeepAction : public juce::UndoableAction
{
public:
    RenameKeepAction (SessionState& s, int indexToRename, juce::String newName)
        : state (s), index (indexToRename), after (std::move (newName)) {}

    bool perform() override
    {
        if (index < 0 || index >= (int) state.keeps.size())
            return false;
        before = state.keeps[(size_t) index].name;
        if (before == after)
            return false;                     // nothing to record
        state.keeps[(size_t) index].name = after;
        return true;
    }

    bool undo() override
    {
        if (index < 0 || index >= (int) state.keeps.size())
            return false;
        state.keeps[(size_t) index].name = before;
        return true;
    }

private:
    SessionState& state;
    int index;
    juce::String before, after;
};

class FavouriteKeepAction : public juce::UndoableAction
{
public:
    FavouriteKeepAction (SessionState& s, int indexToToggle)
        : state (s), index (indexToToggle) {}

    bool perform() override { return flip(); }
    bool undo() override    { return flip(); }

private:
    bool flip()
    {
        if (index < 0 || index >= (int) state.keeps.size())
            return false;
        auto& f = state.keeps[(size_t) index].favourite;
        f = ! f;
        return true;
    }

    SessionState& state;
    int index;
};

/** Recording a capture as undoable means an accidental press is reversible and,
    more usefully, that UNDO does the obvious thing right after one. */
class AddKeepAction : public juce::UndoableAction
{
public:
    AddKeepAction (SessionState& s, CaptureClip clipToAdd)
        : state (s), clip (std::move (clipToAdd)) {}

    bool perform() override
    {
        state.keeps.insert (state.keeps.begin(), clip);

        // The capacity limit would otherwise silently destroy the oldest keep,
        // so it is captured here and restored on undo.
        if ((int) state.keeps.size() > state.keepCapacity)
        {
            evicted = state.keeps.back();
            didEvict = true;
            state.keeps.pop_back();
        }
        state.selectedKeep = 0;
        return true;
    }

    bool undo() override
    {
        if (! state.keeps.empty())
            state.keeps.erase (state.keeps.begin());
        if (didEvict)
            state.keeps.push_back (evicted);
        state.selectedKeep = 0;
        return true;
    }

    int getSizeInUnits() override
    {
        return clip.hasAudio()
                 ? (int) (clip.audio->getNumSamples() * clip.audio->getNumChannels()
                          * sizeof (float) / 1024) : 1;
    }

private:
    SessionState& state;
    CaptureClip clip, evicted;
    bool didEvict = false;
};

} // namespace keepthat
