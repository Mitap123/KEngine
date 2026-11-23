// K Engine — Engine/Editing/SceneEditor.h
//
// The Editor's document model, with no Qt and no rendering: owns the authoring
// Scene, its undo/redo history, dirty tracking, file save/open, and the
// isolated Play session. The Editor UI talks to this class; it never mutates a
// Scene directly. Objects are addressed by persistent UUID (see Command.h).
//
// Play isolation (M05-T07, ADR-0001): begin_play() makes an independent COPY of
// the authoring scene (including unsaved edits). Gameplay/runtime code mutates
// only that copy (play_scene()). end_play() discards it; the authoring scene,
// its undo history and its dirty flag are exactly as they were. While playing,
// every authoring mutation and undo/redo is refused (editor.read_only_while_playing).
// Saving while playing writes the authoring scene, never the play copy.
//
// Hazard: handles are numerically identical in the source and the copy.
// Never carry an ObjectHandle between them; use UUIDs.
//
// Error codes: command.object_not_found, editor.read_only_while_playing,
// editor.already_playing, editor.not_playing, history.*, scene.*, file.*.
// Not thread-safe.
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "Engine/Editing/CommandHistory.h"
#include "Engine/Editing/SceneCommands.h"
#include "Engine/Serialization/SceneFile.h"

namespace k {

class SceneEditor {
public:
    SceneEditor() = default;
    // Starts clean (not dirty) with `initial` as the saved state, e.g. a loaded file.
    explicit SceneEditor(Scene initial) : scene_(std::move(initial)) {}

    // Loads a scene file (warnings are returned through `warnings` if non-null).
    static Result<SceneEditor> open(const std::filesystem::path& path, std::vector<std::string>* warnings = nullptr);
    // Saves the AUTHORING scene (even while playing) and, on success, marks it clean.
    Status save(const std::filesystem::path& path, const SaveOptions& options = {});

    // The authoring scene: the source of truth, never the play copy.
    const Scene& scene() const { return scene_; }
    // What a viewport should draw: the play copy while playing, else the authoring scene.
    const Scene& active_scene() const { return play_ ? *play_ : scene_; }

    // ---- authoring edits (each one undoable step; all refused while playing) ----
    // `parent` nil = root. Returns the new object's UUID.
    Result<Uuid> create(ObjectKind kind, std::string name, const Uuid& parent = Uuid());
    Status destroy(const Uuid& id);
    // `merge`: coalesce with the previous merge-enabled transform edit (a drag).
    Status set_transform(const Uuid& id, const Transform& transform, bool merge = false);
    Status rename(const Uuid& id, std::string name);
    // See make_reparent_command() for the index and mode semantics. The default keeps the
    // transform numbers (engine behaviour before KeepWorld existed).
    Status reparent(const Uuid& id, const Uuid& new_parent, std::size_t sibling_index = Scene::kAppend,
                    ReparentMode mode = ReparentMode::KeepLocal);

    // Call when a drag/gesture ends so the next edit starts a new undo step.
    void end_edit_gesture() { history_.break_merge(); }

    Status undo();
    Status redo();
    bool can_undo() const { return !playing() && history_.can_undo(); }
    bool can_redo() const { return !playing() && history_.can_redo(); }
    std::optional<std::string> undo_name() const { return can_undo() ? history_.undo_name() : std::nullopt; }
    std::optional<std::string> redo_name() const { return can_redo() ? history_.redo_name() : std::nullopt; }

    bool is_dirty() const { return !history_.is_clean(); }

    // ---- Play / Stop ----
    bool playing() const { return play_ != nullptr; }
    Status begin_play();
    Status end_play();
    // The runtime's working copy; nullptr unless playing.
    Scene* play_scene() { return play_.get(); }

private:
    Status refuse_if_playing() const;

    Scene scene_;
    CommandHistory history_;
    std::unique_ptr<Scene> play_;
};

}  // namespace k
