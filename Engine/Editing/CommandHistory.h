// K Engine — Engine/Editing/CommandHistory.h
//
// Undo/redo stacks with merge (coalescing) and clean/dirty tracking. Pure
// logic: no Qt, no I/O. Not thread-safe.
#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Engine/Editing/Command.h"

namespace k {

class CommandHistory {
public:
    explicit CommandHistory(std::size_t max_depth = 1000) : max_depth_(max_depth < 1 ? 1 : max_depth) {}

    // Applies `cmd`; on success it becomes the newest undo step and the redo
    // branch is discarded. On failure nothing changes (scene, stacks, clean state).
    // If `allow_merge` is true and the previous successful execute also allowed
    // merging (and no undo/redo/save/break_merge happened since), the command is
    // offered to the previous one via try_merge(); if accepted, no new step is
    // added (the edit is applied to the scene and folded into the previous step).
    // Error codes: history.null_command, plus whatever apply() returns.
    Status execute(Scene& scene, std::unique_ptr<Command> cmd, bool allow_merge = false);

    // Error codes: history.nothing_to_undo / history.nothing_to_redo, plus
    // whatever revert()/apply() returns (history unchanged in that case).
    Status undo(Scene& scene);
    Status redo(Scene& scene);

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    std::optional<std::string> undo_name() const;
    std::optional<std::string> redo_name() const;
    std::size_t undo_depth() const { return undo_.size(); }
    std::size_t redo_depth() const { return redo_.size(); }

    // Ends the current merge run (call when a drag/gesture finishes).
    void break_merge() { merge_open_ = false; }

    // The current state is the saved state. is_clean() is true exactly when the
    // scene is back at that state; it becomes permanently false if the saved
    // state is no longer reachable (discarded redo branch, trimmed history).
    void mark_clean();
    bool is_clean() const { return clean_pos_.has_value() && *clean_pos_ == position(); }

    void clear();

private:
    // Absolute position: number of commands applied since the (possibly
    // trimmed) beginning of time. Stable across trimming of the oldest steps.
    std::size_t position() const { return base_ + undo_.size(); }

    std::size_t max_depth_;
    std::deque<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;  // back() is the next redo
    std::size_t base_ = 0;                        // commands dropped from the bottom
    std::optional<std::size_t> clean_pos_ = 0;    // a fresh history is clean
    bool merge_open_ = false;
};

}  // namespace k
