#include "Engine/Rendering/DrawList.h"

#include "Engine/Scene/SceneTransforms.h"

namespace k {

std::vector<DrawItem> build_draw_list(const Scene& scene, ObjectHandle selected) {
    const auto worlds = compute_world_matrices(scene);
    std::vector<DrawItem> items;
    items.reserve(scene.object_count());
    scene.visit_preorder([&](ObjectHandle h, const SceneObject& o) {
        if (o.kind == ObjectKind::Empty) return;
        DrawItem item;
        item.object = h;
        item.kind = o.kind;
        const Mat4& w = worlds.at(h.index);
        for (std::size_t i = 0; i < 16; ++i) item.world[i] = static_cast<float>(w.m[i]);
        item.selected = !selected.is_null() && h == selected;
        items.push_back(item);
    });
    return items;
}

}  // namespace k
