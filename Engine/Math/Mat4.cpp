#include "Engine/Math/Mat4.h"

#include <cmath>

namespace k {

namespace {
constexpr double kPi = 3.14159265358979323846;
double deg_to_rad(double d) { return d * (kPi / 180.0); }
}  // namespace

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 4; ++k) sum += a.at(i, k) * b.at(k, j);
            r.at(i, j) = sum;
        }
    }
    return r;
}

bool nearly_equal(const Mat4& a, const Mat4& b, double eps) {
    for (std::size_t i = 0; i < 16; ++i) {
        if (!(std::fabs(a.m[i] - b.m[i]) <= eps)) return false;  // also false for NaN
    }
    return true;
}

Mat4 Mat4::translation(const Vec3& t) {
    Mat4 r;
    r.at(0, 3) = t[0];
    r.at(1, 3) = t[1];
    r.at(2, 3) = t[2];
    return r;
}

Mat4 Mat4::scaling(const Vec3& s) {
    Mat4 r;
    r.at(0, 0) = s[0];
    r.at(1, 1) = s[1];
    r.at(2, 2) = s[2];
    return r;
}

Mat4 Mat4::rotation_x_deg(double deg) {
    const double c = std::cos(deg_to_rad(deg)), s = std::sin(deg_to_rad(deg));
    Mat4 r;
    r.at(1, 1) = c; r.at(1, 2) = -s;
    r.at(2, 1) = s; r.at(2, 2) = c;
    return r;
}

Mat4 Mat4::rotation_y_deg(double deg) {
    const double c = std::cos(deg_to_rad(deg)), s = std::sin(deg_to_rad(deg));
    Mat4 r;
    r.at(0, 0) = c;  r.at(0, 2) = s;
    r.at(2, 0) = -s; r.at(2, 2) = c;
    return r;
}

Mat4 Mat4::rotation_z_deg(double deg) {
    const double c = std::cos(deg_to_rad(deg)), s = std::sin(deg_to_rad(deg));
    Mat4 r;
    r.at(0, 0) = c; r.at(0, 1) = -s;
    r.at(1, 0) = s; r.at(1, 1) = c;
    return r;
}

Mat4 Mat4::from_euler_deg(const Vec3& e) {
    return rotation_y_deg(e[1]) * rotation_x_deg(e[0]) * rotation_z_deg(e[2]);
}

Mat4 Mat4::trs(const Vec3& position, const Vec3& euler_deg, const Vec3& scale) {
    return translation(position) * from_euler_deg(euler_deg) * scaling(scale);
}

Mat4 Mat4::look_at(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = normalized(target - eye);
    Vec3 s = normalized(cross(f, up));
    if (length(s) == 0.0) {
        // `up` parallel to the view direction: pick any perpendicular axis.
        s = normalized(cross(f, std::fabs(f[0]) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 0, 1}));
    }
    const Vec3 u = cross(s, f);
    Mat4 r;
    r.at(0, 0) = s[0];  r.at(0, 1) = s[1];  r.at(0, 2) = s[2];  r.at(0, 3) = -dot(s, eye);
    r.at(1, 0) = u[0];  r.at(1, 1) = u[1];  r.at(1, 2) = u[2];  r.at(1, 3) = -dot(u, eye);
    r.at(2, 0) = -f[0]; r.at(2, 1) = -f[1]; r.at(2, 2) = -f[2]; r.at(2, 3) = dot(f, eye);
    return r;
}

Mat4 Mat4::perspective(double fov_y_deg, double aspect, double n, double f) {
    if (!(fov_y_deg > 0.0 && fov_y_deg < 180.0 && aspect > 0.0 && n > 0.0 && f > n)) return Mat4{};
    const double t = 1.0 / std::tan(deg_to_rad(fov_y_deg) * 0.5);
    Mat4 r;
    r.m.fill(0.0);
    r.at(0, 0) = t / aspect;
    r.at(1, 1) = t;
    r.at(2, 2) = f / (n - f);
    r.at(2, 3) = n * f / (n - f);
    r.at(3, 2) = -1.0;
    return r;
}

Mat4 Mat4::transposed() const {
    Mat4 r;
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 4; ++j) r.at(i, j) = at(j, i);
    return r;
}

std::optional<Mat4> Mat4::inverse() const {
    // Cofactor expansion (closed form for 4x4).
    const auto& a = m;
    std::array<double, 16> inv{};
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    const double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    // Only exact/near-exact singularity is rejected. A relative threshold based
    // on the largest element would wrongly reject valid matrices such as a tiny
    // uniform scale placed far from the origin (translation dominates).
    if (!std::isfinite(det) || std::fabs(det) < 1e-300) return std::nullopt;
    Mat4 r;
    const double inv_det = 1.0 / det;
    for (std::size_t i = 0; i < 16; ++i) {
        r.m[i] = inv[i] * inv_det;
        if (!std::isfinite(r.m[i])) return std::nullopt;
    }
    return r;
}

Vec3 Mat4::transform_point(const Vec3& p) const {
    return {at(0, 0) * p[0] + at(0, 1) * p[1] + at(0, 2) * p[2] + at(0, 3),
            at(1, 0) * p[0] + at(1, 1) * p[1] + at(1, 2) * p[2] + at(1, 3),
            at(2, 0) * p[0] + at(2, 1) * p[1] + at(2, 2) * p[2] + at(2, 3)};
}

Vec3 Mat4::transform_direction(const Vec3& d) const {
    return {at(0, 0) * d[0] + at(0, 1) * d[1] + at(0, 2) * d[2],
            at(1, 0) * d[0] + at(1, 1) * d[1] + at(1, 2) * d[2],
            at(2, 0) * d[0] + at(2, 1) * d[1] + at(2, 2) * d[2]};
}

double Mat4::transform_point_w(const Vec3& p) const {
    return at(3, 0) * p[0] + at(3, 1) * p[1] + at(3, 2) * p[2] + at(3, 3);
}

Vec3 Mat4::transform_point_divided(const Vec3& p) const {
    const double w = transform_point_w(p);
    if (std::fabs(w) < 1e-300) return {0.0, 0.0, 0.0};
    const Vec3 r = transform_point(p);
    return {r[0] / w, r[1] / w, r[2] / w};
}

}  // namespace k
