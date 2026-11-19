// K Engine — Engine/Editing/Command.h
//
// An undoable edit of a Scene (reference §10.2). Commands refer to objects by
// persistent UUID, never by Handle: a handle dies when its object is deleted,
// while undoing a delete must bring back the same object (same UUID) and every
// later command in the history must still find it.
//
// Contract:
//   apply():  performs the edit. On failure the scene is unchanged.
//   revert(): exactly undoes a successful apply() (given that the scene was
//             only modified through the history since). On failure the scene
//             is unchanged and the history keeps the command.
//   apply() may be called again after revert() (redo) and must reproduce the
//   same result, including UUIDs of created objects.
#pragma once

#include <string>

#include "Engine/Core/Result.h"
#include "Engine/Scene/Scene.h"

namespace k {

class Command {
public:
    virtual ~Command() = default;

    // Human-readable label for menus/status bars, e.g. "Delete 'Cube'".
    virtual std::string name() const = 0;

    virtual Status apply(Scene& scene) = 0;
    virtual Status revert(Scene& scene) = 0;

    // Try to absorb `later`, a command that was applied right after this one.
    // Return true if this command now represents both (used to coalesce drags).
    virtual bool try_merge(const Command& later) {
        (void)later;
        return false;
    }
};

}  // namespace k
