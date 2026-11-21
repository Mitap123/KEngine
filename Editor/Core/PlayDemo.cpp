#include "Editor/Core/PlayDemo.h"

#include <cmath>
#include <vector>

namespace k {

void advance_play_demo(Scene& scene, double dt_seconds) {
    if (!(dt_seconds > 0.0) || !std::isfinite(dt_seconds)) return;
    std::vector<ObjectHandle> spinners;
    scene.visit_preorder([&](ObjectHandle h, const SceneObject& o) {
        if (o.kind == ObjectKind::Cube || o.kind == ObjectKind::Sphere) spinners.push_back(h);
    });
    for (ObjectHandle h : spinners) {
        Transform t = scene.get(h)->transform;
        t.rotation_euler_deg[1] = std::fmod(t.rotation_euler_deg[1] + kPlayDemoDegreesPerSecond * dt_seconds, 360.0);
        if (t.rotation_euler_deg[1] < 0.0) t.rotation_euler_deg[1] += 360.0;
        (void)scene.set_transform(h, t);
    }
}

}  // namespace k
