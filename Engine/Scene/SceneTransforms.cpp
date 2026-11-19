#include "Engine/Scene/SceneTransforms.h"

#include <vector>

namespace k {

Mat4 local_matrix(const Transform& t) {
    return Mat4::trs(t.position, t.rotation_euler_deg, t.scale);
}

std::optional<Mat4> world_matrix(const Scene& scene, ObjectHandle h) {
    const SceneObject* o = scene.get(h);
    if (!o) return std::nullopt;
    // Collect the chain leaf -> root, then multiply root -> leaf.
    std::vector<const SceneObject*> chain;
    for (; o != nullptr; o = scene.get(o->parent)) chain.push_back(o);
    Mat4 world = Mat4::identity();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) world = world * local_matrix((*it)->transform);
    return world;
}

std::unordered_map<std::uint32_t, Mat4> compute_world_matrices(const Scene& scene) {
    std::unordered_map<std::uint32_t, Mat4> worlds;
    worlds.reserve(scene.object_count());
    // Preorder guarantees a parent is visited before its children.
    scene.visit_preorder([&](ObjectHandle h, const SceneObject& o) {
        Mat4 world = local_matrix(o.transform);
        if (!o.parent.is_null()) world = worlds.at(o.parent.index) * world;
        worlds.emplace(h.index, world);
    });
    return worlds;
}

}  // namespace k
