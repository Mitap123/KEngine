#include "Editor/Core/ExplorerModel.h"

#include <unordered_map>

namespace k {

std::vector<ExplorerRow> build_explorer_rows(const Scene& scene) {
    std::vector<ExplorerRow> rows;
    rows.reserve(scene.object_count());
    std::unordered_map<std::uint32_t, int> depth;  // handle slot -> depth; parents are visited first
    scene.visit_preorder([&](ObjectHandle h, const SceneObject& o) {
        const SceneObject* parent = scene.get(o.parent);
        const int d = parent ? depth.at(o.parent.index) + 1 : 0;
        depth.emplace(h.index, d);
        rows.push_back({o.id, parent ? parent->id : Uuid(), o.name, o.kind, d});
    });
    return rows;
}

Result<DropPlacement> resolve_drop(const Scene& scene, const Uuid& dragged, const Uuid& target, DropPosition position) {
    const ObjectHandle d = scene.find_by_uuid(dragged);
    if (d.is_null()) return make_error("command.object_not_found", "no object with UUID " + dragged.to_string() + " in the scene");
    if (position == DropPosition::OnEmptySpace) return DropPlacement{Uuid(), Scene::kAppend};

    const ObjectHandle t = scene.find_by_uuid(target);
    if (t.is_null()) return make_error("command.object_not_found", "no object with UUID " + target.to_string() + " in the scene");
    if (position == DropPosition::OnItem) {
        if (d == t) return make_error("scene.cycle", "an object cannot be dropped onto itself");
        return DropPlacement{target, Scene::kAppend};
    }

    const SceneObject* dobj = scene.get(d);
    const SceneObject* tobj = scene.get(t);
    const SceneObject* dparent = scene.get(dobj->parent);
    const SceneObject* tparent = scene.get(tobj->parent);
    const Uuid new_parent = tparent ? tparent->id : Uuid();
    const std::size_t s = scene.sibling_index(d).value_or(0);
    if (d == t) return DropPlacement{dparent ? dparent->id : Uuid(), s};  // above/below itself: stays where it is

    std::size_t index = scene.sibling_index(t).value_or(0);
    if (dobj->parent == tobj->parent && s < index) --index;  // the dragged object leaves a gap before the target
    if (position == DropPosition::BelowItem) ++index;
    return DropPlacement{new_parent, index};
}

std::string explorer_label(const ExplorerRow& row) { return row.name + "  [" + to_string(row.kind) + "]"; }

}  // namespace k
