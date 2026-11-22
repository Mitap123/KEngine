#include "Editor/Core/DemoScene.h"

namespace k {

void build_demo_scene(Scene& scene) {
    auto place = [&](ObjectHandle h, Vec3 p, Vec3 sc = {1, 1, 1}, Vec3 rot = {0, 0, 0}) {
        Transform t;
        t.position = p;
        t.scale = sc;
        t.rotation_euler_deg = rot;
        (void)scene.set_transform(h, t);
    };
    place(scene.create(ObjectKind::Plane, "Ground").value(), {0, -0.5, 0}, {8, 1, 8});
    place(scene.create(ObjectKind::Cube, "Cube").value(), {-1.5, 0, 0}, {1, 1, 1}, {0, 25, 0});
    place(scene.create(ObjectKind::Sphere, "Sphere").value(), {1.5, 0, 0});
    const ObjectHandle group = scene.create(ObjectKind::Empty, "Group").value();
    place(group, {0, 0, -2.5});
    place(scene.create(ObjectKind::Cube, "Child Cube", group).value(), {0, 0.25, 0}, {0.5, 1.5, 0.5});
}

}  // namespace k
