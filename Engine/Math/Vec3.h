// K Engine — Engine/Math/Vec3.h
// Small value-type vector helpers on std::array<double,3>. Conventions: ADR-0010.
#pragma once

#include <array>
#include <cmath>

namespace k {

using Vec3 = std::array<double, 3>;

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec3 operator*(const Vec3& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
inline Vec3 operator*(double s, const Vec3& a) { return a * s; }
inline Vec3 operator-(const Vec3& a) { return {-a[0], -a[1], -a[2]}; }

inline double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

inline double length(const Vec3& a) { return std::sqrt(dot(a, a)); }

// Unit vector, or the zero vector if `a` is (numerically) zero-length.
inline Vec3 normalized(const Vec3& a) {
    const double len = length(a);
    return len > 1e-300 ? a * (1.0 / len) : Vec3{0.0, 0.0, 0.0};
}

}  // namespace k
