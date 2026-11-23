#include "Engine/Editing/SceneEditor.h"

#include "Engine/Editing/SceneCommands.h"

#include <unordered_map>
#include <utility>
#include <vector>

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

Result<Uuid> SceneEditor::duplicate(const Uuid& id, std::string new_name) {
    if (Status st = refuse_if_playing(); !st) return st.error();
    const ObjectHandle top = scene_.find_by_uuid(id);
    if (top.is_null()) return make_error("command.object_not_found", "no object with UUID " + id.to_string() + " in the scene");

    const SceneObject* top_obj = scene_.get(top);
    const SceneObject* top_parent = scene_.get(top_obj->parent);
    const Uuid parent_id = top_parent ? top_parent->id : Uuid();
    const std::size_t top_index = scene_.sibling_index(top).value_or(Scene::kAppend);
    const std::size_t sibling_count = top_parent ? top_parent->children.size() : scene_.roots().size();
    const std::string label = top_obj->name;

    // A fresh UUID is checked against the scene only; a clash inside the batch (2^-122) would make
    // the create step fail and the composite roll back cleanly, never corrupt the scene.
    auto fresh = [this] {
        Uuid u = Uuid::generate();
        while (!scene_.find_by_uuid(u).is_null()) u = Uuid::generate();
        return u;
    };

    std::vector<std::unique_ptr<Command>> steps;
    Uuid top_copy;
    std::vector<std::pair<ObjectHandle, Uuid>> stack{{top, parent_id}};  // source object, UUID of its copy's parent
    while (!stack.empty()) {
        const auto [src, copy_parent] = stack.back();
        stack.pop_back();
        const SceneObject* o = scene_.get(src);
        const Uuid copy = fresh();
        if (src == top) top_copy = copy;
        steps.push_back(make_create_command(copy, o->kind, src == top ? new_name : o->name, copy_parent));
        if (!(o->transform == Transform{})) steps.push_back(make_set_transform_command(copy, o->transform));
        for (auto it = o->children.rbegin(); it != o->children.rend(); ++it) stack.emplace_back(*it, copy);
    }
    // Creation appended the copy as the last sibling; move it to just after the original.
    if (top_index + 1 < sibling_count)
        steps.push_back(make_reparent_command(top_copy, parent_id, top_index + 1, ReparentMode::KeepLocal));

    if (Status st = history_.execute(scene_, make_composite_command("Duplicate '" + label + "'", std::move(steps))); !st)
        return st.error();
    return top_copy;
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
