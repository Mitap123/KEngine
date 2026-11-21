#include <limits>
#include <set>
#include <string>
#include <vector>

#include "Engine/Scene/Scene.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {
ObjectHandle must_create(Scene& s, ObjectKind kind, const char* name, ObjectHandle parent = kNullObjectHandle) {
    auto r = s.create(kind, name, parent);
    KCHECK(r.ok());
    return r.ok() ? r.value() : kNullObjectHandle;
}
std::string code_of(const Status& st) { return st.ok() ? std::string() : st.error().code; }
}  // namespace

KTEST(scene_create_assigns_unique_non_nil_uuids_and_defaults) {
    Scene s;
    std::set<std::string> uuids;
    for (int i = 0; i < 100; ++i) {
        ObjectHandle h = must_create(s, ObjectKind::Cube, "Cube");
        const SceneObject* o = s.get(h);
        KCHECK(o != nullptr);
        if (!o) continue;
        KCHECK(!o->id.is_nil());
        uuids.insert(o->id.to_string());
        KCHECK(o->kind == ObjectKind::Cube);
        KCHECK(o->parent.is_null());
        KCHECK(o->transform == Transform{});
        KCHECK(s.find_by_uuid(o->id) == h);
    }
    KCHECK_EQ(uuids.size(), static_cast<std::size_t>(100));
    KCHECK_EQ(s.object_count(), static_cast<std::size_t>(100));
    KCHECK_EQ(s.roots().size(), static_cast<std::size_t>(100));
}

KTEST(scene_destroy_invalidates_handle_and_removes_uuid_lookup) {
    Scene s;
    ObjectHandle h = must_create(s, ObjectKind::Sphere, "S");
    const Uuid id = s.get(h)->id;
    KCHECK(s.destroy(h).ok());
    KCHECK(s.get(h) == nullptr);
    KCHECK(s.status(h) == HandleStatus::Stale);
    KCHECK(s.find_by_uuid(id).is_null());
    KCHECK_EQ(s.object_count(), static_cast<std::size_t>(0));
    KCHECK(s.roots().empty());
    KCHECK_EQ(code_of(s.destroy(h)), std::string("scene.invalid_handle"));  // double destroy is an error, not UB
}

KTEST(scene_stale_handle_does_not_resolve_to_object_reusing_its_slot) {
    Scene s;
    ObjectHandle a = must_create(s, ObjectKind::Cube, "A");
    KCHECK(s.destroy(a).ok());
    ObjectHandle b = must_create(s, ObjectKind::Plane, "B");  // reuses A's slot
    KCHECK_EQ(a.index, b.index);
    KCHECK(a != b);
    KCHECK(s.get(a) == nullptr);
    KCHECK(s.get(b) != nullptr && s.get(b)->name == "B");
    // Every mutating API rejects the stale handle and leaves B untouched.
    KCHECK_EQ(code_of(s.set_name(a, "hijack")), std::string("scene.invalid_handle"));
    KCHECK_EQ(code_of(s.set_transform(a, Transform{})), std::string("scene.invalid_handle"));
    KCHECK_EQ(code_of(s.destroy(a)), std::string("scene.invalid_handle"));
    KCHECK_EQ(code_of(s.reparent(a, kNullObjectHandle)), std::string("scene.invalid_handle"));
    KCHECK(s.get(b) != nullptr && s.get(b)->name == "B");
}

KTEST(scene_hierarchy_and_destroy_subtree) {
    Scene s;
    ObjectHandle root = must_create(s, ObjectKind::Empty, "root");
    ObjectHandle c1 = must_create(s, ObjectKind::Cube, "c1", root);
    ObjectHandle c2 = must_create(s, ObjectKind::Cube, "c2", root);
    ObjectHandle g = must_create(s, ObjectKind::Sphere, "g", c1);
    ObjectHandle other = must_create(s, ObjectKind::Plane, "other");

    KCHECK_EQ(s.get(root)->children.size(), static_cast<std::size_t>(2));
    KCHECK(s.get(g)->parent == c1);

    std::vector<std::string> order;
    s.visit_preorder([&](ObjectHandle, const SceneObject& o) { order.push_back(o.name); });
    KCHECK_EQ(order.size(), static_cast<std::size_t>(5));
    if (order.size() == 5) {
        KCHECK_EQ(order[0], std::string("root"));
        KCHECK_EQ(order[1], std::string("c1"));
        KCHECK_EQ(order[2], std::string("g"));
        KCHECK_EQ(order[3], std::string("c2"));
        KCHECK_EQ(order[4], std::string("other"));
    }

    const Uuid gid = s.get(g)->id;
    KCHECK(s.destroy(c1).ok());  // takes grandchild with it
    KCHECK(s.get(c1) == nullptr);
    KCHECK(s.get(g) == nullptr);
    KCHECK(s.find_by_uuid(gid).is_null());
    KCHECK(s.get(c2) != nullptr);
    KCHECK(s.get(other) != nullptr);
    KCHECK_EQ(s.get(root)->children.size(), static_cast<std::size_t>(1));
    KCHECK_EQ(s.object_count(), static_cast<std::size_t>(3));
}

KTEST(scene_create_with_bad_parent_fails_without_side_effects) {
    Scene s;
    ObjectHandle p = must_create(s, ObjectKind::Empty, "p");
    KCHECK(s.destroy(p).ok());
    auto r = s.create(ObjectKind::Cube, "orphan", p);  // stale parent
    KCHECK(!r.ok());
    if (!r.ok()) KCHECK_EQ(r.error().code, std::string("scene.invalid_handle"));
    KCHECK_EQ(s.object_count(), static_cast<std::size_t>(0));
    KCHECK(s.create(ObjectKind::Cube, "forged", ObjectHandle{77, 1}).ok() == false);
}

KTEST(scene_create_with_uuid_rejects_nil_and_duplicates) {
    Scene s;
    const Uuid id = Uuid::from_random_words(5, 6);
    auto nil = s.create_with_uuid(Uuid(), ObjectKind::Cube, "n");
    KCHECK(!nil.ok());
    if (!nil.ok()) KCHECK_EQ(nil.error().code, std::string("scene.nil_uuid"));

    KCHECK(s.create_with_uuid(id, ObjectKind::Cube, "a").ok());
    auto dup = s.create_with_uuid(id, ObjectKind::Cube, "b");
    KCHECK(!dup.ok());
    if (!dup.ok()) KCHECK_EQ(dup.error().code, std::string("scene.duplicate_uuid"));
    KCHECK_EQ(s.object_count(), static_cast<std::size_t>(1));

    // After destroying, the UUID becomes reusable (needed by undo of delete).
    KCHECK(s.destroy(s.find_by_uuid(id)).ok());
    KCHECK(s.create_with_uuid(id, ObjectKind::Cube, "again").ok());
}

KTEST(scene_reparent_moves_appends_and_rejects_cycles) {
    Scene s;
    ObjectHandle a = must_create(s, ObjectKind::Empty, "a");
    ObjectHandle b = must_create(s, ObjectKind::Empty, "b", a);
    ObjectHandle c = must_create(s, ObjectKind::Empty, "c", b);
    ObjectHandle d = must_create(s, ObjectKind::Empty, "d");

    KCHECK_EQ(code_of(s.reparent(a, a)), std::string("scene.cycle"));
    KCHECK_EQ(code_of(s.reparent(a, c)), std::string("scene.cycle"));  // under own descendant
    KCHECK(s.get(a)->parent.is_null());                                // unchanged after rejection
    KCHECK_EQ(s.get(a)->children.size(), static_cast<std::size_t>(1));

    KCHECK(s.reparent(c, d).ok());                                     // move subtree member
    KCHECK(s.get(c)->parent == d);
    KCHECK(s.get(b)->children.empty());
    KCHECK_EQ(s.get(d)->children.size(), static_cast<std::size_t>(1));

    KCHECK(s.reparent(b, kNullObjectHandle).ok());                           // to root, appended last
    KCHECK_EQ(s.roots().size(), static_cast<std::size_t>(3));
    KCHECK(s.roots().back() == b);
    KCHECK(s.get(a)->children.empty());
}

KTEST(scene_set_transform_validates_and_set_name_works) {
    Scene s;
    ObjectHandle h = must_create(s, ObjectKind::Cube, "c");
    Transform t;
    t.position = {1, 2, 3};
    t.rotation_euler_deg = {10, 20, 30};
    t.scale = {2, 2, 2};
    KCHECK(s.set_transform(h, t).ok());
    KCHECK(s.get(h)->transform == t);

    Transform bad = t;
    bad.position[1] = std::numeric_limits<double>::quiet_NaN();
    KCHECK_EQ(code_of(s.set_transform(h, bad)), std::string("scene.non_finite_transform"));
    bad = t;
    bad.scale[2] = std::numeric_limits<double>::infinity();
    KCHECK_EQ(code_of(s.set_transform(h, bad)), std::string("scene.non_finite_transform"));
    KCHECK(s.get(h)->transform == t);  // unchanged after rejected edits

    KCHECK(s.set_name(h, "renamed").ok());
    KCHECK_EQ(s.get(h)->name, std::string("renamed"));
}

KTEST(scene_object_kind_names_round_trip) {
    for (ObjectKind kind : {ObjectKind::Empty, ObjectKind::Cube, ObjectKind::Sphere, ObjectKind::Plane}) {
        ObjectKind back = ObjectKind::Empty;
        KCHECK(parse_object_kind(to_string(kind), back));
        KCHECK(back == kind);
    }
    ObjectKind out = ObjectKind::Cube;
    KCHECK(!parse_object_kind("Cube", out));   // case-sensitive
    KCHECK(!parse_object_kind("", out));
    KCHECK(!parse_object_kind("cylinder", out));
    KCHECK(out == ObjectKind::Cube);           // untouched on failure
}

KTEST(scene_very_deep_hierarchy_does_not_overflow_the_stack) {
    Scene s;
    ObjectHandle cur = must_create(s, ObjectKind::Empty, "n0");
    const ObjectHandle top = cur;
    const int kDepth = static_cast<int>(ktest::scaled(10000, 100000));
    for (int i = 1; i < kDepth; ++i) cur = must_create(s, ObjectKind::Empty, "n", cur);
    std::size_t visited = 0;
    s.visit_preorder([&](ObjectHandle, const SceneObject&) { ++visited; });
    KCHECK_EQ(visited, static_cast<std::size_t>(kDepth));
    KCHECK(s.destroy(top).ok());
    KCHECK_EQ(s.object_count(), static_cast<std::size_t>(0));
}

KTEST(scene_sibling_index_insertion_and_reordering) {
    Scene s;
    auto a = must_create(s, ObjectKind::Empty, "a");
    auto b = must_create(s, ObjectKind::Empty, "b");
    auto c = must_create(s, ObjectKind::Empty, "c");
    KCHECK(s.sibling_index(a) == 0u && s.sibling_index(b) == 1u && s.sibling_index(c) == 2u);

    // Insert at an explicit position (used to undo a delete at its old place).
    auto mid = s.create_with_uuid(Uuid::from_random_words(9, 9), ObjectKind::Cube, "mid", kNullObjectHandle, 1);
    KCHECK(mid.ok());
    if (mid.ok()) KCHECK(s.sibling_index(mid.value()) == 1u && s.sibling_index(b) == 2u);
    // Out-of-range indices clamp to "append"; they are not errors.
    auto last = s.create_with_uuid(Uuid::from_random_words(8, 8), ObjectKind::Cube, "last", kNullObjectHandle, 999);
    KCHECK(last.ok());
    if (last.ok()) KCHECK(s.sibling_index(last.value()) == s.roots().size() - 1);

    // Reparent to an explicit index under a parent, then reorder within the same parent.
    auto p = must_create(s, ObjectKind::Empty, "p");
    auto k1 = must_create(s, ObjectKind::Cube, "k1", p);
    auto k2 = must_create(s, ObjectKind::Cube, "k2", p);
    KCHECK(s.reparent(a, p, 0).ok());                       // a becomes p's first child
    KCHECK(s.sibling_index(a) == 0u && s.sibling_index(k1) == 1u && s.sibling_index(k2) == 2u);
    KCHECK(s.reparent(a, p, 1).ok());                       // same parent: index is after removal
    KCHECK(s.sibling_index(k1) == 0u && s.sibling_index(a) == 1u && s.sibling_index(k2) == 2u);
    KCHECK(s.reparent(a, p).ok());                          // default appends
    KCHECK(s.sibling_index(a) == 2u);
    // A failed reparent (cycle) changes nothing.
    KCHECK_EQ(code_of(s.reparent(p, k1, 0)), std::string("scene.cycle"));
    KCHECK(s.get(p)->parent.is_null());

    KCHECK(!s.sibling_index(kNullObjectHandle).has_value());
    KCHECK(s.destroy(c).ok());
    KCHECK(!s.sibling_index(c).has_value());                // stale handle
}
