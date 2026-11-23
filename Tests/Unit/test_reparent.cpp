// Reparenting with sibling index and world-transform preservation (Explorer drag-and-drop).
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "Editor/Core/EditorDocument.h"
#include "Engine/Editing/SceneEditor.h"
#include "Engine/Scene/SceneTransforms.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {

std::string snap(const Scene& s) { return save_scene_to_string(s).value(); }
std::string code(const Status& st) { return st.ok() ? std::string() : st.error().code; }
const SceneObject* find(const Scene& s, const Uuid& id) { return s.get(s.find_by_uuid(id)); }

Transform trs(Vec3 p, Vec3 r = {0, 0, 0}, Vec3 s = {1, 1, 1}) {
    Transform t;
    t.position = p;
    t.rotation_euler_deg = r;
    t.scale = s;
    return t;
}

Uuid make(SceneEditor& ed, const char* name, const Transform& t, const Uuid& parent = Uuid()) {
    auto r = ed.create(ObjectKind::Cube, name, parent);
    KCHECK(r.ok());
    if (!r.ok()) return Uuid();
    KCHECK(ed.set_transform(r.value(), t).ok());
    ed.end_edit_gesture();
    return r.value();
}

Mat4 world_of(const Scene& s, const Uuid& id) { return world_matrix(s, s.find_by_uuid(id)).value(); }

std::uint64_t g_seed = 0x9E3779B97F4A7C15ull;
std::uint32_t rnd(std::uint32_t n) {
    g_seed = g_seed * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<std::uint32_t>((g_seed >> 33) % n);
}
double frac() { return static_cast<double>(rnd(2000001)) / 1000000.0 - 1.0; }  // [-1, 1]

}  // namespace

// --------------------------------------------------------------- decompose ---

KTEST(decompose_round_trips_random_transforms_and_the_gimbal_poles) {
    std::vector<Transform> cases;
    for (int i = 0; i < 400; ++i)
        cases.push_back(trs({frac() * 50, frac() * 50, frac() * 50}, {frac() * 180, frac() * 180, frac() * 180},
                            {0.2 + std::fabs(frac()) * 3, 0.2 + std::fabs(frac()) * 3, 0.2 + std::fabs(frac()) * 3}));
    for (double x : {90.0, -90.0})  // gimbal lock, with a Y/Z mix that is only representable on Y
        cases.push_back(trs({1, 2, 3}, {x, 33.0, 21.0}, {2, 1, 0.5}));
    cases.push_back(trs({0, 0, 0}, {0, 0, 0}, {-1, 1, 1}));      // reflection
    cases.push_back(trs({5, 0, 0}, {10, 20, 30}, {1, -2, 3}));   // reflection with rotation
    for (const Transform& t : cases) {
        const Mat4 m = local_matrix(t);
        const auto d = decompose_trs(m);
        KCHECK(d.has_value());
        if (d) KCHECK(nearly_equal(local_matrix(*d), m, 1e-7));
    }
}

KTEST(decompose_rejects_shear_perspective_degenerate_and_non_finite) {
    // Scale applied AFTER a rotation (S * R) shears; T*R*S cannot express it.
    const Mat4 sheared = Mat4::scaling({1, 2, 1}) * Mat4::rotation_z_deg(45);
    KCHECK(!decompose_trs(sheared).has_value());
    KCHECK(!decompose_trs(Mat4::scaling({1, 0, 1})).has_value());        // zero-length axis
    Mat4 persp = Mat4::identity();
    persp.at(3, 2) = 0.5;
    KCHECK(!decompose_trs(persp).has_value());
    Mat4 nan_m = Mat4::identity();
    nan_m.at(0, 3) = std::nan("");
    KCHECK(!decompose_trs(nan_m).has_value());
    KCHECK(decompose_trs(Mat4::identity()).has_value());
}

// ------------------------------------------------------------- the command ---

KTEST(reparent_keep_world_leaves_the_object_where_it_was_and_undo_is_exact) {
    SceneEditor ed;
    const Uuid parent = make(ed, "Parent", trs({10, 0, 0}, {0, 90, 0}, {2, 2, 2}));
    const Uuid obj = make(ed, "Obj", trs({1, 2, 3}, {10, 20, 30}, {1, 1, 1}));
    const std::string before = snap(ed.scene());
    const Mat4 w0 = world_of(ed.scene(), obj);

    KCHECK(ed.reparent(obj, parent, Scene::kAppend, ReparentMode::KeepWorld).ok());
    KCHECK(find(ed.scene(), obj)->parent == ed.scene().find_by_uuid(parent));
    KCHECK(nearly_equal(world_of(ed.scene(), obj), w0, 1e-7));
    KCHECK(!(find(ed.scene(), obj)->transform == trs({1, 2, 3}, {10, 20, 30})));  // local numbers did change

    const std::string after = snap(ed.scene());
    KCHECK(ed.undo().ok());
    KCHECK_EQ(snap(ed.scene()), before);          // byte-identical, including the original transform
    KCHECK(ed.redo().ok());
    KCHECK_EQ(snap(ed.scene()), after);
}

KTEST(reparent_keep_local_keeps_the_numbers_and_moves_the_world) {
    SceneEditor ed;
    const Uuid parent = make(ed, "Parent", trs({10, 0, 0}));
    const Uuid obj = make(ed, "Obj", trs({1, 2, 3}));
    KCHECK(ed.reparent(obj, parent).ok());   // default = KeepLocal
    KCHECK(find(ed.scene(), obj)->transform == trs({1, 2, 3}));
    const Vec3 w = world_of(ed.scene(), obj).transform_point({0, 0, 0});
    KCHECK(std::fabs(w[0] - 11.0) < 1e-9 && std::fabs(w[1] - 2.0) < 1e-9 && std::fabs(w[2] - 3.0) < 1e-9);
}

KTEST(reparent_keep_world_to_root_and_between_parents_keeps_children_in_place_too) {
    SceneEditor ed;
    const Uuid pa = make(ed, "A", trs({5, 0, 0}, {0, 30, 0}, {2, 2, 2}));
    const Uuid pb = make(ed, "B", trs({-3, 4, 0}, {20, 0, 45}, {0.5, 0.5, 0.5}));
    const Uuid obj = make(ed, "Obj", trs({1, 1, 1}, {0, 10, 0}), pa);
    const Uuid kid = make(ed, "Kid", trs({0, 2, 0}, {5, 5, 5}), obj);
    const Mat4 wo = world_of(ed.scene(), obj), wk = world_of(ed.scene(), kid);

    KCHECK(ed.reparent(obj, pb, Scene::kAppend, ReparentMode::KeepWorld).ok());
    KCHECK(nearly_equal(world_of(ed.scene(), obj), wo, 1e-7));
    KCHECK(nearly_equal(world_of(ed.scene(), kid), wk, 1e-7));   // the subtree did not move either
    KCHECK(ed.reparent(obj, Uuid(), Scene::kAppend, ReparentMode::KeepWorld).ok());
    KCHECK(nearly_equal(world_of(ed.scene(), obj), wo, 1e-7));
    KCHECK(nearly_equal(world_of(ed.scene(), kid), wk, 1e-7));
}

KTEST(reparent_sibling_index_places_the_object_and_reorders_within_a_parent) {
    SceneEditor ed;
    const Uuid p = make(ed, "P", {});
    const Uuid a = make(ed, "a", {}, p), b = make(ed, "b", {}, p), c = make(ed, "c", {}, p);
    const Uuid x = make(ed, "x", {});
    const std::string s0 = snap(ed.scene());
    auto order = [&] {
        std::string r;
        for (ObjectHandle h : find(ed.scene(), p)->children) r += ed.scene().get(h)->name;
        return r;
    };
    KCHECK_EQ(order(), std::string("abc"));
    KCHECK(ed.reparent(x, p, 1).ok());
    KCHECK_EQ(order(), std::string("axbc"));
    KCHECK(ed.reparent(c, p, 0).ok());            // same parent: reorder (index is after removal)
    KCHECK_EQ(order(), std::string("caxb"));
    KCHECK(ed.undo().ok());
    KCHECK_EQ(order(), std::string("axbc"));
    KCHECK(ed.undo().ok());
    KCHECK_EQ(snap(ed.scene()), s0);
    (void)a; (void)b;
}

KTEST(reparent_keep_world_failures_leave_scene_history_and_dirty_flag_unchanged) {
    SceneEditor ed;
    // Non-uniform parent scale + a rotated child: the needed local matrix has shear.
    const Uuid skew = make(ed, "Skew", trs({0, 0, 0}, {0, 0, 0}, {1, 3, 1}));
    const Uuid rotated = make(ed, "Rot", trs({0, 0, 0}, {0, 0, 45}));
    const Uuid flat = make(ed, "Flat", trs({0, 0, 0}, {0, 0, 0}, {0, 1, 1}));
    const Uuid plain = make(ed, "Plain", trs({1, 1, 1}));
    const std::string before = snap(ed.scene());
    const bool dirty = ed.is_dirty();
    const std::string undo_name = ed.undo_name().value_or("");

    KCHECK_EQ(code(ed.reparent(rotated, skew, Scene::kAppend, ReparentMode::KeepWorld)),
              std::string("command.world_transform_not_representable"));
    KCHECK_EQ(code(ed.reparent(plain, flat, Scene::kAppend, ReparentMode::KeepWorld)),
              std::string("command.singular_parent_transform"));
    KCHECK_EQ(code(ed.reparent(skew, skew, Scene::kAppend, ReparentMode::KeepWorld)), std::string("scene.cycle"));
    KCHECK_EQ(snap(ed.scene()), before);
    KCHECK_EQ(ed.is_dirty(), dirty);
    KCHECK_EQ(ed.undo_name().value_or(""), undo_name);

    // The same moves are fine when the transform numbers are kept, or the child is axis-aligned.
    KCHECK(ed.reparent(rotated, skew).ok());
    const Uuid aligned = make(ed, "Aligned", trs({2, 0, 0}, {0, 90, 0}));
    KCHECK(ed.reparent(aligned, skew, Scene::kAppend, ReparentMode::KeepWorld).ok());
}

KTEST(reparent_keep_world_with_unchanged_world_does_not_touch_the_transform_bytes) {
    SceneEditor ed;
    const Uuid p = make(ed, "P", {});
    const Uuid a = make(ed, "a", trs({1, 2, 3}, {200, 10, 300}, {1, 1, 1}), p);   // Euler values that decompose would renormalise
    make(ed, "b", {}, p);
    const Transform before = find(ed.scene(), a)->transform;
    KCHECK(ed.reparent(a, p, Scene::kAppend, ReparentMode::KeepWorld).ok());      // reorder only
    KCHECK(find(ed.scene(), a)->transform == before);
    KCHECK(ed.reparent(a, Uuid(), Scene::kAppend, ReparentMode::KeepWorld).ok()); // identity parent chain: world == local
    KCHECK(find(ed.scene(), a)->transform == before);
}

KTEST(reparent_keep_world_randomized_preserves_every_world_matrix_and_undoes_exactly) {
    // Uniform scales only, so every move is representable and no move may fail except a cycle.
    SceneEditor ed;
    std::vector<Uuid> ids;
    for (int i = 0; i < 24; ++i) {
        const double s = 0.4 + std::fabs(frac()) * 2.5;
        const Uuid parent = (ids.empty() || rnd(4) == 0) ? Uuid() : ids[rnd(static_cast<std::uint32_t>(ids.size()))];
        ids.push_back(make(ed, "o", trs({frac() * 20, frac() * 20, frac() * 20}, {frac() * 180, frac() * 180, frac() * 180}, {s, s, s}), parent));
    }
    std::size_t moved = 0, cycles = 0;
    const int steps = static_cast<int>(ktest::scaled(150, 600));
    for (int i = 0; i < steps; ++i) {
        const Uuid id = ids[rnd(static_cast<std::uint32_t>(ids.size()))];
        const Uuid to = rnd(5) == 0 ? Uuid() : ids[rnd(static_cast<std::uint32_t>(ids.size()))];
        const std::size_t index = rnd(3) == 0 ? Scene::kAppend : rnd(5);
        const std::string before = snap(ed.scene());
        std::vector<std::pair<Uuid, Mat4>> worlds;
        ed.scene().visit_preorder([&](ObjectHandle h, const SceneObject& o) { worlds.emplace_back(o.id, world_matrix(ed.scene(), h).value()); });

        const Status st = ed.reparent(id, to, index, ReparentMode::KeepWorld);
        if (!st) {
            KCHECK_EQ(code(st), std::string("scene.cycle"));
            KCHECK_EQ(snap(ed.scene()), before);
            ++cycles;
            continue;
        }
        ++moved;
        for (const auto& [oid, w] : worlds) KCHECK(nearly_equal(world_of(ed.scene(), oid), w, 1e-6));
        const std::string after = snap(ed.scene());
        KCHECK(ed.undo().ok());
        KCHECK_EQ(snap(ed.scene()), before);
        KCHECK(ed.redo().ok());
        KCHECK_EQ(snap(ed.scene()), after);
    }
    KCHECK(moved > 20);    // the run must really exercise moves and rejected cycles
    KCHECK(cycles > 0);
}

// ---------------------------------------------------------------- document ---

KTEST(document_reparent_keeps_world_selection_and_labels_the_undo_step) {
    EditorDocument d;
    const Uuid parent = d.create(ObjectKind::Empty).value();
    KCHECK(d.select(parent).ok());
    KCHECK(d.set_selected_transform(trs({5, 0, 0}, {0, 0, 0}, {2, 2, 2})).ok());
    d.end_edit_gesture();
    const Uuid obj = d.create(ObjectKind::Cube).value();
    KCHECK(d.set_selected_transform(trs({9, 0, 0})).ok());
    d.end_edit_gesture();
    const Mat4 w0 = world_of(d.scene(), obj);
    const std::string name = find(d.scene(), obj)->name;

    KCHECK(d.can_reparent(obj, parent).ok());
    KCHECK(d.reparent(obj, parent).ok());                       // default KeepWorld
    KCHECK(nearly_equal(world_of(d.scene(), obj), w0, 1e-9));
    KCHECK(d.selected() == obj);                                // selection survives the move
    KCHECK_EQ(d.undo_label(), "Undo Reparent '" + name + "'");
    KCHECK(d.undo().ok());
    KCHECK(find(d.scene(), obj)->parent.is_null());
}

KTEST(document_reparent_to_the_same_place_is_a_no_op_without_an_undo_step) {
    EditorDocument d;
    const Uuid a = d.create(ObjectKind::Cube).value();
    const Uuid b = d.create(ObjectKind::Cube).value();
    d.end_edit_gesture();
    KCHECK(d.reparent(b, Uuid(), 1).ok());                       // already root index 1
    KCHECK(d.reparent(b, Uuid()).ok());                          // append = last = where it is
    KCHECK_EQ(d.undo_label(), std::string("Undo Create '") + find(d.scene(), b)->name + "'");   // no Reparent step was added
    KCHECK(d.reparent(a, Uuid(), 5).ok());                       // index past the end = append: a really moves
    KCHECK(d.undo_label().find("Reparent") != std::string::npos);
}

KTEST(document_can_reparent_reports_cycles_missing_objects_and_play) {
    EditorDocument d;
    const Uuid p = d.create(ObjectKind::Empty).value();
    const Uuid c = d.create(ObjectKind::Cube).value();
    KCHECK(d.reparent(c, p).ok());
    const Uuid ghost = Uuid::from_random_words(1, 2);
    KCHECK_EQ(code(d.can_reparent(p, p)), std::string("scene.cycle"));
    KCHECK_EQ(code(d.can_reparent(p, c)), std::string("scene.cycle"));       // into its own child
    KCHECK_EQ(code(d.can_reparent(ghost, p)), std::string("command.object_not_found"));
    KCHECK_EQ(code(d.can_reparent(c, ghost)), std::string("command.object_not_found"));
    KCHECK(d.can_reparent(c, Uuid()).ok());
    const std::string before = snap(d.scene());
    KCHECK_EQ(code(d.reparent(p, c)), std::string("scene.cycle"));
    KCHECK_EQ(snap(d.scene()), before);

    KCHECK(d.play().ok());
    KCHECK_EQ(code(d.can_reparent(c, Uuid())), std::string("editor.read_only_while_playing"));
    KCHECK_EQ(code(d.reparent(c, Uuid())), std::string("editor.read_only_while_playing"));
    KCHECK(d.stop().ok());
    KCHECK(d.can_reparent(c, Uuid()).ok());
}
