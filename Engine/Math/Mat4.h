// K Engine — Engine/Math/Mat4.h
//
// 4x4 double matrix. Conventions (ADR-0010, all covered by tests):
//   * Storage is ROW-MAJOR: element (r,c) is m[r*4 + c].
//   * COLUMN-VECTOR math: a point transforms as p' = M * p, and a product
//     A * B applies B first, then A.
//   * Right-handed, +Y up. Rotations are counter-clockwise about their axis
//     when looking from the positive axis toward the origin.
//   * Euler angles are in DEGREES and compose as R = Ry * Rx * Rz.
//   * Perspective maps view-space depth to clip z in [0,1] (Direct3D
//     convention); the camera looks down -Z.
//
// GPU upload: copy the 16 row-major floats unchanged and declare the HLSL
// matrix `row_major float4x4`, then use mul(M, v). No transposition anywhere.
#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "Engine/Math/Vec3.h"

namespace k {

struct Mat4 {
    std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // identity

    double& at(std::size_t r, std::size_t c) { return m[r * 4 + c]; }
    double at(std::size_t r, std::size_t c) const { return m[r * 4 + c]; }

    static Mat4 identity() { return Mat4{}; }
    static Mat4 translation(const Vec3& t);
    static Mat4 scaling(const Vec3& s);
    static Mat4 rotation_x_deg(double deg);
    static Mat4 rotation_y_deg(double deg);
    static Mat4 rotation_z_deg(double deg);
    // Ry * Rx * Rz, angles in degrees ({x, y, z}).
    static Mat4 from_euler_deg(const Vec3& euler_deg);
    // T * R * S  (scale first, then rotate, then translate).
    static Mat4 trs(const Vec3& position, const Vec3& euler_deg, const Vec3& scale);

    // Right-handed look-at; looks from `eye` toward `target`.
    static Mat4 look_at(const Vec3& eye, const Vec3& target, const Vec3& up);
    // Right-handed perspective, clip z in [0,1]. Requires 0 < near < far,
    // 0 < fov_y_deg < 180, aspect > 0 (otherwise returns identity).
    static Mat4 perspective(double fov_y_deg, double aspect, double near_plane, double far_plane);

    Mat4 transposed() const;
    // Full inverse, or nullopt if the matrix is singular (|det| too small).
    std::optional<Mat4> inverse() const;

    Vec3 transform_point(const Vec3& p) const;      // w = 1, no perspective divide
    Vec3 transform_direction(const Vec3& d) const;  // w = 0 (ignores translation)
    // w = 1 followed by the perspective divide. If the resulting w is ~0 the
    // result is {0,0,0} (callers that care check via transform_point_w).
    Vec3 transform_point_divided(const Vec3& p) const;
    double transform_point_w(const Vec3& p) const;

    friend bool operator==(const Mat4&, const Mat4&) = default;
};

Mat4 operator*(const Mat4& a, const Mat4& b);

// True if every element differs by at most `eps`.
bool nearly_equal(const Mat4& a, const Mat4& b, double eps = 1e-9);

}  // namespace k
