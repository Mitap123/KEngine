#include <cmath>

#include "Engine/Math/Mat4.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {
bool near(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return std::fabs(a[0] - b[0]) <= eps && std::fabs(a[1] - b[1]) <= eps && std::fabs(a[2] - b[2]) <= eps;
}
}  // namespace

KTEST(math_vec3_basics) {
    KCHECK(near(Vec3{1, 2, 3} + Vec3{4, 5, 6}, Vec3{5, 7, 9}));
    KCHECK(near(Vec3{4, 5, 6} - Vec3{1, 2, 3}, Vec3{3, 3, 3}));
    KCHECK(near(Vec3{1, 2, 3} * 2.0, Vec3{2, 4, 6}));
    KCHECK_EQ(dot(Vec3{1, 2, 3}, Vec3{4, -5, 6}), 12.0);
    KCHECK(near(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}), Vec3{0, 0, 1}));  // right-handed
    KCHECK(near(cross(Vec3{0, 1, 0}, Vec3{0, 0, 1}), Vec3{1, 0, 0}));
    KCHECK_EQ(length(Vec3{3, 4, 0}), 5.0);
    KCHECK(near(normalized(Vec3{0, 0, 9}), Vec3{0, 0, 1}));
    KCHECK(near(normalized(Vec3{0, 0, 0}), Vec3{0, 0, 0}));  // zero stays zero, never NaN
}

KTEST(math_rotations_are_right_handed_counter_clockwise) {
    // Looking from +axis toward the origin, positive angle is counter-clockwise.
    KCHECK(near(Mat4::rotation_z_deg(90).transform_point({1, 0, 0}), {0, 1, 0}));
    KCHECK(near(Mat4::rotation_x_deg(90).transform_point({0, 1, 0}), {0, 0, 1}));
    KCHECK(near(Mat4::rotation_y_deg(90).transform_point({0, 0, 1}), {1, 0, 0}));
    KCHECK(near(Mat4::rotation_y_deg(180).transform_point({1, 2, 3}), {-1, 2, -3}));
    // Rotations preserve length and have det +1 (checked through the inverse = transpose property).
    const Mat4 r = Mat4::from_euler_deg({17, -63, 142});
    KCHECK(nearly_equal(r * r.transposed(), Mat4::identity(), 1e-12));
}

KTEST(math_euler_order_is_ry_rx_rz) {
    const Vec3 e{90, 90, 0};
    // Documented order: Z first, then X, then Y.  (0,1,0) -Rx-> (0,0,1) -Ry-> (1,0,0)
    KCHECK(near(Mat4::from_euler_deg(e).transform_point({0, 1, 0}), {1, 0, 0}));
    KCHECK(nearly_equal(Mat4::from_euler_deg(e), Mat4::rotation_y_deg(90) * Mat4::rotation_x_deg(90)));
    // The opposite order gives a different result, so the test would catch a swap.
    KCHECK(!near((Mat4::rotation_x_deg(90) * Mat4::rotation_y_deg(90)).transform_point({0, 1, 0}), {1, 0, 0}, 1e-6));
    KCHECK(nearly_equal(Mat4::from_euler_deg({0, 0, 0}), Mat4::identity()));
    // Z is applied first of all:
    KCHECK(near(Mat4::from_euler_deg({0, 0, 90}).transform_point({1, 0, 0}), {0, 1, 0}));
}

KTEST(math_trs_composes_scale_then_rotate_then_translate) {
    const Mat4 m = Mat4::trs({10, 0, 0}, {0, 90, 0}, {2, 1, 1});
    // (1,0,0): scale -> (2,0,0); Ry(90) -> (0,0,-2); translate -> (10,0,-2)
    KCHECK(near(m.transform_point({1, 0, 0}), {10, 0, -2}));
    // Directions ignore translation.
    KCHECK(near(m.transform_direction({1, 0, 0}), {0, 0, -2}));
    KCHECK(nearly_equal(Mat4::trs({0, 0, 0}, {0, 0, 0}, {1, 1, 1}), Mat4::identity()));
}

KTEST(math_multiplication_order_applies_right_operand_first) {
    const Mat4 t = Mat4::translation({1, 0, 0});
    const Mat4 s = Mat4::scaling({2, 2, 2});
    KCHECK(near((t * s).transform_point({1, 0, 0}), {3, 0, 0}));  // scale, then translate
    KCHECK(near((s * t).transform_point({1, 0, 0}), {4, 0, 0}));  // translate, then scale
    KCHECK(nearly_equal(Mat4::identity() * t, t));
}

KTEST(math_inverse_round_trips_and_rejects_singular) {
    const Mat4 cases[] = {
        Mat4::trs({3, -2, 5}, {20, 30, 40}, {1, 2, 3}),
        Mat4::trs({1000, 0, 0}, {0, 0, 0}, {1e-3, 1e-3, 1e-3}),   // tiny scale far from origin
        Mat4::perspective(60, 1.5, 0.1, 100),
        Mat4::look_at({3, 4, 5}, {0, 0, 0}, {0, 1, 0}),
    };
    for (const Mat4& c : cases) {
        auto inv = c.inverse();
        KCHECK(inv.has_value());
        if (inv) {
            KCHECK(nearly_equal(c * *inv, Mat4::identity(), 1e-9));
            KCHECK(nearly_equal(*inv * c, Mat4::identity(), 1e-9));
        }
    }
    KCHECK(!Mat4::scaling({0, 1, 1}).inverse().has_value());
    KCHECK(!Mat4::scaling({1, 0, 1}).inverse().has_value());
    Mat4 zero;
    zero.m.fill(0.0);
    KCHECK(!zero.inverse().has_value());
    Mat4 nan = Mat4::identity();
    nan.at(0, 0) = std::nan("");
    KCHECK(!nan.inverse().has_value());
}

KTEST(math_look_at_puts_eye_at_origin_and_target_on_minus_z) {
    const Vec3 eye{3, 4, 5};
    const Mat4 v = Mat4::look_at(eye, {0, 0, 0}, {0, 1, 0});
    KCHECK(near(v.transform_point(eye), {0, 0, 0}));
    const Vec3 t = v.transform_point({0, 0, 0});
    KCHECK(std::fabs(t[0]) < 1e-9 && std::fabs(t[1]) < 1e-9);
    KCHECK(t[2] < 0);                                   // in front of the camera = negative view-space Z
    KCHECK(std::fabs(t[2] + length(eye)) < 1e-9);       // at distance |eye|
    // Degenerate up (parallel to view direction) must still produce a finite, usable matrix.
    const Mat4 d = Mat4::look_at({0, 5, 0}, {0, 0, 0}, {0, 1, 0});
    for (double x : d.m) KCHECK(std::isfinite(x));
    KCHECK(near(d.transform_point({0, 5, 0}), {0, 0, 0}));
    KCHECK(d.transform_point({0, 0, 0})[2] < 0);
}

KTEST(math_perspective_maps_depth_to_zero_one_and_looks_down_minus_z) {
    const double n = 0.5, f = 50.0;
    const Mat4 p = Mat4::perspective(90, 2.0, n, f);
    KCHECK(std::fabs(p.transform_point_divided({0, 0, -n})[2] - 0.0) < 1e-12);   // near plane -> 0
    KCHECK(std::fabs(p.transform_point_divided({0, 0, -f})[2] - 1.0) < 1e-12);   // far plane  -> 1
    const double mid = p.transform_point_divided({0, 0, -5})[2];
    KCHECK(mid > 0.0 && mid < 1.0);
    // 90 degree vertical FOV: a point at y = -z is on the top edge (ndc y = 1); aspect 2 widens x.
    const Vec3 top = p.transform_point_divided({0, 3, -3});
    KCHECK(std::fabs(top[1] - 1.0) < 1e-12);
    const Vec3 right = p.transform_point_divided({6, 0, -3});
    KCHECK(std::fabs(right[0] - 1.0) < 1e-12);
    KCHECK(p.transform_point_w({0, 0, -4}) == 4.0);                              // w = -z
    // Invalid parameters fall back to identity instead of producing NaN/inf.
    KCHECK(nearly_equal(Mat4::perspective(60, 1, 0, 10), Mat4::identity()));
    KCHECK(nearly_equal(Mat4::perspective(60, 1, 5, 5), Mat4::identity()));
    KCHECK(nearly_equal(Mat4::perspective(0, 1, 1, 10), Mat4::identity()));
    KCHECK(nearly_equal(Mat4::perspective(60, 0, 1, 10), Mat4::identity()));
}

KTEST(math_transform_point_divided_guards_zero_w) {
    Mat4 m = Mat4::identity();
    m.at(3, 3) = 0.0;  // w = 0 for any point
    const Vec3 r = m.transform_point_divided({1, 2, 3});
    KCHECK(near(r, {0, 0, 0}));
}

KTEST(math_transposed_swaps_rows_and_columns) {
    Mat4 m = Mat4::identity();
    m.at(0, 3) = 7;
    KCHECK_EQ(m.transposed().at(3, 0), 7.0);
    KCHECK(nearly_equal(m.transposed().transposed(), m));
}
