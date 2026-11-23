#include "Engine/Scene/SceneTransforms.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace k {

Mat4 local_matrix(const Transform& t) {
    return Mat4::trs(t.position, t.rotation_euler_deg, t.scale);
}

std::optional<Transform> decompose_trs(const Mat4& m) {
    constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

    double biggest = 1.0;
    for (double v : m.m) {
        if (!std::isfinite(v)) return std::nullopt;
        biggest = std::max(biggest, std::fabs(v));
    }
    const double eps = 1e-7 * biggest;
    // Affine only: bottom row (0,0,0,1).
    if (std::fabs(m.at(3, 0)) > eps || std::fabs(m.at(3, 1)) > eps || std::fabs(m.at(3, 2)) > eps ||
        std::fabs(m.at(3, 3) - 1.0) > eps)
        return std::nullopt;

    Vec3 col[3];
    for (std::size_t c = 0; c < 3; ++c) col[c] = {m.at(0, c), m.at(1, c), m.at(2, c)};
    double scale[3];
    for (std::size_t c = 0; c < 3; ++c) {
        scale[c] = length(col[c]);
        if (!(scale[c] > 1e-12)) return std::nullopt;  // zero-length axis (also catches NaN)
        col[c] = col[c] * (1.0 / scale[c]);
    }
    if (dot(col[0], cross(col[1], col[2])) < 0.0) {  // reflection: carry it on the X scale
        scale[0] = -scale[0];
        col[0] = -col[0];
    }
    // R(row, col) = col[col][row].
    const double r02 = col[2][0], r22 = col[2][2], r12 = col[2][1];
    const double r10 = col[0][1], r11 = col[1][1];
    const double r00 = col[0][0], r01 = col[1][0];

    const double cx = std::hypot(r10, r11);
    const double sx = std::clamp(-r12, -1.0, 1.0);
    double x_deg = std::atan2(sx, cx) * kRadToDeg;
    double y_deg = 0.0, z_deg = 0.0;
    if (cx > 1e-9) {
        y_deg = std::atan2(r02, r22) * kRadToDeg;
        z_deg = std::atan2(r10, r11) * kRadToDeg;
    } else {  // gimbal lock: only y -/+ z is determined; put it all on Y
        y_deg = std::atan2((sx < 0.0 ? -1.0 : 1.0) * r01, r00) * kRadToDeg;
    }

    Transform t;
    t.position = {m.at(0, 3), m.at(1, 3), m.at(2, 3)};
    t.rotation_euler_deg = {x_deg, y_deg, z_deg};
    t.scale = {scale[0], scale[1], scale[2]};
    if (!is_finite(t)) return std::nullopt;
    // Prove it: shear or a non-orthogonal basis does not survive recomposition.
    if (!nearly_equal(local_matrix(t), m, eps)) return std::nullopt;
    return t;
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
