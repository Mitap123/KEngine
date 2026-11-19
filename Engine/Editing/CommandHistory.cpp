#include "Engine/Editing/CommandHistory.h"

namespace k {

Status CommandHistory::execute(Scene& scene, std::unique_ptr<Command> cmd, bool allow_merge) {
    if (!cmd) return make_error("history.null_command", "cannot execute a null command");
    if (Status st = cmd->apply(scene); !st) return st;  // failure: nothing else has changed

    const std::size_t pos_before = position();
    const bool had_redo = !redo_.empty();
    redo_.clear();
    // The saved state lived on the discarded redo branch: it can never be reached again.
    if (had_redo && clean_pos_ && *clean_pos_ > pos_before) clean_pos_.reset();

    // A merge never touches the saved state: mark_clean() closes the merge run, so a
    // merge target is always a step created after the save (position > clean position).
    if (allow_merge && merge_open_ && !undo_.empty() && undo_.back()->try_merge(*cmd)) {
        return Status::success();
    }

    undo_.push_back(std::move(cmd));
    merge_open_ = allow_merge;
    while (undo_.size() > max_depth_) {
        undo_.pop_front();
        ++base_;
    }
    if (clean_pos_ && *clean_pos_ < base_) clean_pos_.reset();  // saved state fell off the bottom
    return Status::success();
}

Status CommandHistory::undo(Scene& scene) {
    if (undo_.empty()) return make_error("history.nothing_to_undo", "nothing to undo");
    if (Status st = undo_.back()->revert(scene); !st) return st;  // history unchanged
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    merge_open_ = false;
    return Status::success();
}

Status CommandHistory::redo(Scene& scene) {
    if (redo_.empty()) return make_error("history.nothing_to_redo", "nothing to redo");
    if (Status st = redo_.back()->apply(scene); !st) return st;  // history unchanged
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    merge_open_ = false;
    return Status::success();
}

std::optional<std::string> CommandHistory::undo_name() const {
    if (undo_.empty()) return std::nullopt;
    return undo_.back()->name();
}

std::optional<std::string> CommandHistory::redo_name() const {
    if (redo_.empty()) return std::nullopt;
    return redo_.back()->name();
}

void CommandHistory::mark_clean() {
    clean_pos_ = position();
    merge_open_ = false;  // an edit after saving must not be folded into pre-save history
}

void CommandHistory::clear() {
    undo_.clear();
    redo_.clear();
    base_ = 0;
    clean_pos_ = 0;
    merge_open_ = false;
}

}  // namespace k
