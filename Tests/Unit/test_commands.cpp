#include <memory>
#include <string>
#include <vector>

#include "Engine/Editing/CommandHistory.h"
#include "Engine/Editing/SceneCommands.h"
#include "Engine/Editing/SceneEditor.h"
#include "Tests/Unit/TempDir.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {

std::string snap(const Scene& s) { return save_scene_to_string(s).value(); }

Uuid make(SceneEditor& ed, ObjectKind kind, const char* name, const Uuid& parent = Uuid()) {
    auto r = ed.create(kind, name, parent);
    KCHECK(r.ok());
    return r.ok() ? r.value() : Uuid();
}

Transform at(double x, double y = 0, double z = 0) {
    Transform t;
    t.position = {x, y, z};
    return t;
}

std::string code(const Status& st) { return st.ok() ? std::string() : st.error().code; }

const SceneObject* find(const Scene& s, const Uuid& id) { return s.get(s.find_by_uuid(id)); }

}  // namespace

// ------------------------------------------------------------ single edits ---

KTEST(commands_create_undo_redo_keeps_the_same_uuid) {
    SceneEditor ed;
    const std::string empty = snap(ed.scene());
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    const std::string with_a = snap(ed.scene());
    KCHECK(find(ed.scene(), a) != nullptr);
    KCHECK_EQ(ed.undo_name().value_or(""), std::string("Create 'A'"));

    KCHECK(ed.undo().ok());
    KCHECK(find(ed.scene(), a) == nullptr);
    KCHECK_EQ(snap(ed.scene()), empty);
    KCHECK(ed.can_redo() && !ed.can_undo());
    KCHECK_EQ(ed.redo_name().value_or(""), std::string("Create 'A'"));

    KCHECK(ed.redo().ok());
    KCHECK(find(ed.scene(), a) != nullptr);   // same UUID after redo
    KCHECK_EQ(snap(ed.scene()), with_a);      // byte-identical state
}

KTEST(commands_delete_subtree_undo_restores_exact_state_including_sibling_order) {
    SceneEditor ed;
    const Uuid first_root = make(ed, ObjectKind::Cube, "first");
    const Uuid root = make(ed, ObjectKind::Empty, "root");
    const Uuid c1 = make(ed, ObjectKind::Cube, "c1", root);
    const Uuid c2 = make(ed, ObjectKind::Sphere, "c2", root);
    const Uuid gc = make(ed, ObjectKind::Plane, "gc", c1);
    const Uuid c3 = make(ed, ObjectKind::Cube, "c3", root);
    const Uuid last_root = make(ed, ObjectKind::Cube, "last");
    KCHECK(ed.set_transform(c1, at(1, 2, 3)).ok());
    KCHECK(ed.set_transform(gc, at(-4, 5, 6)).ok());
    KCHECK(ed.set_transform(c2, at(7)).ok());
    (void)first_root; (void)c3; (void)last_root;
    const std::string s0 = snap(ed.scene());

    // Middle child with a grandchild.
    KCHECK(ed.destroy(c1).ok());
    KCHECK(find(ed.scene(), c1) == nullptr && find(ed.scene(), gc) == nullptr);
    KCHECK(find(ed.scene(), c2) != nullptr);
    const std::string s1 = snap(ed.scene());
    KCHECK(s1 != s0);
    KCHECK(ed.undo().ok());
    KCHECK_EQ(snap(ed.scene()), s0);          // UUIDs, transforms, order, hierarchy all restored
    KCHECK(ed.redo().ok());
    KCHECK_EQ(snap(ed.scene()), s1);
    KCHECK(ed.undo().ok());

    // A root in the middle of the root list, with its whole subtree.
    KCHECK(ed.destroy(root).ok());
    KCHECK_EQ(ed.scene().object_count(), static_cast<std::size_t>(2));
    KCHECK(ed.undo().ok());
    KCHECK_EQ(snap(ed.scene()), s0);

    // Two deletions undone in reverse order.
    KCHECK(ed.destroy(c2).ok());
    KCHECK(ed.destroy(root).ok());
    KCHECK(ed.undo().ok());
    KCHECK(ed.undo().ok());
    KCHECK_EQ(snap(ed.scene()), s0);
}

KTEST(commands_set_transform_undo_redo_and_merging) {
    SceneEditor ed;
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    make(ed, ObjectKind::Cube, "B");

    // A drag: many merge-enabled edits collapse into ONE undo step.
    for (int i = 1; i <= 10; ++i) KCHECK(ed.set_transform(a, at(i), /*merge=*/true).ok());
    ed.end_edit_gesture();
    KCHECK(find(ed.scene(), a)->transform == at(10));
    KCHECK(ed.undo().ok());
    KCHECK(find(ed.scene(), a)->transform == Transform{});     // back to before the whole drag
    KCHECK(ed.redo().ok());
    KCHECK(find(ed.scene(), a)->transform == at(10));          // redo reapplies the final value

    // Two gestures stay two steps.
    KCHECK(ed.set_transform(a, at(20), true).ok());
    KCHECK(ed.set_transform(a, at(21), true).ok());
    ed.end_edit_gesture();
    KCHECK(ed.set_transform(a, at(30), true).ok());
    KCHECK(ed.undo().ok());
    KCHECK(find(ed.scene(), a)->transform == at(21));
    KCHECK(ed.undo().ok());
    KCHECK(find(ed.scene(), a)->transform == at(10));

    // Without the merge flag every edit is its own step; different objects never merge.
    SceneEditor ed2;
    const Uuid p = make(ed2, ObjectKind::Cube, "P");
    const Uuid q = make(ed2, ObjectKind::Cube, "Q");
    KCHECK(ed2.set_transform(p, at(1)).ok());
    KCHECK(ed2.set_transform(p, at(2)).ok());
    KCHECK(ed2.undo().ok());
    KCHECK(find(ed2.scene(), p)->transform == at(1));
    KCHECK(ed2.set_transform(p, at(5), true).ok());
    KCHECK(ed2.set_transform(q, at(6), true).ok());            // other object: new step
    KCHECK(ed2.undo().ok());
    KCHECK(find(ed2.scene(), q)->transform == Transform{} && find(ed2.scene(), p)->transform == at(5));

    // An undo in between ends the merge run.
    KCHECK(ed2.set_transform(q, at(7), true).ok());
    KCHECK(ed2.undo().ok());
    KCHECK(ed2.set_transform(q, at(8), true).ok());
    KCHECK(ed2.set_transform(q, at(9), true).ok());
    KCHECK(ed2.undo().ok());
    KCHECK(find(ed2.scene(), q)->transform == Transform{});
}

KTEST(commands_rename_and_reparent_undo_restore_previous_state) {
    SceneEditor ed;
    const Uuid p = make(ed, ObjectKind::Empty, "P");
    const Uuid a = make(ed, ObjectKind::Cube, "A", p);
    const Uuid b = make(ed, ObjectKind::Cube, "B", p);
    const Uuid c = make(ed, ObjectKind::Cube, "C", p);
    const Uuid other = make(ed, ObjectKind::Empty, "Other");
    const std::string s0 = snap(ed.scene());

    KCHECK(ed.rename(b, "Bee").ok());
    KCHECK_EQ(find(ed.scene(), b)->name, std::string("Bee"));
    KCHECK_EQ(ed.undo_name().value_or(""), std::string("Rename 'B'"));
    KCHECK(ed.undo().ok());
    KCHECK_EQ(snap(ed.scene()), s0);

    KCHECK(ed.reparent(a, other).ok());                      // first child moves to another parent
    KCHECK(find(ed.scene(), a)->parent == ed.scene().find_by_uuid(other));
    KCHECK(ed.undo().ok());
    KCHECK_EQ(snap(ed.scene()), s0);                         // back under P at index 0, not appended
    KCHECK(ed.redo().ok());
    KCHECK(ed.undo().ok());
    KCHECK_EQ(snap(ed.scene()), s0);

    KCHECK(ed.reparent(b, Uuid()).ok());                     // to root
    KCHECK_EQ(ed.scene().roots().size(), static_cast<std::size_t>(3));
    KCHECK(ed.undo().ok());
    KCHECK_EQ(snap(ed.scene()), s0);
    (void)c;
}

KTEST(commands_failures_change_nothing_and_leave_no_history_entry) {
    SceneEditor ed;
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    const Uuid b = make(ed, ObjectKind::Cube, "B", a);
    KCHECK(ed.undo().ok());                                  // leave a redo entry to prove it survives failures
    KCHECK(ed.redo().ok());
    KCHECK(ed.undo().ok());
    const std::string before = snap(ed.scene());
    const auto undo_name = ed.undo_name();
    KCHECK(ed.can_redo());

    const Uuid ghost = Uuid::from_random_words(1, 2);
    KCHECK_EQ(code(ed.destroy(ghost)), std::string("command.object_not_found"));
    KCHECK_EQ(code(ed.destroy(Uuid())), std::string("command.object_not_found"));
    KCHECK_EQ(code(ed.set_transform(ghost, at(1))), std::string("command.object_not_found"));
    KCHECK_EQ(code(ed.rename(ghost, "x")), std::string("command.object_not_found"));
    KCHECK_EQ(code(ed.reparent(ghost, Uuid())), std::string("command.object_not_found"));
    auto bad_parent = ed.create(ObjectKind::Cube, "x", ghost);
    KCHECK(!bad_parent.ok());
    if (!bad_parent.ok()) KCHECK_EQ(bad_parent.error().code, std::string("command.object_not_found"));

    KCHECK_EQ(snap(ed.scene()), before);
    KCHECK(ed.undo_name() == undo_name);
    KCHECK(ed.can_redo());                                   // a FAILED edit must not discard the redo branch
    KCHECK(ed.redo().ok());                                  // still works: restores B under A
    KCHECK(find(ed.scene(), a) != nullptr && find(ed.scene(), b) != nullptr);

    // Scene-level validation errors surface unchanged.
    Transform bad;
    bad.scale = {std::nan(""), 1, 1};
    KCHECK_EQ(code(ed.set_transform(a, bad)), std::string("scene.non_finite_transform"));
    KCHECK(find(ed.scene(), a)->transform == Transform{});
    // Cycle: cannot make a parent a child of its own descendant.
    KCHECK_EQ(code(ed.reparent(a, b)), std::string("scene.cycle"));
    KCHECK(find(ed.scene(), a)->parent.is_null());
}

KTEST(commands_new_edit_discards_the_redo_branch) {
    SceneEditor ed;
    make(ed, ObjectKind::Cube, "A");
    make(ed, ObjectKind::Cube, "B");
    KCHECK(ed.undo().ok());
    KCHECK(ed.can_redo());
    make(ed, ObjectKind::Cube, "C");
    KCHECK(!ed.can_redo());
    KCHECK_EQ(code(ed.redo()), std::string("history.nothing_to_redo"));
    KCHECK(ed.undo().ok());
    KCHECK(ed.undo().ok());
    KCHECK(!ed.can_undo());
    KCHECK_EQ(code(ed.undo()), std::string("history.nothing_to_undo"));
    KCHECK_EQ(ed.scene().object_count(), static_cast<std::size_t>(0));
}

// -------------------------------------------------------- clean / dirty state ---

KTEST(history_dirty_tracking_follows_the_saved_state) {
    ktest::TempDir dir;
    SceneEditor ed;
    KCHECK(!ed.is_dirty());
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    KCHECK(ed.is_dirty());
    KCHECK(ed.undo().ok());
    KCHECK(!ed.is_dirty());                                  // back at the (empty) clean state
    KCHECK(ed.redo().ok());
    KCHECK(ed.is_dirty());

    KCHECK(ed.save(dir / "s.kscene").ok());
    KCHECK(!ed.is_dirty());
    KCHECK(ed.rename(a, "A2").ok());
    KCHECK(ed.is_dirty());
    KCHECK(ed.undo().ok());
    KCHECK(!ed.is_dirty());                                  // undo to the saved state => clean again
    KCHECK(ed.undo().ok());
    KCHECK(ed.is_dirty());                                   // before the saved state => dirty
    KCHECK(ed.redo().ok());
    KCHECK(!ed.is_dirty());
}

KTEST(history_saved_state_on_a_discarded_redo_branch_stays_dirty_forever) {
    ktest::TempDir dir;
    SceneEditor ed;
    make(ed, ObjectKind::Cube, "A");
    KCHECK(ed.save(dir / "s.kscene").ok());                  // saved: {A}
    KCHECK(ed.undo().ok());                                  // {}
    make(ed, ObjectKind::Cube, "B");                         // new branch {B}: same history depth as the saved state
    KCHECK(ed.is_dirty());                                   // must NOT look clean just because the depth matches
    KCHECK(ed.undo().ok());
    KCHECK(ed.is_dirty());                                   // the saved {A} state can no longer be reached
    KCHECK(ed.redo().ok());
    KCHECK(ed.is_dirty());
}

KTEST(history_failed_save_keeps_the_document_dirty) {
    ktest::TempDir dir;
    SceneEditor ed;
    make(ed, ObjectKind::Cube, "A");
    KCHECK(!ed.save(dir / "no_such_dir" / "s.kscene").ok());
    KCHECK(ed.is_dirty());
}

KTEST(history_edit_after_save_is_never_merged_into_the_saved_step) {
    ktest::TempDir dir;
    SceneEditor ed;
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    KCHECK(ed.set_transform(a, at(1), true).ok());           // merge run is open...
    KCHECK(ed.save(dir / "s.kscene").ok());                  // ...saving closes it
    KCHECK(ed.set_transform(a, at(2), true).ok());           // must be a NEW step, not folded into the saved one
    KCHECK(ed.is_dirty());
    KCHECK(ed.undo().ok());
    KCHECK(!ed.is_dirty());                                  // exactly the saved state
    KCHECK(find(ed.scene(), a)->transform == at(1));
}

KTEST(history_merge_request_right_after_mark_clean_starts_a_new_step) {
    // Raw history: mark_clean() closes the merge run, so even an edit that asks to merge
    // becomes its own step and one undo returns exactly to the saved state.
    Scene scene;
    CommandHistory h;
    const Uuid id = Uuid::from_random_words(3, 4);
    KCHECK(h.execute(scene, make_create_command(id, ObjectKind::Cube, "A", Uuid())).ok());
    KCHECK(h.execute(scene, make_set_transform_command(id, at(1)), true).ok());
    h.mark_clean();
    KCHECK(h.is_clean());
    KCHECK(h.execute(scene, make_set_transform_command(id, at(2)), true).ok());
    KCHECK(!h.is_clean());
    KCHECK_EQ(h.undo_depth(), static_cast<std::size_t>(3));   // create + first transform + a NEW step
    KCHECK(h.undo(scene).ok());
    KCHECK(h.is_clean());
    KCHECK(scene.get(scene.find_by_uuid(id))->transform == at(1));
}

KTEST(history_max_depth_trims_oldest_steps_and_invalidates_unreachable_saved_state) {
    Scene scene;
    CommandHistory h(3);
    KCHECK(h.is_clean());                                    // fresh: clean at position 0
    for (std::uint64_t i = 1; i <= 5; ++i) {
        KCHECK(h.execute(scene, make_create_command(Uuid::from_random_words(i, i), ObjectKind::Cube, "o", Uuid())).ok());
    }
    KCHECK_EQ(h.undo_depth(), static_cast<std::size_t>(3));
    KCHECK(h.undo(scene).ok());
    KCHECK(h.undo(scene).ok());
    KCHECK(h.undo(scene).ok());
    KCHECK_EQ(code(h.undo(scene)), std::string("history.nothing_to_undo"));
    KCHECK_EQ(scene.object_count(), static_cast<std::size_t>(2));  // two steps were trimmed away
    KCHECK(!h.is_clean());                                   // the original empty state fell off the bottom

    // A saved state that is still inside the window stays reachable.
    Scene scene2;
    CommandHistory g(3);
    for (std::uint64_t i = 1; i <= 4; ++i) KCHECK(g.execute(scene2, make_create_command(Uuid::from_random_words(i, 9), ObjectKind::Cube, "o", Uuid())).ok());
    g.mark_clean();
    KCHECK(g.execute(scene2, make_create_command(Uuid::from_random_words(99, 9), ObjectKind::Cube, "o", Uuid())).ok());
    KCHECK(!g.is_clean());
    KCHECK(g.undo(scene2).ok());
    KCHECK(g.is_clean());
}

KTEST(history_rejects_null_commands_and_survives_external_scene_changes) {
    Scene scene;
    CommandHistory h;
    KCHECK_EQ(code(h.execute(scene, nullptr)), std::string("history.null_command"));
    KCHECK(!h.can_undo());

    // The scene is modified behind the history's back: undo must report an error and keep the step.
    const Uuid id = Uuid::from_random_words(5, 5);
    KCHECK(h.execute(scene, make_create_command(id, ObjectKind::Cube, "A", Uuid())).ok());
    KCHECK(scene.destroy(scene.find_by_uuid(id)).ok());
    KCHECK_EQ(code(h.undo(scene)), std::string("command.object_not_found"));
    KCHECK(h.can_undo());
    KCHECK(!h.can_redo());
}

KTEST(history_undo_of_a_delete_rolls_back_cleanly_if_restoring_conflicts) {
    Scene scene;
    CommandHistory h;
    const Uuid parent = Uuid::from_random_words(10, 1), child = Uuid::from_random_words(10, 2);
    KCHECK(h.execute(scene, make_create_command(parent, ObjectKind::Empty, "P", Uuid())).ok());
    KCHECK(h.execute(scene, make_create_command(child, ObjectKind::Cube, "C", parent)).ok());
    KCHECK(h.execute(scene, make_delete_command(parent)).ok());
    KCHECK_EQ(scene.object_count(), static_cast<std::size_t>(0));

    // Someone re-uses the child's UUID while it is deleted.
    KCHECK(scene.create_with_uuid(child, ObjectKind::Sphere, "squatter").ok());
    const std::string before = snap(scene);
    KCHECK_EQ(code(h.undo(scene)), std::string("scene.duplicate_uuid"));
    KCHECK_EQ(snap(scene), before);                           // the partially restored parent was rolled back
    KCHECK(h.can_undo());
}

// ---------------------------------------------------------------- file I/O ---

KTEST(editor_open_and_save_round_trip) {
    ktest::TempDir dir;
    SceneEditor ed;
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    make(ed, ObjectKind::Sphere, "B", a);
    KCHECK(ed.save(dir / "s.kscene").ok());

    std::vector<std::string> warnings{"stale"};
    auto reopened = SceneEditor::open(dir / "s.kscene", &warnings);
    KCHECK(reopened.ok());
    if (!reopened.ok()) return;
    KCHECK(warnings.empty());
    KCHECK_EQ(snap(reopened.value().scene()), snap(ed.scene()));
    KCHECK(!reopened.value().is_dirty());                     // a freshly opened document is clean
    KCHECK(!reopened.value().can_undo());                     // and has no history

    auto missing = SceneEditor::open(dir / "nope.kscene");
    KCHECK(!missing.ok());
    if (!missing.ok()) KCHECK_EQ(missing.error().code, std::string("file.not_found"));
}

// ------------------------------------------------------ randomized model check ---

// Applies random edits/undos/redos and compares the serialized scene with a
// trivially-correct reference (a list of snapshots) after EVERY step. Because
// the snapshot includes UUIDs, names, transforms, hierarchy and sibling order,
// any imperfect undo shows up as a mismatch.
KTEST(editor_randomized_undo_redo_matches_snapshot_model) {
    SceneEditor ed;
    std::vector<std::string> states{snap(ed.scene())};
    std::size_t cursor = 0;
    std::uint64_t seed = 0x2545F4914F6CDD1Dull;
    auto rnd = [&seed](std::uint32_t n) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>((seed >> 33) % n);
    };
    auto all_ids = [&ed] {
        std::vector<Uuid> ids;
        ed.scene().visit_preorder([&](ObjectHandle, const SceneObject& o) { ids.push_back(o.id); });
        return ids;
    };

    std::size_t edits_ok = 0, edits_rejected = 0, undos = 0, redos = 0;
    for (int step = 0; step < 1500; ++step) {
        const auto ids = all_ids();
        auto pick_id = [&] { return ids.empty() ? Uuid::from_random_words(7, 7) : ids[rnd(static_cast<std::uint32_t>(ids.size()))]; };
        auto pick_parent = [&] { return (ids.empty() || rnd(3) == 0) ? Uuid() : pick_id(); };

        // 40% edits, 30% undo, 30% redo. A high edit rate keeps discarding the redo
        // branch, which would leave redo (and undo of old steps) barely exercised.
        const std::uint32_t roll = rnd(20);
        std::uint32_t op = roll < 8 ? roll : (roll < 14 ? 8u : 9u);  // 0..7 = create x2, destroy, transform x2, rename, reparent x2
        if (cursor >= 900) op = 8;                            // stay inside the 1000-step history window
        Status st;
        bool is_edit = true;
        switch (op) {
            case 0: case 1: {
                const ObjectKind kinds[] = {ObjectKind::Empty, ObjectKind::Cube, ObjectKind::Sphere, ObjectKind::Plane};
                auto r = ed.create(kinds[rnd(4)], "obj" + std::to_string(step), pick_parent());
                st = r.ok() ? Status::success() : Status(r.error());
                break;
            }
            case 2: st = ed.destroy(pick_id()); break;
            case 3: case 4: st = ed.set_transform(pick_id(), at(rnd(100) * 0.25, rnd(10), -1.0 * rnd(7))); break;
            case 5: st = ed.rename(pick_id(), "renamed" + std::to_string(step)); break;
            case 6: case 7: st = ed.reparent(pick_id(), pick_parent()); break;   // may legitimately fail (cycle, self)
            case 8: {
                is_edit = false;
                const bool could = cursor > 0;
                st = ed.undo();
                KCHECK_EQ(st.ok(), could);
                if (st.ok()) { --cursor; ++undos; }
                break;
            }
            default: {
                is_edit = false;
                const bool could = cursor + 1 < states.size();
                st = ed.redo();
                KCHECK_EQ(st.ok(), could);
                if (st.ok()) { ++cursor; ++redos; }
                break;
            }
        }
        if (is_edit) {
            if (st.ok()) {
                states.resize(cursor + 1);                    // the redo branch is gone
                states.push_back(snap(ed.scene()));
                ++cursor;
                ++edits_ok;
            } else {
                ++edits_rejected;                             // rejected edits must leave everything alone
            }
        }
        if (snap(ed.scene()) != states[cursor]) {
            ktest::fail(__FILE__, __LINE__, "scene differs from the reference at step " + std::to_string(step) + " (op " + std::to_string(op) + ")");
            return;
        }
        KCHECK_EQ(ed.can_undo(), cursor > 0);
        KCHECK_EQ(ed.can_redo(), cursor + 1 < states.size());
        KCHECK_EQ(ed.is_dirty(), cursor != 0);                // clean state is the initial empty scene
    }
    // The run must actually have exercised every path, or the check is vacuous.
    KCHECK(edits_ok > 200);
    KCHECK(edits_rejected > 5);
    KCHECK(undos > 100);
    KCHECK(redos > 20);
}

// ------------------------------------------------------------------- Play ---

KTEST(play_runtime_changes_never_touch_the_authoring_scene) {
    SceneEditor ed;
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    const Uuid b = make(ed, ObjectKind::Sphere, "B", a);
    KCHECK(ed.set_transform(a, at(1, 2, 3)).ok());
    const std::string authored = snap(ed.scene());
    const bool dirty_before = ed.is_dirty();
    KCHECK(dirty_before);

    KCHECK(ed.begin_play().ok());
    KCHECK(ed.playing());
    Scene* play = ed.play_scene();
    KCHECK(play != nullptr);
    if (!play) return;
    KCHECK(&ed.active_scene() == play);                       // the viewport draws the play copy
    KCHECK_EQ(snap(*play), authored);                         // Play starts from the current authored state

    // The "game" moves, renames, creates and destroys things in its copy.
    KCHECK(play->set_transform(play->find_by_uuid(a), at(100, 100, 100)).ok());
    KCHECK(play->set_name(play->find_by_uuid(b), "mutated").ok());
    KCHECK(play->create(ObjectKind::Plane, "spawned").ok());
    KCHECK(play->destroy(play->find_by_uuid(b)).ok());
    KCHECK(snap(*play) != authored);

    KCHECK_EQ(snap(ed.scene()), authored);                    // authoring scene untouched while running
    KCHECK(ed.end_play().ok());
    KCHECK(!ed.playing() && ed.play_scene() == nullptr);
    KCHECK(&ed.active_scene() == &ed.scene());
    KCHECK_EQ(snap(ed.scene()), authored);                    // ...and after Stop
    KCHECK_EQ(ed.is_dirty(), dirty_before);                   // Play does not change the dirty flag

    // The undo history is intact: it still walks back through the authored steps.
    KCHECK(ed.undo().ok());
    KCHECK(find(ed.scene(), a)->transform == Transform{});
    KCHECK(ed.redo().ok());
    KCHECK_EQ(snap(ed.scene()), authored);
}

KTEST(play_blocks_authoring_edits_and_undo_redo) {
    SceneEditor ed;
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    KCHECK(ed.undo().ok());                                   // leaves a redo entry
    const std::string before = snap(ed.scene());
    KCHECK(ed.begin_play().ok());

    const std::string blocked = "editor.read_only_while_playing";
    KCHECK_EQ(code(ed.destroy(a)), blocked);
    KCHECK_EQ(code(ed.set_transform(a, at(1))), blocked);
    KCHECK_EQ(code(ed.rename(a, "x")), blocked);
    KCHECK_EQ(code(ed.reparent(a, Uuid())), blocked);
    KCHECK_EQ(code(ed.undo()), blocked);
    KCHECK_EQ(code(ed.redo()), blocked);
    auto created = ed.create(ObjectKind::Cube, "x");
    KCHECK(!created.ok());
    if (!created.ok()) KCHECK_EQ(created.error().code, blocked);
    KCHECK(!ed.can_undo() && !ed.can_redo());                 // the UI must not offer them
    KCHECK(!ed.undo_name().has_value());
    KCHECK_EQ(snap(ed.scene()), before);

    KCHECK(ed.end_play().ok());
    KCHECK(ed.can_redo());                                    // redo branch survived the Play session
    KCHECK(ed.redo().ok());
    KCHECK(find(ed.scene(), a) != nullptr);
}

KTEST(play_state_machine_and_repeated_sessions_start_fresh) {
    SceneEditor ed;
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    KCHECK_EQ(code(ed.end_play()), std::string("editor.not_playing"));
    KCHECK(ed.begin_play().ok());
    KCHECK_EQ(code(ed.begin_play()), std::string("editor.already_playing"));
    KCHECK(ed.play_scene()->set_transform(ed.play_scene()->find_by_uuid(a), at(5)).ok());
    KCHECK(ed.end_play().ok());
    KCHECK_EQ(code(ed.end_play()), std::string("editor.not_playing"));

    // A second session does not see the first session's changes.
    KCHECK(ed.begin_play().ok());
    KCHECK(ed.play_scene()->get(ed.play_scene()->find_by_uuid(a))->transform == Transform{});
    KCHECK(ed.end_play().ok());
}

KTEST(play_starts_from_unsaved_edits_and_save_writes_the_authoring_scene) {
    ktest::TempDir dir;
    SceneEditor ed;
    const Uuid a = make(ed, ObjectKind::Cube, "A");          // never saved
    KCHECK(ed.begin_play().ok());
    KCHECK(ed.play_scene()->find_by_uuid(a).index == ed.scene().find_by_uuid(a).index);
    KCHECK(!ed.play_scene()->find_by_uuid(a).is_null());      // unsaved object is in the play copy

    KCHECK(ed.play_scene()->set_name(ed.play_scene()->find_by_uuid(a), "PLAY-ONLY").ok());
    const std::string authored = snap(ed.scene());
    KCHECK(ed.save(dir / "s.kscene").ok());                   // allowed while playing: saves the authoring scene
    KCHECK(!ed.is_dirty());
    auto loaded = load_scene_file(dir / "s.kscene");
    KCHECK(loaded.ok());
    if (loaded.ok()) KCHECK_EQ(snap(loaded.value().scene), authored);
    KCHECK(save_scene_to_string(loaded.value().scene).value().find("PLAY-ONLY") == std::string::npos);
    KCHECK(ed.end_play().ok());
}

KTEST(play_copy_is_independent_in_both_directions) {
    SceneEditor ed;
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    KCHECK(ed.begin_play().ok());
    Scene* play = ed.play_scene();
    // Destroying everything in the copy leaves the source fully usable.
    KCHECK(play->destroy(play->find_by_uuid(a)).ok());
    KCHECK_EQ(play->object_count(), static_cast<std::size_t>(0));
    KCHECK_EQ(ed.scene().object_count(), static_cast<std::size_t>(1));
    KCHECK(ed.scene().get(ed.scene().find_by_uuid(a)) != nullptr);
    KCHECK(ed.end_play().ok());
}

KTEST(play_repeated_sessions_leave_the_authoring_state_and_history_untouched) {
    SceneEditor ed;
    const Uuid a = make(ed, ObjectKind::Cube, "A");
    const Uuid b = make(ed, ObjectKind::Sphere, "B", a);
    KCHECK(ed.set_transform(a, at(1, 2, 3)).ok());
    const std::string authored = snap(ed.scene());
    for (int i = 0; i < 500; ++i) {
        KCHECK(ed.begin_play().ok());
        Scene* play = ed.play_scene();
        // Each session starts from the authored state, whatever the previous one did.
        if (snap(*play) != authored) { ktest::fail(__FILE__, __LINE__, "session " + std::to_string(i) + " did not start from the authored state"); return; }
        KCHECK(play->set_transform(play->find_by_uuid(a), at(i)).ok());
        for (int k = 0; k < 5; ++k) KCHECK(play->create(ObjectKind::Cube, "spawn").ok());
        if (i % 3 == 0) KCHECK(play->destroy(play->find_by_uuid(b)).ok());
        KCHECK(ed.end_play().ok());
    }
    KCHECK_EQ(snap(ed.scene()), authored);
    // History still intact after 500 sessions (leaks/stale state would be reported by ASan/LSan runs).
    KCHECK(ed.undo().ok());
    KCHECK(ed.undo().ok());
    KCHECK(ed.undo().ok());
    KCHECK(!ed.can_undo());
    KCHECK_EQ(ed.scene().object_count(), static_cast<std::size_t>(0));
}
