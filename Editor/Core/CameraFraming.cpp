#include "Editor/Core/CameraFraming.h"

#include <algorithm>
#include <cmath>

#include "Engine/Scene/SceneTransforms.h"

namespace k {

void frame_scene(OrbitCamera& camera, const Scene& scene, double aspect) {
    if (!(aspect > 0.0) || !std::isfinite(aspect)) aspect = 1.0;
    constexpr double kPi = 3.14159265358979323846;
    const auto worlds = compute_world_matrices(scene);
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    bool any = false;
    scene.visit_preorder([&](ObjectHandle h, const SceneObject& o) {
        if (o.kind == ObjectKind::Empty) return;
        const Mat4& w = worlds.at(h.index);
        // Every primitive fits inside the unit cube [-0.5, 0.5]^3 in local space.
        for (int i = 0; i < 8; ++i) {
            const Vec3 corner{(i & 1) ? 0.5 : -0.5, (i & 2) ? 0.5 : -0.5, (i & 4) ? 0.5 : -0.5};
            const Vec3 p = w.transform_point(corner);
            for (std::size_t a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], p[a]);
                hi[a] = std::max(hi[a], p[a]);
            }
            any = true;
        }
    });
    if (!any) {
        camera.target = {0, 0, 0};
        camera.distance = 6.0;
        return;
    }
    camera.target = (lo + hi) * 0.5;
    const double radius = std::max(0.5 * length(hi - lo), 0.25);
    const double half_v = 0.5 * camera.fov_y_deg * kPi / 180.0;
    const double half_h = std::atan(std::tan(half_v) * aspect);
    // A sphere of `radius` fits when the distance is at least radius / sin(half_fov) for the narrower angle.
    const double half_fov = std::min(half_v, half_h);
    camera.distance = std::clamp(radius / std::sin(half_fov) * 1.1, OrbitCamera::kMinDistance, OrbitCamera::kMaxDistance);
}

}  // namespace k
