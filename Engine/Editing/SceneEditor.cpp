#include "Engine/Editing/SceneEditor.h"

#include "Engine/Editing/SceneCommands.h"

namespace k {

Status SceneEditor::refuse_if_playing() const {
    if (playing()) {
        return make_error("editor.read_only_while_playing", "the scene cannot be edited while Play is running; stop first");
    }
    return Status::success();
}

Result<SceneEditor> SceneEditor::open(const std::filesystem::path& path, std::vector<std::string>* warnings) {
    auto loaded = load_scene_file(path);
    if (!loaded) return loaded.error();
    if (warnings) *warnings = loaded.value().warnings;
    return SceneEditor(std::move(loaded.value().scene));
}

Status SceneEditor::save(const std::filesystem::path& path, const SaveOptions& options) {
    if (Status st = save_scene_file(scene_, path, options); !st) return st;  // stays dirty on failure
    history_.mark_clean();
    return Status::success();
}

Result<Uuid> SceneEditor::create(ObjectKind kind, std::string name, const Uuid& parent) {
    if (Status st = refuse_if_playing(); !st) return st.error();
    Uuid id = Uuid::generate();
    while (!scene_.find_by_uuid(id).is_null()) id = Uuid::generate();
    if (Status st = history_.execute(scene_, make_create_command(id, kind, std::move(name), parent)); !st) return st.error();
    return id;
}

Status SceneEditor::destroy(const Uuid& id) {
    if (Status st = refuse_if_playing(); !st) return st;
    return history_.execute(scene_, make_delete_command(id));
}

Status SceneEditor::set_transform(const Uuid& id, const Transform& transform, bool merge) {
    if (Status st = refuse_if_playing(); !st) return st;
    return history_.execute(scene_, make_set_transform_command(id, transform), merge);
}

Status SceneEditor::rename(const Uuid& id, std::string name) {
    if (Status st = refuse_if_playing(); !st) return st;
    return history_.execute(scene_, make_rename_command(id, std::move(name)));
}

Status SceneEditor::reparent(const Uuid& id, const Uuid& new_parent, std::size_t sibling_index, ReparentMode mode) {
    if (Status st = refuse_if_playing(); !st) return st;
    return history_.execute(scene_, make_reparent_command(id, new_parent, sibling_index, mode));
}

Status SceneEditor::undo() {
    if (Status st = refuse_if_playing(); !st) return st;
    return history_.undo(scene_);
}

Status SceneEditor::redo() {
    if (Status st = refuse_if_playing(); !st) return st;
    return history_.redo(scene_);
}

Status SceneEditor::begin_play() {
    if (playing()) return make_error("editor.already_playing", "Play is already running");
    history_.break_merge();
    play_ = std::make_unique<Scene>(scene_);  // independent deep copy, includes unsaved edits
    return Status::success();
}

Status SceneEditor::end_play() {
    if (!playing()) return make_error("editor.not_playing", "Play is not running");
    play_.reset();  // everything the runtime did to the copy is discarded
    return Status::success();
}

}  // namespace k
