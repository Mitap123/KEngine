// K Engine — Engine/Rendering/OrbitCamera.h
//
// Editor orbit camera: always looks at `target` from `distance`. Pure math,
// no graphics API, no Qt. Conventions: ADR-0010 (right-handed, +Y up).
//
// Eye position: target + distance * (cos(pitch) sin(yaw), sin(pitch), cos(pitch) cos(yaw)).
// yaw = 0, pitch = 0 puts the camera on +Z looking toward -Z; positive pitch
// raises the camera above the target.
#pragma once

#include "Engine/Math/Mat4.h"

namespace k {

struct Ray {
    Vec3 origin{0, 0, 0};
    Vec3 direction{0, 0, -1};  // unit length when produced by OrbitCamera::screen_ray
};

struct OrbitCamera {
    Vec3 target{0, 0, 0};
    double distance = 6.0;
    double yaw_deg = 35.0;
    double pitch_deg = 25.0;
    double fov_y_deg = 60.0;
    double near_plane = 0.05;
    double far_plane = 1000.0;

    static constexpr double kMinDistance = 0.1;
    static constexpr double kMaxDistance = 10000.0;
    static constexpr double kMaxPitchDeg = 89.0;

    Vec3 eye() const;
    Mat4 view() const;
    Mat4 projection(double aspect) const;

    // Rotate by the given angle deltas (degrees); pitch is clamped to +-89 so the camera never flips.
    void orbit(double d_yaw_deg, double d_pitch_deg);
    // Multiply the distance by `factor` (>0), clamped to [kMinDistance, kMaxDistance].
    void zoom(double factor);
    // Move the target in the view plane. dx/dy are fractions of the visible
    // viewport height at the target distance (0.5 = half the viewport).
    void pan(double dx, double dy);

    // World-space ray through pixel (px, py) of a width x height viewport
    // (origin top-left, y down). Returns nullopt-equivalent zero ray if the
    // viewport is empty or the matrices are not invertible: `valid` false.
    struct ScreenRay {
        Ray ray;
        bool valid = false;
    };
    ScreenRay screen_ray(double px, double py, double width, double height) const;
};

}  // namespace k
