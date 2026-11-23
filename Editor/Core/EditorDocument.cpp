#include "Editor/Core/EditorDocument.h"

#include <unordered_set>

namespace k {

namespace {

std::string utf8_of(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

const char* default_base_name(ObjectKind kind) {
    switch (kind) {
        case ObjectKind::Empty: return "Empty";
        case ObjectKind::Cube: return "Cube";
        case ObjectKind::Sphere: return "Sphere";
        case ObjectKind::Plane: return "Plane";
    }
    return "Object";
}

Status read_only_while_playing() {
    return make_error("editor.read_only_while_playing", "the scene cannot be edited while Play is running; stop first");
}

}  // namespace

std::string EditorDocument::display_name() const {
    return path_.empty() ? std::string("Untitled") : utf8_of(path_.filename());
}

std::string EditorDocument::title() const {
    std::string t = display_name();
    if (is_dirty()) t += "*";
    if (playing()) t += " [Playing]";
    return t;
}

Status EditorDocument::new_scene() {
    if (playing()) return read_only_while_playing();
    editor_ = SceneEditor();
    path_.clear();
    selected_ = Uuid();
    warnings_.clear();
    return Status::success();
}

Status EditorDocument::adopt_scene(Scene scene) {
    if (playing()) return read_only_while_playing();
    editor_ = SceneEditor(std::move(scene));
    path_.clear();
    selected_ = Uuid();
    warnings_.clear();
    return Status::success();
}

Status EditorDocument::open(const std::filesystem::path& path) {
    if (playing()) return read_only_while_playing();
    std::vector<std::string> warnings;
    auto opened = SceneEditor::open(path, &warnings);
    if (!opened) return opened.error();  // the current document stays exactly as it was
    editor_ = std::move(opened.value());
    path_ = path;
    selected_ = Uuid();
    warnings_ = std::move(warnings);
    return Status::success();
}

Status EditorDocument::save() {
    if (path_.empty()) return make_error("document.no_path", "this scene has not been saved yet; use Save As");
    return editor_.save(path_);
}

Status EditorDocument::save_as(const std::filesystem::path& path) {
    if (Status st = editor_.save(path); !st) return st;  // path_ unchanged on failure
    path_ = path;
    return Status::success();
}

Status EditorDocument::select(const Uuid& id) {
    if (id.is_nil()) {
        selected_ = Uuid();
        return Status::success();
    }
    if (scene().find_by_uuid(id).is_null()) {
        return make_error("command.object_not_found", "no object with UUID " + id.to_string() + " in the scene");
    }
    selected_ = id;
    return Status::success();
}

const SceneObject* EditorDocument::selected_object() const {
    return selected_.is_nil() ? nullptr : scene().get(scene().find_by_uuid(selected_));
}

ObjectHandle EditorDocument::selected_handle() const {
    return selected_.is_nil() ? kNullObjectHandle : scene().find_by_uuid(selected_);
}

void EditorDocument::sanitize_selection() {
    if (!selected_.is_nil() && scene().find_by_uuid(selected_).is_null()) selected_ = Uuid();
}

std::string EditorDocument::unique_name(const std::string& base) const {
    std::unordered_set<std::string> used;
    editor_.scene().visit_preorder([&](ObjectHandle, const SceneObject& o) { used.insert(o.name); });
    if (!used.count(base)) return base;
    for (int n = 2;; ++n) {
        std::string candidate = base + " " + std::to_string(n);
        if (!used.count(candidate)) return candidate;
    }
}

Result<Uuid> EditorDocument::create(ObjectKind kind) {
    auto id = editor_.create(kind, unique_name(default_base_name(kind)));
    if (!id) return id;
    selected_ = id.value();
    return id;
}

Status EditorDocument::delete_selected() {
    if (selected_.is_nil()) return make_error("document.no_selection", "nothing is selected");
    Status st = editor_.destroy(selected_);
    sanitize_selection();
    return st;
}

Status EditorDocument::rename_selected(std::string name) {
    if (selected_.is_nil()) return make_error("document.no_selection", "nothing is selected");
    return editor_.rename(selected_, std::move(name));
}

Status EditorDocument::set_selected_transform(const Transform& transform, bool merge) {
    if (selected_.is_nil()) return make_error("document.no_selection", "nothing is selected");
    return editor_.set_transform(selected_, transform, merge);
}

Status EditorDocument::can_reparent(const Uuid& id, const Uuid& new_parent) const {
    if (playing())
        return make_error("editor.read_only_while_playing", "the scene cannot be edited while Play is running; stop first");
    const Scene& sc = editor_.scene();
    const ObjectHandle h = sc.find_by_uuid(id);
    if (h.is_null()) return make_error("command.object_not_found", "no object with UUID " + id.to_string() + " in the scene");
    if (new_parent.is_nil()) return Status::success();
    const ObjectHandle p = sc.find_by_uuid(new_parent);
    if (p.is_null())
        return make_error("command.object_not_found", "no object with UUID " + new_parent.to_string() + " in the scene");
    for (ObjectHandle cur = p; !cur.is_null();) {  // walk up from the target: is `h` an ancestor (or the target)?
        if (cur == h) return make_error("scene.cycle", "an object cannot be moved into itself or its own descendants");
        const SceneObject* o = sc.get(cur);
        cur = o ? o->parent : kNullObjectHandle;
    }
    return Status::success();
}

Status EditorDocument::reparent(const Uuid& id, const Uuid& new_parent, std::size_t sibling_index, ReparentMode mode) {
    if (Status st = can_reparent(id, new_parent); !st) return st;

    // Same parent and the index it already has: nothing to do, and no empty undo step.
    const Scene& sc = editor_.scene();
    const ObjectHandle h = sc.find_by_uuid(id);
    const SceneObject* o = sc.get(h);
    const SceneObject* cur_parent = sc.get(o->parent);
    if ((cur_parent ? cur_parent->id : Uuid()) == new_parent) {
        const std::size_t siblings = cur_parent ? cur_parent->children.size() : sc.roots().size();
        const std::size_t target = sibling_index < siblings ? sibling_index : siblings - 1;
        if (sc.sibling_index(h).value_or(siblings) == target) return Status::success();
    }
    return editor_.reparent(id, new_parent, sibling_index, mode);
}

Status EditorDocument::undo() {
    Status st = editor_.undo();
    sanitize_selection();
    return st;
}

Status EditorDocument::redo() {
    Status st = editor_.redo();
    sanitize_selection();
    return st;
}

std::string EditorDocument::undo_label() const {
    const auto n = editor_.undo_name();
    return n ? "Undo " + *n : std::string("Undo");
}

std::string EditorDocument::redo_label() const {
    const auto n = editor_.redo_name();
    return n ? "Redo " + *n : std::string("Redo");
}

Status EditorDocument::play() { return editor_.begin_play(); }

Status EditorDocument::stop() {
    Status st = editor_.end_play();
    sanitize_selection();
    return st;
}

}  // namespace k
