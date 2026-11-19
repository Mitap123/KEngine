#include <cmath>

#include "Engine/Rendering/OrbitCamera.h"
#include "Engine/Rendering/Picking.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {

bool near3(const Vec3& a, const Vec3& b, double eps = 1e-9) { return length(a - b) <= eps; }

ObjectHandle add(Scene& s, ObjectKind kind, const char* name, ObjectHandle parent = kNullObjectHandle) {
    auto r = s.create(kind, name, parent);
    return r.ok() ? r.value() : kNullObjectHandle;
}
void set(Scene& s, ObjectHandle h, Vec3 p, Vec3 r = {0, 0, 0}, Vec3 sc = {1, 1, 1}) {
    Transform t;
    t.position = p;
    t.rotation_euler_deg = r;
    t.scale = sc;
    KCHECK(s.set_transform(h, t).ok());
}
// Distance from `p` to the infinite line through the ray.
double distance_to_line(const Ray& r, const Vec3& p) { return length(cross(p - r.origin, normalized(r.direction))); }
Ray ray(Vec3 o, Vec3 d) { return Ray{o, d}; }

}  // namespace

// ------------------------------------------------------------------ camera ---

KTEST(camera_eye_follows_documented_formula) {
    OrbitCamera c;
    c.target = {1, 2, 3};
    c.distance = 10;
    c.yaw_deg = 0;
    c.pitch_deg = 0;
    KCHECK(near3(c.eye(), {1, 2, 13}));      // on +Z looking toward -Z
    c.yaw_deg = 90;
    KCHECK(near3(c.eye(), {11, 2, 3}));      // +X
    c.yaw_deg = 0;
    c.pitch_deg = 90 - 1e-9;
    KCHECK(c.eye()[1] > 2 + 9.99);           // straight up: above the target
    c.pitch_deg = -30;
    KCHECK(c.eye()[1] < 2);                  // negative pitch: below
    // Distance from eye to target is always `distance`.
    c.yaw_deg = 123; c.pitch_deg = 47;
    KCHECK(std::fabs(length(c.eye() - c.target) - 10.0) < 1e-9);
}

KTEST(camera_view_looks_at_target) {
    OrbitCamera c;
    c.target = {4, -1, 2};
    const Vec3 t = c.view().transform_point(c.target);
    KCHECK(std::fabs(t[0]) < 1e-9 && std::fabs(t[1]) < 1e-9);
    KCHECK(std::fabs(t[2] + c.distance) < 1e-9);
    KCHECK(near3(c.view().transform_point(c.eye()), {0, 0, 0}));
}

KTEST(camera_orbit_clamps_pitch_and_never_flips) {
    OrbitCamera c;
    c.orbit(0, 1000);
    KCHECK_EQ(c.pitch_deg, 89.0);
    c.orbit(0, -5000);
    KCHECK_EQ(c.pitch_deg, -89.0);
    c.yaw_deg = 0;
    c.orbit(370, 0);
    KCHECK(std::fabs(c.yaw_deg - 10.0) < 1e-9);          // wraps instead of growing unbounded
    for (double x : c.view().m) KCHECK(std::isfinite(x)); // still a valid view at the clamp
    c.pitch_deg = 89;
    for (double x : c.view().m) KCHECK(std::isfinite(x));
}

KTEST(camera_zoom_scales_and_clamps_and_ignores_bad_input) {
    OrbitCamera c;
    c.distance = 10;
    c.zoom(0.5);
    KCHECK_EQ(c.distance, 5.0);
    c.zoom(1e-9);
    KCHECK_EQ(c.distance, OrbitCamera::kMinDistance);
    c.zoom(1e30);
    KCHECK_EQ(c.distance, OrbitCamera::kMaxDistance);
    c.distance = 7;
    c.zoom(0.0); c.zoom(-2.0); c.zoom(std::nan("")); c.zoom(INFINITY);
    KCHECK_EQ(c.distance, 7.0);                           // invalid factors are ignored
}

KTEST(camera_pan_moves_target_in_view_plane_with_expected_signs_and_scale) {
    OrbitCamera c;
    c.yaw_deg = 0; c.pitch_deg = 0; c.distance = 10;       // looking down -Z, right = +X, up = +Y
    const double visible_height = 2.0 * 10.0 * std::tan(30.0 * 3.14159265358979323846 / 180.0);
    c.pan(0.5, 0.0);                                        // drag right by half the viewport
    KCHECK(near3(c.target, {-0.5 * visible_height, 0, 0}, 1e-9));   // scene follows the cursor: target moves left
    c.target = {0, 0, 0};
    c.pan(0.0, 0.25);                                       // drag down
    KCHECK(near3(c.target, {0, 0.25 * visible_height, 0}, 1e-9));
    // Panning never changes the distance and moves perpendicular to the view direction.
    OrbitCamera d;
    d.yaw_deg = 40; d.pitch_deg = 30;
    const Vec3 forward = normalized(d.target - d.eye());
    const Vec3 before = d.target;
    d.pan(0.3, -0.2);
    KCHECK(std::fabs(dot(d.target - before, forward)) < 1e-9);
    KCHECK(std::fabs(length(d.eye() - d.target) - d.distance) < 1e-9);
}

KTEST(camera_screen_ray_through_center_hits_target) {
    OrbitCamera c;
    c.target = {1, 2, 3};
    c.yaw_deg = 70; c.pitch_deg = -20; c.distance = 9;
    auto r = c.screen_ray(400, 300, 800, 600);
    KCHECK(r.valid);
    KCHECK(std::fabs(length(r.ray.direction) - 1.0) < 1e-9);
    KCHECK(distance_to_line(r.ray, c.target) < 1e-9);
    // Ray points from the camera toward the target.
    KCHECK(dot(r.ray.direction, c.target - c.eye()) > 0);
}

KTEST(camera_screen_ray_round_trips_projected_points) {
    OrbitCamera c;
    c.yaw_deg = 20; c.pitch_deg = 35; c.distance = 8;
    const double w = 1000, h = 500;
    const Mat4 vp = c.projection(w / h) * c.view();
    for (const Vec3& p : {Vec3{0, 0, 0}, Vec3{1, 0.5, -1}, Vec3{-2, 1, 2}, Vec3{0.3, -0.4, 0.2}}) {
        const Vec3 ndc = vp.transform_point_divided(p);
        const double px = (ndc[0] * 0.5 + 0.5) * w;
        const double py = (1.0 - (ndc[1] * 0.5 + 0.5)) * h;     // pixel y grows downward
        auto r = c.screen_ray(px, py, w, h);
        KCHECK(r.valid);
        KCHECK(distance_to_line(r.ray, p) < 1e-6);
    }
}

KTEST(camera_screen_ray_rejects_empty_viewports) {
    OrbitCamera c;
    KCHECK(!c.screen_ray(0, 0, 0, 100).valid);
    KCHECK(!c.screen_ray(0, 0, 100, 0).valid);
    KCHECK(!c.screen_ray(0, 0, -5, 100).valid);
    KCHECK(!c.screen_ray(0, 0, std::nan(""), 100).valid);
}

// ----------------------------------------------------------------- picking ---

KTEST(picking_cube_distance_scale_translation_rotation) {
    Scene s;
    auto cube = add(s, ObjectKind::Cube, "c");
    auto hit = pick(s, ray({0, 0, 5}, {0, 0, -1}));
    KCHECK(hit.has_value());
    if (hit) { KCHECK(hit->object == cube); KCHECK(std::fabs(hit->distance - 4.5) < 1e-9); }

    set(s, cube, {3, 0, 0});                                          // translated
    KCHECK(!pick(s, ray({0, 0, 5}, {0, 0, -1})).has_value());
    hit = pick(s, ray({3, 0, 5}, {0, 0, -1}));
    KCHECK(hit && std::fabs(hit->distance - 4.5) < 1e-9);

    set(s, cube, {0, 0, 0}, {0, 0, 0}, {2, 2, 2});                    // uniform scale: half-extent 1
    hit = pick(s, ray({0, 0, 5}, {0, 0, -1}));
    KCHECK(hit && std::fabs(hit->distance - 4.0) < 1e-9);

    set(s, cube, {0, 0, 0}, {0, 0, 0}, {4, 1, 1});                    // non-uniform: x half-extent 2
    hit = pick(s, ray({10, 0, 0}, {-1, 0, 0}));
    KCHECK(hit && std::fabs(hit->distance - 8.0) < 1e-9);
    KCHECK(pick(s, ray({0, 0, 5}, {0, 0, -1})).has_value());            // still hit along z (half-extent 0.5)
    KCHECK(!pick(s, ray({0, 0.6, 5}, {0, 0, -1})).has_value());        // y half-extent is only 0.5

    set(s, cube, {0, 0, 0}, {0, 45, 0});                              // rotated: corner points along +X
    hit = pick(s, ray({5, 0, 0}, {-1, 0, 0}));
    KCHECK(hit && std::fabs(hit->distance - (5.0 - std::sqrt(0.5))) < 1e-9);
    // The same ray misses the un-rotated cube's face region only when offset past the rotated silhouette.
    KCHECK(!pick(s, ray({5, 0, 0.75}, {-1, 0, 0})).has_value());
}

KTEST(picking_sphere_hit_miss_and_inside) {
    Scene s;
    auto sp = add(s, ObjectKind::Sphere, "s");
    auto hit = pick(s, ray({0, 0, 5}, {0, 0, -1}));
    KCHECK(hit && hit->object == sp && std::fabs(hit->distance - 4.5) < 1e-9);
    KCHECK(!pick(s, ray({0.6, 0, 5}, {0, 0, -1})).has_value());           // passes outside the radius
    hit = pick(s, ray({0.4, 0, 5}, {0, 0, -1}));                          // inside the silhouette
    KCHECK(hit && std::fabs(hit->distance - (5.0 - std::sqrt(0.25 - 0.16))) < 1e-9);
    hit = pick(s, ray({0, 0, 0}, {1, 0, 0}));                             // origin inside the solid
    KCHECK(hit && hit->distance == 0.0);
    set(s, sp, {0, 0, 0}, {0, 0, 0}, {3, 1, 1});                          // ellipsoid
    hit = pick(s, ray({5, 0, 0}, {-1, 0, 0}));
    KCHECK(hit && std::fabs(hit->distance - 3.5) < 1e-9);
}

KTEST(picking_plane_is_double_sided_bounded_and_ignores_parallel_rays) {
    Scene s;
    auto pl = add(s, ObjectKind::Plane, "p");
    auto from_above = pick(s, ray({0, 5, 0}, {0, -1, 0}));
    KCHECK(from_above && from_above->object == pl && std::fabs(from_above->distance - 5.0) < 1e-9);
    auto from_below = pick(s, ray({0, -5, 0}, {0, 1, 0}));
    KCHECK(from_below && std::fabs(from_below->distance - 5.0) < 1e-9);
    KCHECK(!pick(s, ray({0.6, 5, 0}, {0, -1, 0})).has_value());           // outside the 1x1 quad
    KCHECK(!pick(s, ray({0, 5, 0.51}, {0, -1, 0})).has_value());
    KCHECK(pick(s, ray({0.5, 5, -0.5}, {0, -1, 0})).has_value());         // edge/corner is inclusive
    KCHECK(!pick(s, ray({-5, 0, 0}, {1, 0, 0})).has_value());             // parallel, lying in the plane: no hit
    KCHECK(!pick(s, ray({0, -5, 0}, {0, -1, 0})).has_value());            // pointing away
    set(s, pl, {0, 0, 0}, {0, 0, 0}, {4, 1, 1});                          // scaled wide
    KCHECK(pick(s, ray({1.9, 5, 0}, {0, -1, 0})).has_value());
    KCHECK(!pick(s, ray({2.1, 5, 0}, {0, -1, 0})).has_value());
}

KTEST(picking_respects_parent_transforms) {
    Scene s;
    auto parent = add(s, ObjectKind::Empty, "p");
    auto child = add(s, ObjectKind::Cube, "c", parent);
    set(s, parent, {10, 0, 0});
    auto hit = pick(s, ray({10, 0, 5}, {0, 0, -1}));
    KCHECK(hit && hit->object == child && std::fabs(hit->distance - 4.5) < 1e-9);
    KCHECK(!pick(s, ray({0, 0, 5}, {0, 0, -1})).has_value());             // child is NOT at its local origin in world space

    set(s, parent, {10, 0, 0}, {0, 0, 0}, {2, 2, 2});                     // parent scale scales the child
    set(s, child, {1, 0, 0});                                             // world x = 12, half-extent 1
    hit = pick(s, ray({12, 0, 5}, {0, 0, -1}));
    KCHECK(hit && std::fabs(hit->distance - 4.0) < 1e-9);
    set(s, parent, {0, 0, 0}, {0, 90, 0});                                // rotating the parent swings the child: (1,0,0) -> (0,0,-1)
    set(s, child, {1, 0, 0});
    hit = pick(s, ray({0, 0, 5}, {0, 0, -1}));
    KCHECK(hit.has_value());                                              // child now sits at z = -1
    if (hit) KCHECK(std::fabs(hit->distance - 5.5) < 1e-9);
}

KTEST(picking_returns_nearest_and_ignores_empties_stale_and_degenerate_objects) {
    Scene s;
    auto empty = add(s, ObjectKind::Empty, "e");
    auto far_cube = add(s, ObjectKind::Cube, "far");
    auto near_sphere = add(s, ObjectKind::Sphere, "near");
    set(s, far_cube, {0, 0, -3});
    set(s, near_sphere, {0, 0, 1});
    auto hit = pick(s, ray({0, 0, 5}, {0, 0, -1}));
    KCHECK(hit && hit->object == near_sphere && std::fabs(hit->distance - 3.5) < 1e-9);
    hit = pick(s, ray({0, 0, -10}, {0, 0, 1}));                           // from the other side the cube is nearer
    KCHECK(hit && hit->object == far_cube);
    (void)empty;                                                          // Empty at the origin never hits

    auto zero = add(s, ObjectKind::Cube, "zero");
    set(s, zero, {0, 0, 4}, {0, 0, 0}, {0, 1, 1});                        // singular world matrix: skipped, no crash
    hit = pick(s, ray({0, 0, 8}, {0, 0, -1}));
    KCHECK(hit && hit->object == near_sphere);

    KCHECK(s.destroy(near_sphere).ok());
    hit = pick(s, ray({0, 0, 5}, {0, 0, -1}));
    KCHECK(hit && hit->object == far_cube);                               // destroyed objects are not pickable
}

KTEST(picking_direction_length_and_degenerate_rays) {
    Scene s;
    add(s, ObjectKind::Cube, "c");
    auto hit = pick(s, ray({0, 0, 5}, {0, 0, -2}));                       // non-unit direction: distance is in direction units
    KCHECK(hit && std::fabs(hit->distance - 2.25) < 1e-9);
    KCHECK(!pick(s, ray({0, 0, 5}, {0, 0, 1})).has_value());              // pointing away
    KCHECK(!pick(s, ray({0, 0, -5}, {0, 0, -1})).has_value());            // object is behind the origin
    KCHECK(!pick(s, ray({0, 0, 5}, {0, 0, 0})).has_value());              // zero direction: no hit, no NaN
    KCHECK(!pick(Scene{}, ray({0, 0, 5}, {0, 0, -1})).has_value());       // empty scene
}

KTEST(picking_through_the_camera_selects_the_object_under_the_cursor) {
    Scene s;
    auto left = add(s, ObjectKind::Cube, "left");
    auto right = add(s, ObjectKind::Sphere, "right");
    set(s, left, {-1.5, 0, 0});
    set(s, right, {1.5, 0, 0});
    OrbitCamera c;
    c.yaw_deg = 0; c.pitch_deg = 0; c.distance = 8;
    const double w = 800, h = 600;
    const Mat4 vp = c.projection(w / h) * c.view();
    auto pixel_of = [&](const Vec3& p) {
        const Vec3 n = vp.transform_point_divided(p);
        return std::pair{(n[0] * 0.5 + 0.5) * w, (1.0 - (n[1] * 0.5 + 0.5)) * h};
    };
    auto [lx, ly] = pixel_of({-1.5, 0, 0});
    auto [rx, ry] = pixel_of({1.5, 0, 0});
    auto hl = pick(s, c.screen_ray(lx, ly, w, h).ray);
    auto hr = pick(s, c.screen_ray(rx, ry, w, h).ray);
    KCHECK(hl && hl->object == left);
    KCHECK(hr && hr->object == right);
    KCHECK(!pick(s, c.screen_ray(w * 0.5, 5, w, h).ray).has_value());      // top of the screen: empty space
}
