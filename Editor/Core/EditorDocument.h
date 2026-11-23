// K Engine — Editor/Core/EditorDocument.h
//
// The Editor's document, with no Qt: one open scene + its file path + the
// current selection (by UUID) on top of SceneEditor. Every Editor window action
// maps to a method here, so the behaviour is unit-tested without a GUI.
//
// Selection is a UUID, never a handle: it survives undo/redo/Play, and it is
// cleared automatically whenever the selected object no longer exists in the
// scene that is currently shown (the play copy while playing).
//
// Not thread-safe (UI thread only).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Editor/Core/ExplorerModel.h"
#include "Engine/Editing/SceneEditor.h"

namespace k {

class EditorDocument {
public:
    // ---- file ----
    // Replace the content with an empty untitled scene (the caller asks about unsaved
    // changes first). Refused while playing: editor.read_only_while_playing.
    Status new_scene();
    // Replaces the content with `scene` as an untitled, clean document (demo content, imports).
    // Refused while playing.
    Status adopt_scene(Scene scene);
    // Loads a file. On failure the current document is left untouched. Warnings from the
    // load (e.g. ignored unknown fields) are available via warnings().
    Status open(const std::filesystem::path& path);
    // Error document.no_path if the document is untitled.
    Status save();
    Status save_as(const std::filesystem::path& path);

    bool has_path() const { return !path_.empty(); }
    const std::filesystem::path& path() const { return path_; }
    // "Untitled" or the file name (UTF-8).
    std::string display_name() const;
    // display_name + "*" when dirty + " [Playing]" while playing.
    std::string title() const;
    bool is_dirty() const { return editor_.is_dirty(); }
    const std::vector<std::string>& warnings() const { return warnings_; }

    // ---- state ----
    const SceneEditor& editor() const { return editor_; }
    // The scene to show/pick: the play copy while playing, else the authoring scene.
    const Scene& scene() const { return editor_.active_scene(); }

    // ---- selection ----
    const Uuid& selected() const { return selected_; }
    // Error command.object_not_found if the UUID is not in the shown scene (selection unchanged).
    Status select(const Uuid& id);
    void clear_selection() { selected_ = Uuid(); }
    const SceneObject* selected_object() const;
    // Handle of the selection in the shown scene (null if none). Valid only until the next edit.
    ObjectHandle selected_handle() const;

    // ---- edits (refused while playing; each is one undo step) ----
    // New object at the origin under the root, with a unique default name ("Cube", "Cube 2", ...).
    // It becomes the selection.
    Result<Uuid> create(ObjectKind kind);
    // Error document.no_selection if nothing is selected.
    Status delete_selected();
    Status rename_selected(std::string name);
    // `merge`: coalesce with the previous merge-enabled edit (a drag); call end_edit_gesture() when it ends.
    Status set_selected_transform(const Transform& transform, bool merge = false);
    void end_edit_gesture() { editor_.end_edit_gesture(); }

    // Copies the selected object and its subtree as one undo step; the copy is named like the
    // original with the next free number ("Cube" -> "Cube 2", "Cube 2" -> "Cube 3"), sits right
    // after it and becomes the selection. Error document.no_selection if nothing is selected.
    Result<Uuid> duplicate_selected();

    // Moves `id` under `new_parent` (nil = root) at `sibling_index` among the new siblings,
    // counted after the object is removed from its old spot (Scene::kAppend = last; the same
    // parent reorders). This is what an Explorer drag-and-drop calls. One undo step; the
    // selection is kept (it is a UUID). Defaults to KeepWorld so the object does not jump.
    // A move that would leave the object exactly where it is succeeds without an undo step.
    // Errors: see can_reparent() and make_reparent_command() (KeepWorld errors).
    Status reparent(const Uuid& id, const Uuid& new_parent, std::size_t sibling_index = Scene::kAppend,
                    ReparentMode mode = ReparentMode::KeepWorld);
    // Would a drop of `id` on `new_parent` be accepted? Lets the UI refuse a target while the
    // user hovers, without changing anything. Ok, or editor.read_only_while_playing,
    // command.object_not_found, scene.cycle (onto itself or into its own subtree). It does not
    // predict the KeepWorld shear/singular errors; those only show when the move is tried.
    Status can_reparent(const Uuid& id, const Uuid& new_parent) const;

    // Explorer drag-and-drop: resolve_drop() + can_reparent() / reparent() (KeepWorld). `target` is the
    // row dropped on (ignored for OnEmptySpace). can_drop() is the side-effect-free hover check: Ok means
    // the drop would be accepted structurally (it does not predict the KeepWorld shear/singular errors).
    Status can_drop(const Uuid& dragged, const Uuid& target, DropPosition position) const;
    Status reparent_by_drop(const Uuid& dragged, const Uuid& target, DropPosition position);

    Status undo();
    Status redo();
    bool can_undo() const { return editor_.can_undo(); }
    bool can_redo() const { return editor_.can_redo(); }
    std::string undo_label() const;  // "Undo Create 'Cube'" or "Undo"
    std::string redo_label() const;

    // ---- Play ----
    bool playing() const { return editor_.playing(); }
    Status play();
    Status stop();
    Scene* play_scene() { return editor_.play_scene(); }

private:
    void sanitize_selection();
    std::string unique_name(const std::string& base) const;

    SceneEditor editor_;
    std::filesystem::path path_;
    Uuid selected_;
    std::vector<std::string> warnings_;
};

}  // namespace k
