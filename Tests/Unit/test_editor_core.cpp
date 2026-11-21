#include <cmath>
#include <fstream>
#include <string>

#include "Editor/Core/CameraFraming.h"
#include "Editor/Core/EditorDocument.h"
#include "Editor/Core/ExplorerModel.h"
#include "Editor/Core/InspectorModel.h"
#include "Editor/Core/PlayDemo.h"
#include "Engine/Rendering/Picking.h"
#include "Engine/Scene/SceneTransforms.h"
#include "Tests/Unit/TempDir.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {

std::string snap(const Scene& s) { return save_scene_to_string(s).value(); }
std::string code(const Status& st) { return st.ok() ? std::string() : st.error().code; }

Uuid make(EditorDocument& d, ObjectKind kind) {
    auto r = d.create(kind);
    KCHECK(r.ok());
    return r.ok() ? r.value() : Uuid();
}

Transform at(double x, double y = 0, double z = 0) {
    Transform t;
    t.position = {x, y, z};
    return t;
}

}  // namespace

// ----------------------------------------------------------------- document ---

KTEST(document_starts_untitled_empty_clean_and_without_selection) {
    EditorDocument d;
    KCHECK_EQ(d.title(), std::string("Untitled"));
    KCHECK(!d.is_dirty() && !d.has_path() && !d.playing());
    KCHECK_EQ(d.scene().object_count(), static_cast<std::size_t>(0));
    KCHECK(d.selected().is_nil() && d.selected_object() == nullptr && d.selected_handle().is_null());
    KCHECK(!d.can_undo() && !d.can_redo());
    KCHECK_EQ(d.undo_label(), std::string("Undo"));
    KCHECK_EQ(code(d.delete_selected()), std::string("document.no_selection"));
    KCHECK_EQ(code(d.rename_selected("x")), std::string("document.no_selection"));
    KCHECK_EQ(code(d.set_selected_transform(Transform{})), std::string("document.no_selection"));
    KCHECK_EQ(code(d.save()), std::string("document.no_path"));
}

KTEST(document_create_selects_the_new_object_and_names_are_unique) {
    EditorDocument d;
    const Uuid a = make(d, ObjectKind::Cube);
    KCHECK(d.selected() == a);
    KCHECK_EQ(d.selected_object()->name, std::string("Cube"));
    const Uuid b = make(d, ObjectKind::Cube);
    const Uuid c = make(d, ObjectKind::Cube);
    KCHECK(d.selected() == c);
    KCHECK_EQ(d.scene().get(d.scene().find_by_uuid(b))->name, std::string("Cube 2"));
    KCHECK_EQ(d.selected_object()->name, std::string("Cube 3"));
    make(d, ObjectKind::Sphere);
    make(d, ObjectKind::Plane);
    make(d, ObjectKind::Empty);
    KCHECK_EQ(d.selected_object()->name, std::string("Empty"));
    KCHECK(d.is_dirty());
    KCHECK_EQ(d.title(), std::string("Untitled*"));
    // A renamed object frees its old default name.
    KCHECK(d.select(a).ok());
    KCHECK(d.rename_selected("Hero").ok());
    KCHECK_EQ(d.scene().get(d.scene().find_by_uuid(a))->name, std::string("Hero"));
    make(d, ObjectKind::Cube);
    KCHECK_EQ(d.selected_object()->name, std::string("Cube"));
}

KTEST(document_selection_follows_the_scene_through_delete_undo_redo) {
    EditorDocument d;
    const Uuid a = make(d, ObjectKind::Cube);
    const Uuid b = make(d, ObjectKind::Sphere);
    KCHECK(d.select(a).ok());
    KCHECK(d.delete_selected().ok());
    KCHECK(d.selected().is_nil());                            // the selection died with its object
    KCHECK(d.scene().find_by_uuid(a).is_null());
    KCHECK(d.undo().ok());                                    // the object is back, same UUID
    KCHECK(!d.scene().find_by_uuid(a).is_null());
    KCHECK(d.select(a).ok());
    KCHECK(d.undo().ok());                                    // undoes the creation of b
    KCHECK(d.scene().find_by_uuid(b).is_null());
    KCHECK(d.selected() == a);                                // unrelated selection survives
    KCHECK(d.undo().ok());                                    // undoes the creation of a itself
    KCHECK(d.selected().is_nil());                            // ...so the selection is cleared, not dangling
    KCHECK(d.redo().ok());
    KCHECK(d.selected().is_nil());                            // redo does not invent a selection

    // Selecting something that does not exist is an error and changes nothing.
    KCHECK(d.select(a).ok());
    KCHECK_EQ(code(d.select(Uuid::from_random_words(1, 1))), std::string("command.object_not_found"));
    KCHECK(d.selected() == a);
    KCHECK(d.select(Uuid()).ok());                            // nil = clear
    KCHECK(d.selected().is_nil());
}

KTEST(document_transform_edits_merge_into_one_undo_step) {
    EditorDocument d;
    const Uuid a = make(d, ObjectKind::Cube);
    for (int i = 1; i <= 8; ++i) KCHECK(d.set_selected_transform(at(i), true).ok());
    d.end_edit_gesture();
    KCHECK(d.selected_object()->transform == at(8));
    KCHECK_EQ(d.undo_label(), std::string("Undo Transform 'Cube'"));
    KCHECK(d.undo().ok());
    KCHECK(d.selected_object()->transform == Transform{});    // one undo reverts the whole drag
    KCHECK_EQ(d.redo_label(), std::string("Redo Transform 'Cube'"));
    (void)a;
}

KTEST(document_file_operations_title_dirty_and_failures) {
    ktest::TempDir dir;
    EditorDocument d;
    make(d, ObjectKind::Cube);
    make(d, ObjectKind::Sphere);
    KCHECK(d.save_as(dir / "scene.kscene").ok());
    KCHECK(d.has_path() && !d.is_dirty());
    KCHECK_EQ(d.title(), std::string("scene.kscene"));
    KCHECK(d.save().ok());                                    // plain Save reuses the path

    make(d, ObjectKind::Plane);
    KCHECK_EQ(d.title(), std::string("scene.kscene*"));
    KCHECK(d.save().ok());
    KCHECK_EQ(d.title(), std::string("scene.kscene"));

    // Save As to a bad location fails and keeps the document, path and dirty state.
    make(d, ObjectKind::Cube);
    KCHECK(!d.save_as(dir / "no_such_dir" / "x.kscene").ok());
    KCHECK_EQ(d.display_name(), std::string("scene.kscene"));
    KCHECK(d.is_dirty());

    // Open into a fresh document.
    EditorDocument e;
    KCHECK(e.open(dir / "scene.kscene").ok());
    KCHECK_EQ(e.scene().object_count(), static_cast<std::size_t>(3));
    KCHECK(!e.is_dirty() && e.selected().is_nil() && !e.can_undo());
    KCHECK(e.warnings().empty());

    // Opening a missing or invalid file leaves the open document completely untouched.
    const std::string before = snap(e.scene());
    const Uuid sel = make(e, ObjectKind::Cube);
    KCHECK_EQ(code(e.open(dir / "missing.kscene")), std::string("file.not_found"));
    {
        std::ofstream(dir / "bad.kscene") << "{ not json";
    }
    KCHECK_EQ(code(e.open(dir / "bad.kscene")), std::string("json.syntax"));
    KCHECK(e.selected() == sel);
    KCHECK(e.is_dirty() && e.can_undo());
    KCHECK(e.scene().object_count() == 4 && before != snap(e.scene()));

    // New resets everything.
    KCHECK(e.new_scene().ok());
    KCHECK_EQ(e.title(), std::string("Untitled"));
    KCHECK(e.scene().object_count() == 0 && !e.can_undo() && e.selected().is_nil() && !e.has_path());
}

KTEST(document_open_reports_load_warnings) {
    ktest::TempDir dir;
    {
        std::ofstream(dir / "w.kscene") << R"({"schema":"kengine.scene","schema_version":1,"objects":[],"extra":1})";
    }
    EditorDocument d;
    KCHECK(d.open(dir / "w.kscene").ok());
    KCHECK_EQ(d.warnings().size(), static_cast<std::size_t>(1));
    KCHECK(d.warnings()[0].find("extra") != std::string::npos);
    KCHECK(d.new_scene().ok());
    KCHECK(d.warnings().empty());
}

KTEST(document_play_stop_isolates_and_keeps_selection_and_history) {
    EditorDocument d;
    const Uuid cube = make(d, ObjectKind::Cube);
    const Uuid sphere = make(d, ObjectKind::Sphere);
    KCHECK(d.select(cube).ok());
    KCHECK(d.set_selected_transform(at(1, 2, 3)).ok());
    const std::string authored = snap(d.editor().scene());
    const bool dirty = d.is_dirty();

    KCHECK(d.play().ok());
    KCHECK(d.playing());
    KCHECK_EQ(d.title(), std::string("Untitled* [Playing]"));
    KCHECK(d.selected() == cube);                             // selection carries into Play
    KCHECK(d.selected_object() != nullptr && d.selected_handle() == d.scene().find_by_uuid(cube));
    // Everything that edits the authoring scene is refused.
    const std::string ro = "editor.read_only_while_playing";
    {
        auto created = d.create(ObjectKind::Cube);
        KCHECK(!created.ok());
        if (!created.ok()) KCHECK_EQ(created.error().code, ro);
    }
    KCHECK_EQ(code(d.delete_selected()), ro);
    KCHECK_EQ(code(d.rename_selected("x")), ro);
    KCHECK_EQ(code(d.set_selected_transform(at(9))), ro);
    KCHECK_EQ(code(d.undo()), ro);
    KCHECK_EQ(code(d.new_scene()), ro);
    KCHECK_EQ(code(d.open("whatever.kscene")), ro);
    KCHECK(!d.can_undo() && !d.can_redo());

    // The demo behaviour changes only the play copy.
    advance_play_demo(*d.play_scene(), 1.0);
    KCHECK_EQ(d.selected_object()->transform.rotation_euler_deg[1], 45.0);   // shown scene = play copy
    KCHECK_EQ(snap(d.editor().scene()), authored);                           // authoring scene untouched
    KCHECK(d.select(sphere).ok());                                           // picking/selection works in Play
    KCHECK_EQ(code(d.play()), std::string("editor.already_playing"));

    KCHECK(d.stop().ok());
    KCHECK(!d.playing());
    KCHECK_EQ(d.title(), std::string("Untitled*"));
    KCHECK_EQ(snap(d.scene()), authored);
    KCHECK_EQ(d.is_dirty(), dirty);
    KCHECK(d.selected() == sphere);
    KCHECK(d.can_undo());
    KCHECK(d.undo().ok());                                    // the transform edit made before Play is still undoable
    KCHECK(d.scene().get(d.scene().find_by_uuid(cube))->transform == Transform{});
    KCHECK_EQ(code(d.stop()), std::string("editor.not_playing"));
}

KTEST(document_save_while_playing_writes_the_authored_scene) {
    ktest::TempDir dir;
    EditorDocument d;
    make(d, ObjectKind::Cube);
    const std::string authored = snap(d.scene());
    KCHECK(d.play().ok());
    advance_play_demo(*d.play_scene(), 2.0);
    KCHECK(d.save_as(dir / "s.kscene").ok());
    KCHECK(d.stop().ok());
    EditorDocument e;
    KCHECK(e.open(dir / "s.kscene").ok());
    KCHECK_EQ(snap(e.scene()), authored);
}

// ---------------------------------------------------------------- explorer ---

KTEST(explorer_rows_follow_hierarchy_order_and_depth) {
    Scene s;
    auto root = s.create(ObjectKind::Empty, "root").value();
    auto c1 = s.create(ObjectKind::Cube, "c1", root).value();
    auto g = s.create(ObjectKind::Sphere, "g", c1).value();
    auto c2 = s.create(ObjectKind::Plane, "c2", root).value();
    auto other = s.create(ObjectKind::Cube, "other").value();
    const auto rows = build_explorer_rows(s);
    KCHECK_EQ(rows.size(), static_cast<std::size_t>(5));
    if (rows.size() != 5) return;
    const char* names[] = {"root", "c1", "g", "c2", "other"};
    const int depths[] = {0, 1, 2, 1, 0};
    for (std::size_t i = 0; i < 5; ++i) {
        KCHECK_EQ(rows[i].name, std::string(names[i]));
        KCHECK_EQ(rows[i].depth, depths[i]);
    }
    KCHECK(rows[0].parent.is_nil() && rows[4].parent.is_nil());
    KCHECK(rows[1].parent == rows[0].id && rows[2].parent == rows[1].id && rows[3].parent == rows[0].id);
    KCHECK_EQ(explorer_label(rows[2]), std::string("g  [sphere]"));
    KCHECK(build_explorer_rows(Scene{}).empty());
    (void)c2; (void)g; (void)other;
}

KTEST(explorer_rows_handle_very_deep_hierarchies) {
    Scene s;
    ObjectHandle cur = s.create(ObjectKind::Empty, "n").value();
    const int n = static_cast<int>(ktest::scaled(5000, 50000));
    for (int i = 1; i < n; ++i) cur = s.create(ObjectKind::Empty, "n", cur).value();
    const auto rows = build_explorer_rows(s);
    KCHECK_EQ(rows.size(), static_cast<std::size_t>(n));
    KCHECK_EQ(rows.back().depth, n - 1);
}

// --------------------------------------------------------------- inspector ---

KTEST(inspector_replaces_exactly_one_axis_and_clamps) {
    Transform t;
    t.position = {1.23456789, 2, 3};
    t.rotation_euler_deg = {10, 20, 30};
    t.scale = {1, 2, 3};
    const Transform p = with_axis(t, TransformField::Position, 1, 7.5);
    KCHECK(p.position == (Vec3{1.23456789, 7.5, 3}));          // the displayed-but-rounded axes keep full precision
    KCHECK(p.rotation_euler_deg == t.rotation_euler_deg && p.scale == t.scale);
    KCHECK(with_axis(t, TransformField::Rotation, 2, -45).rotation_euler_deg == (Vec3{10, 20, -45}));
    KCHECK(with_axis(t, TransformField::Scale, 0, 5).scale == (Vec3{5, 2, 3}));
    for (int axis = 0; axis < 3; ++axis) {
        for (auto f : {TransformField::Position, TransformField::Rotation, TransformField::Scale}) {
            KCHECK_EQ(axis_value(with_axis(t, f, axis, 4.0), f, axis), 4.0);
        }
    }
    // Invalid axis: unchanged.
    KCHECK(with_axis(t, TransformField::Position, 3, 9) == t);
    KCHECK(with_axis(t, TransformField::Position, -1, 9) == t);
    KCHECK_EQ(axis_value(t, TransformField::Position, 5), 0.0);
    // Clamping: NaN, infinities, huge values, non-positive scale.
    KCHECK_EQ(clamp_for_field(TransformField::Position, std::nan("")), 0.0);
    KCHECK_EQ(clamp_for_field(TransformField::Position, 1e30), kInspectorMaxAbs);
    KCHECK_EQ(clamp_for_field(TransformField::Position, -INFINITY), -kInspectorMaxAbs);
    KCHECK_EQ(clamp_for_field(TransformField::Scale, 0.0), kInspectorMinScale);
    KCHECK_EQ(clamp_for_field(TransformField::Scale, -5.0), kInspectorMinScale);
    KCHECK_EQ(clamp_for_field(TransformField::Scale, std::nan("")), kInspectorMinScale);
    KCHECK_EQ(clamp_for_field(TransformField::Rotation, -720.0), -720.0);   // rotation is not wrapped or limited to 360
    KCHECK(is_finite(with_axis(t, TransformField::Scale, 1, std::nan(""))));
}

// ---------------------------------------------------------------- play demo ---

KTEST(play_demo_spins_cubes_and_spheres_only_and_wraps) {
    Scene s;
    auto cube = s.create(ObjectKind::Cube, "c").value();
    auto sphere = s.create(ObjectKind::Sphere, "s").value();
    auto plane = s.create(ObjectKind::Plane, "p").value();
    auto empty = s.create(ObjectKind::Empty, "e").value();
    advance_play_demo(s, 2.0);
    KCHECK_EQ(s.get(cube)->transform.rotation_euler_deg[1], 90.0);
    KCHECK_EQ(s.get(sphere)->transform.rotation_euler_deg[1], 90.0);
    KCHECK_EQ(s.get(plane)->transform.rotation_euler_deg[1], 0.0);
    KCHECK_EQ(s.get(empty)->transform.rotation_euler_deg[1], 0.0);
    KCHECK_EQ(s.get(cube)->transform.rotation_euler_deg[0], 0.0);   // only Y changes
    advance_play_demo(s, 7.0);                                     // 90 + 315 = 405 -> 45
    KCHECK(std::fabs(s.get(cube)->transform.rotation_euler_deg[1] - 45.0) < 1e-9);
    // Invalid steps change nothing.
    const std::string before = snap(s);
    advance_play_demo(s, 0.0);
    advance_play_demo(s, -1.0);
    advance_play_demo(s, std::nan(""));
    advance_play_demo(s, INFINITY);
    KCHECK_EQ(snap(s), before);
    advance_play_demo(s, 1e9);                                     // huge dt stays finite and inside [0, 360)
    const double y = s.get(cube)->transform.rotation_euler_deg[1];
    KCHECK(std::isfinite(y) && y >= 0.0 && y < 360.0);
}

// ------------------------------------------------------------ camera framing ---

namespace {
// True if all 8 bounding-box corners of every drawable object are inside the view frustum.
bool everything_visible(const OrbitCamera& cam, const Scene& s, double aspect) {
    const Mat4 vp = cam.projection(aspect) * cam.view();
    const auto worlds = compute_world_matrices(s);
    bool ok = true;
    s.visit_preorder([&](ObjectHandle h, const SceneObject& o) {
        if (o.kind == ObjectKind::Empty) return;
        for (int i = 0; i < 8; ++i) {
            const Vec3 corner{(i & 1) ? 0.5 : -0.5, (i & 2) ? 0.5 : -0.5, (i & 4) ? 0.5 : -0.5};
            const Vec3 world = worlds.at(h.index).transform_point(corner);
            if (vp.transform_point_w(world) <= 0.0) { ok = false; continue; }          // behind the camera
            const Vec3 ndc = vp.transform_point_divided(world);
            if (std::fabs(ndc[0]) > 1.0 || std::fabs(ndc[1]) > 1.0 || ndc[2] < 0.0 || ndc[2] > 1.0) ok = false;
        }
    });
    return ok;
}
}  // namespace

KTEST(camera_framing_centres_and_fits_everything_for_any_window_shape) {
    Scene empty;
    OrbitCamera c;
    c.target = {5, 5, 5};
    c.distance = 99;
    frame_scene(c, empty);
    KCHECK(length(c.target) == 0.0 && c.distance == 6.0);
    // Only Empty objects: nothing to frame.
    Scene only_empty;
    only_empty.create(ObjectKind::Empty, "e").ok();
    frame_scene(c, only_empty);
    KCHECK(length(c.target) == 0.0 && c.distance == 6.0);

    Scene s;
    auto a = s.create(ObjectKind::Cube, "a").value();
    auto b = s.create(ObjectKind::Sphere, "b").value();
    auto ground = s.create(ObjectKind::Plane, "g").value();
    auto child_parent = s.create(ObjectKind::Empty, "p").value();
    auto child = s.create(ObjectKind::Cube, "k", child_parent).value();
    Transform t;
    t.position = {10, 0, 0};
    (void)s.set_transform(a, t);
    t.position = {-6, 3, 4};
    t.rotation_euler_deg = {20, 40, 60};
    t.scale = {2, 1, 3};
    (void)s.set_transform(b, t);
    t = Transform{};
    t.position = {0, -1, 0};
    t.scale = {30, 1, 30};
    (void)s.set_transform(ground, t);
    t = Transform{};
    t.position = {0, 8, -12};
    (void)s.set_transform(child_parent, t);
    t.position = {1, 1, 1};
    (void)s.set_transform(child, t);

    for (double aspect : {0.4, 1.0, 1.6, 3.0}) {
        OrbitCamera cam;
        cam.yaw_deg = 20 + aspect * 30;
        cam.pitch_deg = 15 + aspect * 10;
        frame_scene(cam, s, aspect);
        if (!everything_visible(cam, s, aspect)) ktest::fail(__FILE__, __LINE__, "not everything is visible at aspect " + std::to_string(aspect));
        KCHECK(cam.distance > OrbitCamera::kMinDistance && cam.distance < OrbitCamera::kMaxDistance);
        // Framing is tight, not absurdly far: the bounding sphere's radius is within ~2x of the view.
        OrbitCamera tight = cam;
        tight.distance *= 0.5;
        KCHECK(!everything_visible(tight, s, aspect));
    }
    // A single cube at the origin is centred.
    Scene one;
    one.create(ObjectKind::Cube, "c").ok();
    OrbitCamera cam;
    frame_scene(cam, one);
    KCHECK(length(cam.target) < 1e-12);
    KCHECK(everything_visible(cam, one, 1.0));
    // Bad aspect values are treated as 1.
    OrbitCamera cam2;
    frame_scene(cam2, one, -3.0);
    KCHECK_EQ(cam2.distance, cam.distance);
}

// ------------------------------------------------- Hello Cube, no GUI (M05-T09 core) ---

KTEST(hello_cube_workflow_create_transform_save_reopen_undo_play_stop) {
    ktest::TempDir dir;
    EditorDocument d;

    // 1. Create a cube, a sphere and a plane (the UI's three Create buttons).
    const Uuid cube = make(d, ObjectKind::Cube);
    const Uuid sphere = make(d, ObjectKind::Sphere);
    const Uuid plane = make(d, ObjectKind::Plane);
    KCHECK_EQ(d.scene().object_count(), static_cast<std::size_t>(3));
    KCHECK(d.selected() == plane);

    // 2. Select the cube via a viewport click (CPU picking) and drag-transform it.
    {
        // Put the cube where the camera looks, then click on it through the real camera + picking path.
        KCHECK(d.select(cube).ok());
        KCHECK(d.set_selected_transform(at(0, 0, 0)).ok());
        KCHECK(d.select(sphere).ok());
        KCHECK(d.set_selected_transform(at(3, 0, 0)).ok());
        KCHECK(d.select(plane).ok());
        Transform ground;
        ground.position = {0, -0.5, 0};
        ground.scale = {10, 1, 10};
        KCHECK(d.set_selected_transform(ground).ok());
        OrbitCamera cam;
        cam.yaw_deg = 0; cam.pitch_deg = 0; cam.distance = 8;
        const auto ray = cam.screen_ray(400, 300, 800, 600);      // centre of the viewport
        KCHECK(ray.valid);
        const auto hit = pick(d.scene(), ray.ray);
        KCHECK(hit.has_value());
        if (hit) {
            KCHECK(d.select(d.scene().get(hit->object)->id).ok());
            KCHECK(d.selected() == cube);
        }
    }
    for (int i = 1; i <= 5; ++i) KCHECK(d.set_selected_transform(at(i * 0.5, 1, 0), true).ok());
    d.end_edit_gesture();
    KCHECK(d.rename_selected("Hero").ok());
    const std::string authored = snap(d.scene());

    // 3. Save, reopen in a fresh document: UUIDs, hierarchy, transforms are all preserved.
    KCHECK(d.save_as(dir / "hello.kscene").ok());
    KCHECK(!d.is_dirty());
    EditorDocument reopened;
    KCHECK(reopened.open(dir / "hello.kscene").ok());
    KCHECK_EQ(snap(reopened.scene()), authored);
    KCHECK(!reopened.scene().find_by_uuid(cube).is_null());   // the same UUIDs
    KCHECK_EQ(reopened.scene().get(reopened.scene().find_by_uuid(cube))->name, std::string("Hero"));

    // 4. Undo / redo all the way and back.
    int undos = 0;
    while (d.can_undo()) { KCHECK(d.undo().ok()); ++undos; }
    // 3 creates + 3 placement transforms + 1 merged drag + 1 rename
    KCHECK_EQ(undos, 3 + 3 + 1 + 1);
    KCHECK_EQ(d.scene().object_count(), static_cast<std::size_t>(0));
    KCHECK(d.is_dirty());
    while (d.can_redo()) KCHECK(d.redo().ok());
    KCHECK_EQ(snap(d.scene()), authored);
    KCHECK(!d.is_dirty());                                    // back at the saved state

    // 5. Play: the demo spins the cube in the copy; Stop restores exactly the authored scene.
    KCHECK(d.play().ok());
    for (int frame = 0; frame < 60; ++frame) advance_play_demo(*d.play_scene(), 1.0 / 60.0);
    KCHECK(snap(d.scene()) != authored);                      // the shown (play) scene changed
    KCHECK_EQ(snap(d.editor().scene()), authored);            // the authoring scene did not
    KCHECK(d.stop().ok());
    KCHECK_EQ(snap(d.scene()), authored);
    KCHECK(!d.is_dirty());                                    // Play never dirties the document
}
