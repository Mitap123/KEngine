#include <cmath>
#include <map>
#include <tuple>

#include "Engine/Rendering/DrawList.h"
#include "Engine/Rendering/Primitives.h"
#include "Engine/Scene/SceneTransforms.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {

Vec3 pos(const MeshVertex& v) { return {v.position[0], v.position[1], v.position[2]}; }
Vec3 nrm(const MeshVertex& v) { return {v.normal[0], v.normal[1], v.normal[2]}; }

struct Tri { Vec3 a, b, c; Vec3 na, nb, nc; };
std::vector<Tri> triangles(const MeshData& m) {
    std::vector<Tri> out;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto& va = m.vertices[m.indices[i]];
        const auto& vb = m.vertices[m.indices[i + 1]];
        const auto& vc = m.vertices[m.indices[i + 2]];
        out.push_back({pos(va), pos(vb), pos(vc), nrm(va), nrm(vb), nrm(vc)});
    }
    return out;
}

// Common structural invariants of a mesh.
void check_mesh_basics(const MeshData& m, const char* name) {
    KCHECK(!m.vertices.empty());
    KCHECK_EQ(m.indices.size() % 3, static_cast<std::size_t>(0));
    for (auto idx : m.indices) {
        if (idx >= m.vertices.size()) { ktest::fail(__FILE__, __LINE__, std::string(name) + ": index out of range"); return; }
    }
    for (const auto& v : m.vertices) {
        for (float f : v.position) KCHECK(std::isfinite(f));
        KCHECK(std::fabs(length(nrm(v)) - 1.0) < 1e-6);   // unit normals
    }
    for (const Tri& t : triangles(m)) {
        const Vec3 geo = cross(t.b - t.a, t.c - t.a);
        KCHECK(length(geo) * 0.5 > 1e-9);                  // no degenerate triangles
    }
}

// Every directed edge must be matched by exactly one opposite edge: proves a
// closed, consistently oriented surface (no cracks, no flipped triangles).
void check_closed_and_consistently_wound(const MeshData& m, const char* name) {
    using Key = std::tuple<long, long, long>;
    auto q = [](const Vec3& p) { return Key{std::lround(p[0] * 1e4), std::lround(p[1] * 1e4), std::lround(p[2] * 1e4)}; };
    std::map<std::pair<Key, Key>, int> edges;
    for (const Tri& t : triangles(m)) {
        const Key ka = q(t.a), kb = q(t.b), kc = q(t.c);
        ++edges[{ka, kb}]; ++edges[{kb, kc}]; ++edges[{kc, ka}];
    }
    for (const auto& [e, count] : edges) {
        const int rev = edges.count({e.second, e.first}) ? edges.at({e.second, e.first}) : 0;
        if (count != 1 || rev != 1) {
            ktest::fail(__FILE__, __LINE__, std::string(name) + ": edge not matched by exactly one opposite edge");
            return;
        }
    }
}

// Front faces wind counter-clockwise seen from outside: geometric normal agrees with the vertex normal.
void check_winding_matches_normals(const MeshData& m, const char* name) {
    for (const Tri& t : triangles(m)) {
        const Vec3 geo = normalized(cross(t.b - t.a, t.c - t.a));
        for (const Vec3& n : {t.na, t.nb, t.nc}) {
            if (dot(geo, n) <= 0.0) { ktest::fail(__FILE__, __LINE__, std::string(name) + ": triangle winds against its normal"); return; }
        }
    }
}

double total_area(const MeshData& m) {
    double a = 0;
    for (const Tri& t : triangles(m)) a += 0.5 * length(cross(t.b - t.a, t.c - t.a));
    return a;
}

}  // namespace

KTEST(primitives_cube_geometry) {
    const MeshData m = make_cube();
    KCHECK_EQ(m.vertices.size(), static_cast<std::size_t>(24));
    KCHECK_EQ(m.indices.size(), static_cast<std::size_t>(36));
    check_mesh_basics(m, "cube");
    check_winding_matches_normals(m, "cube");
    check_closed_and_consistently_wound(m, "cube");
    KCHECK(std::fabs(total_area(m) - 6.0) < 1e-6);
    for (const auto& v : m.vertices) {
        for (float f : v.position) KCHECK(std::fabs(std::fabs(f) - 0.5f) < 1e-6f);  // every coordinate is +-0.5
        // Face normal is axis-aligned and points the same way as the face's position.
        const Vec3 n = nrm(v), p = pos(v);
        KCHECK(dot(n, p) > 0.49);
    }
}

KTEST(primitives_sphere_geometry) {
    const int segments = 24, rings = 16;
    const MeshData m = make_sphere(segments, rings);
    KCHECK_EQ(m.vertices.size(), static_cast<std::size_t>((rings + 1) * (segments + 1)));
    KCHECK_EQ(m.indices.size(), static_cast<std::size_t>(3 * 2 * segments * (rings - 1)));  // poles are fans
    check_mesh_basics(m, "sphere");
    check_winding_matches_normals(m, "sphere");
    check_closed_and_consistently_wound(m, "sphere");
    for (const auto& v : m.vertices) {
        KCHECK(std::fabs(length(pos(v)) - 0.5) < 1e-6);                      // radius 0.5
        KCHECK(length(pos(v) * 2.0 - nrm(v)) < 1e-6);                        // normal = outward radial
    }
    for (const Tri& t : triangles(m)) {
        const Vec3 centroid = (t.a + t.b + t.c) * (1.0 / 3.0);
        KCHECK(dot(cross(t.b - t.a, t.c - t.a), centroid) > 0.0);           // CCW from outside
    }
    const double pi = 3.14159265358979323846;
    KCHECK(std::fabs(total_area(m) - pi) / pi < 0.02);                        // 4*pi*0.5^2, tessellation error < 2%
}

KTEST(primitives_sphere_clamps_degenerate_parameters) {
    for (auto [s, r] : {std::pair{0, 0}, std::pair{1, 1}, std::pair{-5, 100}, std::pair{3, 2}}) {
        const MeshData m = make_sphere(s, r);
        check_mesh_basics(m, "clamped sphere");
        check_closed_and_consistently_wound(m, "clamped sphere");
    }
    KCHECK_EQ(make_sphere(0, 0).vertices.size(), static_cast<std::size_t>((2 + 1) * (3 + 1)));  // clamped to 3 x 2
}

KTEST(primitives_plane_geometry) {
    const MeshData m = make_plane();
    KCHECK_EQ(m.vertices.size(), static_cast<std::size_t>(4));
    KCHECK_EQ(m.indices.size(), static_cast<std::size_t>(6));
    check_mesh_basics(m, "plane");
    check_winding_matches_normals(m, "plane");
    KCHECK(std::fabs(total_area(m) - 1.0) < 1e-9);
    for (const auto& v : m.vertices) {
        KCHECK_EQ(v.position[1], 0.0f);
        KCHECK(nrm(v) == (Vec3{0, 1, 0}));
    }
}

KTEST(primitives_mesh_for_returns_stable_geometry_per_kind) {
    KCHECK(mesh_for(ObjectKind::Empty) == nullptr);
    for (ObjectKind k : {ObjectKind::Cube, ObjectKind::Sphere, ObjectKind::Plane}) {
        const MeshData* a = mesh_for(k);
        KCHECK(a != nullptr);
        KCHECK(a == mesh_for(k));  // built once
        if (a) KCHECK(!a->vertices.empty());
    }
    KCHECK(mesh_for(ObjectKind::Cube) != mesh_for(ObjectKind::Sphere));
}

// ----------------------------------------------------------- world matrices ---

namespace {
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
bool near3(const Vec3& a, const Vec3& b, double eps = 1e-9) { return length(a - b) <= eps; }
}  // namespace

KTEST(scene_transforms_local_matrix_matches_trs) {
    Transform t;
    t.position = {1, 2, 3};
    t.rotation_euler_deg = {10, 20, 30};
    t.scale = {2, 3, 4};
    KCHECK(nearly_equal(local_matrix(t), Mat4::trs({1, 2, 3}, {10, 20, 30}, {2, 3, 4})));
    KCHECK(nearly_equal(local_matrix(Transform{}), Mat4::identity()));
}

KTEST(scene_transforms_world_is_parent_times_local) {
    Scene s;
    auto parent = add(s, ObjectKind::Empty, "p");
    auto child = add(s, ObjectKind::Cube, "c", parent);
    set(s, parent, {10, 0, 0}, {0, 90, 0}, {2, 2, 2});
    set(s, child, {1, 0, 0});
    // child local (1,0,0): parent scale -> (2,0,0); Ry(90) -> (0,0,-2); translate -> (10,0,-2)
    auto w = world_matrix(s, child);
    KCHECK(w.has_value());
    if (w) KCHECK(near3(w->transform_point({0, 0, 0}), {10, 0, -2}));
    // A root's world matrix equals its local matrix.
    auto wp = world_matrix(s, parent);
    KCHECK(wp.has_value() && nearly_equal(*wp, local_matrix(s.get(parent)->transform)));
    // Invalid / stale handles give nullopt, never UB.
    KCHECK(!world_matrix(s, kNullObjectHandle).has_value());
    KCHECK(s.destroy(child).ok());
    KCHECK(!world_matrix(s, child).has_value());
}

KTEST(scene_transforms_bulk_computation_agrees_with_per_object_query) {
    Scene s;
    auto a = add(s, ObjectKind::Empty, "a");
    auto b = add(s, ObjectKind::Cube, "b", a);
    auto c = add(s, ObjectKind::Sphere, "c", b);
    auto d = add(s, ObjectKind::Plane, "d", a);
    auto e = add(s, ObjectKind::Cube, "e");
    set(s, a, {1, 2, 3}, {10, 20, 30}, {1, 2, 1});
    set(s, b, {-4, 0.5, 2}, {0, 45, 0}, {1, 1, 3});
    set(s, c, {0, 1, 0}, {90, 0, 0});
    set(s, d, {5, 5, 5});
    set(s, e, {7, 0, 0});
    const auto worlds = compute_world_matrices(s);
    KCHECK_EQ(worlds.size(), s.object_count());
    for (ObjectHandle h : {a, b, c, d, e}) {
        auto single = world_matrix(s, h);
        KCHECK(single.has_value());
        if (single) KCHECK(nearly_equal(worlds.at(h.index), *single, 1e-9));
    }
}

KTEST(scene_transforms_survive_very_deep_hierarchies) {
    Scene s;
    constexpr int kDepth = 100000;
    ObjectHandle cur = add(s, ObjectKind::Empty, "n0");
    set(s, cur, {1, 0, 0});
    for (int i = 1; i < kDepth; ++i) {
        cur = add(s, ObjectKind::Empty, "n", cur);
        set(s, cur, {1, 0, 0});
    }
    auto w = world_matrix(s, cur);
    KCHECK(w.has_value());
    if (w) KCHECK(near3(w->transform_point({0, 0, 0}), {static_cast<double>(kDepth), 0, 0}));
    const auto worlds = compute_world_matrices(s);
    KCHECK_EQ(worlds.size(), static_cast<std::size_t>(kDepth));
    KCHECK(near3(worlds.at(cur.index).transform_point({0, 0, 0}), {static_cast<double>(kDepth), 0, 0}));
}

// ---------------------------------------------------------------- draw list ---

KTEST(drawlist_skips_empties_and_follows_hierarchy_order) {
    Scene s;
    auto root = add(s, ObjectKind::Empty, "root");
    auto c1 = add(s, ObjectKind::Cube, "c1", root);
    auto sp = add(s, ObjectKind::Sphere, "sp", c1);
    auto c2 = add(s, ObjectKind::Plane, "pl", root);
    auto other = add(s, ObjectKind::Cube, "other");
    const auto list = build_draw_list(s);
    KCHECK_EQ(list.size(), static_cast<std::size_t>(4));  // root is Empty: not drawn
    if (list.size() == 4) {
        KCHECK(list[0].object == c1 && list[0].kind == ObjectKind::Cube);
        KCHECK(list[1].object == sp && list[1].kind == ObjectKind::Sphere);
        KCHECK(list[2].object == c2 && list[2].kind == ObjectKind::Plane);
        KCHECK(list[3].object == other);
    }
    for (const auto& item : list) KCHECK(item.kind != ObjectKind::Empty);
}

KTEST(drawlist_world_matrix_is_row_major_float_copy_of_the_math_matrix) {
    Scene s;
    auto parent = add(s, ObjectKind::Empty, "p");
    auto cube = add(s, ObjectKind::Cube, "c", parent);
    set(s, parent, {10, 0, 0});
    set(s, cube, {1, 2, 3}, {0, 0, 0}, {2, 2, 2});
    const auto list = build_draw_list(s);
    KCHECK_EQ(list.size(), static_cast<std::size_t>(1));
    if (list.size() != 1) return;
    const Mat4 w = *world_matrix(s, cube);
    for (std::size_t i = 0; i < 16; ++i) KCHECK_EQ(list[0].world[i], static_cast<float>(w.m[i]));
    // Row-major layout: translation lives in elements 3, 7, 11 (last column of rows 0..2).
    KCHECK_EQ(list[0].world[3], 11.0f);
    KCHECK_EQ(list[0].world[7], 2.0f);
    KCHECK_EQ(list[0].world[11], 3.0f);
    KCHECK_EQ(list[0].world[0], 2.0f);   // scale on the diagonal
    KCHECK_EQ(list[0].world[15], 1.0f);
}

KTEST(drawlist_marks_only_the_selected_drawable_object) {
    Scene s;
    auto a = add(s, ObjectKind::Cube, "a");
    auto b = add(s, ObjectKind::Cube, "b");
    auto e = add(s, ObjectKind::Empty, "e");
    auto count_selected = [](const std::vector<DrawItem>& l) {
        int n = 0;
        for (const auto& i : l) n += i.selected ? 1 : 0;
        return n;
    };
    KCHECK_EQ(count_selected(build_draw_list(s)), 0);
    KCHECK_EQ(count_selected(build_draw_list(s, kNullObjectHandle)), 0);
    const auto la = build_draw_list(s, a);
    KCHECK_EQ(count_selected(la), 1);
    KCHECK(la[0].selected && !la[1].selected);
    KCHECK_EQ(count_selected(build_draw_list(s, e)), 0);        // selecting an Empty highlights nothing
    KCHECK(s.destroy(b).ok());
    KCHECK_EQ(count_selected(build_draw_list(s, b)), 0);        // stale selection highlights nothing
    (void)e;
}

KTEST(drawlist_empty_scene_yields_empty_list) {
    KCHECK(build_draw_list(Scene{}).empty());
}
