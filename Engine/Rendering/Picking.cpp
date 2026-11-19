#include "Engine/Rendering/Picking.h"

#include <cmath>
#include <limits>

#include "Engine/Scene/SceneTransforms.h"

namespace k {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// All functions return the smallest t >= 0 at which origin + t*dir hits the shape, or kInf.
// An origin inside a solid shape (cube, sphere) counts as a hit at t = 0.

double hit_cube(const Vec3& o, const Vec3& d) {
    double t_min = 0.0, t_max = kInf;
    for (int i = 0; i < 3; ++i) {
        const auto a = static_cast<std::size_t>(i);
        if (std::fabs(d[a]) < 1e-15) {
            if (o[a] < -0.5 || o[a] > 0.5) return kInf;  // parallel to the slab and outside it
            continue;
        }
        double t0 = (-0.5 - o[a]) / d[a], t1 = (0.5 - o[a]) / d[a];
        if (t0 > t1) std::swap(t0, t1);
        t_min = std::fmax(t_min, t0);
        t_max = std::fmin(t_max, t1);
        if (t_min > t_max) return kInf;
    }
    return t_min;  // t_min == 0 when the origin is inside the cube
}

double hit_sphere(const Vec3& o, const Vec3& d) {
    const double a = dot(d, d), b = 2.0 * dot(o, d), c = dot(o, o) - 0.25;
    if (a < 1e-30) return kInf;
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return kInf;
    const double sq = std::sqrt(disc);
    const double t0 = (-b - sq) / (2.0 * a), t1 = (-b + sq) / (2.0 * a);
    if (t0 >= 0.0) return t0;
    if (t1 >= 0.0) return 0.0;  // origin inside the solid: same convention as the cube
    return kInf;
}

double hit_plane(const Vec3& o, const Vec3& d) {
    if (std::fabs(d[1]) < 1e-15) return kInf;  // parallel to the plane
    const double t = -o[1] / d[1];
    if (t < 0.0) return kInf;
    const double x = o[0] + t * d[0], z = o[2] + t * d[2];
    return (std::fabs(x) <= 0.5 && std::fabs(z) <= 0.5) ? t : kInf;
}

}  // namespace

std::optional<PickHit> pick(const Scene& scene, const Ray& ray) {
    const auto worlds = compute_world_matrices(scene);
    std::optional<PickHit> best;
    scene.visit_preorder([&](ObjectHandle h, const SceneObject& obj) {
        if (obj.kind == ObjectKind::Empty) return;
        const auto inv = worlds.at(h.index).inverse();
        if (!inv) return;
        // Transform the ray into local space WITHOUT renormalizing the direction:
        // the parameter t is then identical in local and world space.
        const Vec3 lo = inv->transform_point(ray.origin);
        const Vec3 ld = inv->transform_direction(ray.direction);
        double t = kInf;
        switch (obj.kind) {
            case ObjectKind::Cube: t = hit_cube(lo, ld); break;
            case ObjectKind::Sphere: t = hit_sphere(lo, ld); break;
            case ObjectKind::Plane: t = hit_plane(lo, ld); break;
            case ObjectKind::Empty: break;
        }
        if (std::isfinite(t) && (!best || t < best->distance)) best = PickHit{h, t};
    });
    return best;
}

}  // namespace k
