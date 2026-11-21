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

std::string explorer_label(const ExplorerRow& row) { return row.name + "  [" + to_string(row.kind) + "]"; }

}  // namespace k
