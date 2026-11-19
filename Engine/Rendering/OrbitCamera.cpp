#include "Engine/Rendering/OrbitCamera.h"

#include <algorithm>
#include <cmath>

namespace k {

namespace {
constexpr double kPi = 3.14159265358979323846;
double rad(double d) { return d * (kPi / 180.0); }
}  // namespace

Vec3 OrbitCamera::eye() const {
    const double cy = std::cos(rad(pitch_deg)), sy = std::sin(rad(pitch_deg));
    return target + Vec3{cy * std::sin(rad(yaw_deg)), sy, cy * std::cos(rad(yaw_deg))} * distance;
}

Mat4 OrbitCamera::view() const { return Mat4::look_at(eye(), target, {0, 1, 0}); }

Mat4 OrbitCamera::projection(double aspect) const {
    return Mat4::perspective(fov_y_deg, aspect, near_plane, far_plane);
}

void OrbitCamera::orbit(double d_yaw_deg, double d_pitch_deg) {
    yaw_deg = std::fmod(yaw_deg + d_yaw_deg, 360.0);
    pitch_deg = std::clamp(pitch_deg + d_pitch_deg, -kMaxPitchDeg, kMaxPitchDeg);
}

void OrbitCamera::zoom(double factor) {
    if (!(factor > 0.0) || !std::isfinite(factor)) return;
    distance = std::clamp(distance * factor, kMinDistance, kMaxDistance);
}

void OrbitCamera::pan(double dx, double dy) {
    const Mat4 v = view();
    // Camera right/up in world space are the first two rows of the view rotation.
    const Vec3 right{v.at(0, 0), v.at(0, 1), v.at(0, 2)};
    const Vec3 up{v.at(1, 0), v.at(1, 1), v.at(1, 2)};
    const double visible_height = 2.0 * distance * std::tan(rad(fov_y_deg) * 0.5);
    // Dragging right moves the scene right, i.e. the target moves left; dragging down moves it up.
    target = target + right * (-dx * visible_height) + up * (dy * visible_height);
}

OrbitCamera::ScreenRay OrbitCamera::screen_ray(double px, double py, double width, double height) const {
    ScreenRay out;
    if (!(width > 0.0 && height > 0.0)) return out;
    const auto inv = (projection(width / height) * view()).inverse();
    if (!inv) return out;
    const double nx = 2.0 * px / width - 1.0;
    const double ny = 1.0 - 2.0 * py / height;
    // Unproject a point on the near plane (clip z = 0) and one on the far plane (z = 1).
    const Vec3 a = inv->transform_point_divided({nx, ny, 0.0});
    const Vec3 b = inv->transform_point_divided({nx, ny, 1.0});
    const Vec3 dir = normalized(b - a);
    if (length(dir) == 0.0) return out;
    out.ray = Ray{a, dir};
    out.valid = true;
    return out;
}

}  // namespace k
