#include "Engine/Rendering/Primitives.h"

#include <algorithm>
#include <cmath>

namespace k {

namespace {
constexpr double kPi = 3.14159265358979323846;

void add_quad(MeshData& m, const float p0[3], const float p1[3], const float p2[3], const float p3[3],
              const float n[3]) {
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    for (const float* p : {p0, p1, p2, p3}) {
        m.vertices.push_back({{p[0], p[1], p[2]}, {n[0], n[1], n[2]}});
    }
    // p0..p3 are given counter-clockwise as seen from outside.
    for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(base + i);
}
}  // namespace

MeshData make_cube() {
    MeshData m;
    m.vertices.reserve(24);
    m.indices.reserve(36);
    constexpr float h = 0.5f;
    // Each face: 4 corners CCW seen from outside + outward normal (flat shading => 4 own vertices per face).
    {   const float a[3]{ h, -h,  h}, b[3]{ h, -h, -h}, c[3]{ h,  h, -h}, d[3]{ h,  h,  h}, n[3]{ 1, 0, 0}; add_quad(m, a, b, c, d, n); }  // +X
    {   const float a[3]{-h, -h, -h}, b[3]{-h, -h,  h}, c[3]{-h,  h,  h}, d[3]{-h,  h, -h}, n[3]{-1, 0, 0}; add_quad(m, a, b, c, d, n); }  // -X
    {   const float a[3]{-h,  h,  h}, b[3]{ h,  h,  h}, c[3]{ h,  h, -h}, d[3]{-h,  h, -h}, n[3]{ 0, 1, 0}; add_quad(m, a, b, c, d, n); }  // +Y
    {   const float a[3]{-h, -h, -h}, b[3]{ h, -h, -h}, c[3]{ h, -h,  h}, d[3]{-h, -h,  h}, n[3]{ 0,-1, 0}; add_quad(m, a, b, c, d, n); }  // -Y
    {   const float a[3]{-h, -h,  h}, b[3]{ h, -h,  h}, c[3]{ h,  h,  h}, d[3]{-h,  h,  h}, n[3]{ 0, 0, 1}; add_quad(m, a, b, c, d, n); }  // +Z
    {   const float a[3]{ h, -h, -h}, b[3]{-h, -h, -h}, c[3]{-h,  h, -h}, d[3]{ h,  h, -h}, n[3]{ 0, 0,-1}; add_quad(m, a, b, c, d, n); }  // -Z
    return m;
}

MeshData make_plane() {
    MeshData m;
    constexpr float h = 0.5f;
    const float a[3]{-h, 0, h}, b[3]{h, 0, h}, c[3]{h, 0, -h}, d[3]{-h, 0, -h}, n[3]{0, 1, 0};
    add_quad(m, a, b, c, d, n);  // CCW seen from +Y
    return m;
}

MeshData make_sphere(int segments, int rings) {
    segments = std::max(segments, 3);
    rings = std::max(rings, 2);
    MeshData m;
    const double radius = 0.5;
    // Ring r goes from the north pole (r=0, y=+R) to the south pole (r=rings, y=-R).
    for (int r = 0; r <= rings; ++r) {
        const double phi = kPi * r / rings;            // 0 at +Y
        const double y = std::cos(phi), ring_radius = std::sin(phi);
        for (int s = 0; s <= segments; ++s) {          // s == segments duplicates s == 0 (seam)
            const double theta = 2.0 * kPi * s / segments;
            const double x = ring_radius * std::sin(theta), z = ring_radius * std::cos(theta);
            m.vertices.push_back({{static_cast<float>(x * radius), static_cast<float>(y * radius), static_cast<float>(z * radius)},
                                  {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}});
        }
    }
    const auto at = [&](int r, int s) { return static_cast<std::uint32_t>(r * (segments + 1) + s); };
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            // Quad corners: a=(r,s) b=(r,s+1) c=(r+1,s+1) d=(r+1,s). Poles collapse a side of the
            // quad to a point, so emit only the non-degenerate triangle there.
            if (r != 0) {
                m.indices.insert(m.indices.end(), {at(r, s), at(r + 1, s + 1), at(r, s + 1)});
            }
            if (r != rings - 1) {
                m.indices.insert(m.indices.end(), {at(r, s), at(r + 1, s), at(r + 1, s + 1)});
            }
        }
    }
    return m;
}

const MeshData* mesh_for(ObjectKind kind) {
    static const MeshData cube = make_cube();
    static const MeshData sphere = make_sphere();
    static const MeshData plane = make_plane();
    switch (kind) {
        case ObjectKind::Cube: return &cube;
        case ObjectKind::Sphere: return &sphere;
        case ObjectKind::Plane: return &plane;
        case ObjectKind::Empty: return nullptr;
    }
    return nullptr;
}

}  // namespace k
