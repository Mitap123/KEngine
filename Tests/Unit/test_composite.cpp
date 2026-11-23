// Composite commands and Duplicate.
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Editor/Core/EditorDocument.h"
#include "Engine/Editing/CommandHistory.h"
#include "Engine/Editing/SceneCommands.h"
#include "Engine/Editing/SceneEditor.h"
#include "Engine/Scene/SceneTransforms.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {

std::string snap(const Scene& s) { return save_scene_to_string(s).value(); }
std::string code(const Status& st) { return st.ok() ? std::string() : st.error().code; }
const SceneObject* find(const Scene& s, const Uuid& id) { return s.get(s.find_by_uuid(id)); }

Transform at(double x, double y = 0, double z = 0) {
    Transform t;
    t.position = {x, y, z};
    return t;
}

Uuid make(SceneEditor& ed, const char* name, const Uuid& parent = Uuid(), Transform t = {}) {
    auto r = ed.create(ObjectKind::Cube, name, parent);
    KCHECK(r.ok());
    if (!r.ok()) return Uuid();
    if (!(t == Transform{})) KCHECK(ed.set_transform(r.value(), t).ok());
    ed.end_edit_gesture();
    return r.value();
}

std::string children_names(const Scene& s, const Uuid& parent) {
    std::string r;
    const std::vector<ObjectHandle>& kids = parent.is_nil() ? s.roots() : find(s, parent)->children;
    for (ObjectHandle h : kids) r += s.get(h)->name + ",";
    return r;
}

std::size_t count(const Scene& s) {
    std::size_t n = 0;
    s.visit_preorder([&](ObjectHandle, const SceneObject&) { ++n; });
    return n;
}

}  // namespace

// --------------------------------------------------------------- composite ---

KTEST(composite_is_one_undo_step_and_redo_reproduces_the_same_state) {
    Scene scene;
    CommandHistory hist;
    const std::string empty = snap(scene);
    const Uuid a = Uuid::from_random_words(1, 1), b = Uuid::from_random_words(2, 2);
    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(make_create_command(a, ObjectKind::Cube, "A", Uuid()));
    steps.push_back(make_create_command(b, ObjectKind::Sphere, "B", a));
    steps.push_back(make_rename_command(b, "Bee"));
    KCHECK(hist.execute(scene, make_composite_command("Make pair", std::move(steps))).ok());
    KCHECK_EQ(hist.undo_depth(), static_cast<std::size_t>(1));
    KCHECK_EQ(hist.undo_name().value_or(""), std::string("Make pair"));
    const std::string full = snap(scene);
    KCHECK(find(scene, b) != nullptr && find(scene, b)->name == "Bee");

    KCHECK(hist.undo(scene).ok());
    KCHECK_EQ(snap(scene), empty);                 // all three steps undone by one undo
    KCHECK(hist.redo(scene).ok());
    KCHECK_EQ(snap(scene), full);
}

KTEST(composite_apply_failure_rolls_back_every_earlier_step) {
    Scene scene;
    CommandHistory hist;
    KCHECK(hist.execute(scene, make_create_command(Uuid::from_random_words(9, 9), ObjectKind::Empty, "keep", Uuid())).ok());
    const std::string before = snap(scene);
    const std::size_t depth = hist.undo_depth();

    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(make_create_command(Uuid::from_random_words(3, 3), ObjectKind::Cube, "X", Uuid()));
    steps.push_back(make_create_command(Uuid::from_random_words(4, 4), ObjectKind::Cube, "Y", Uuid()));
    steps.push_back(make_rename_command(Uuid::from_random_words(5, 5), "ghost"));   // no such object
    KCHECK_EQ(code(hist.execute(scene, make_composite_command("Bad", std::move(steps)))), std::string("command.object_not_found"));
    KCHECK_EQ(snap(scene), before);                 // X and Y are gone again
    KCHECK_EQ(hist.undo_depth(), depth);            // no history entry
    KCHECK(!hist.can_redo());
}

KTEST(composite_undo_failure_restores_the_scene_when_it_was_changed_behind_the_history) {
    Scene scene;
    CommandHistory hist;
    const Uuid a = Uuid::from_random_words(1, 1), b = Uuid::from_random_words(2, 2);
    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(make_create_command(a, ObjectKind::Cube, "A", Uuid()));
    steps.push_back(make_create_command(b, ObjectKind::Cube, "B", Uuid()));
    steps.push_back(make_rename_command(b, "Bee"));
    KCHECK(hist.execute(scene, make_composite_command("Pair", std::move(steps))).ok());

    KCHECK(scene.destroy(scene.find_by_uuid(a)).ok());       // tampering: the history does not know
    const std::string tampered = snap(scene);
    KCHECK_EQ(code(hist.undo(scene)), std::string("command.object_not_found"));   // reverting "create A" fails
    KCHECK_EQ(snap(scene), tampered);                        // steps already undone were put back
    KCHECK(hist.can_undo());                                 // history kept the command
}

KTEST(composite_empty_list_is_rejected_without_a_history_entry) {
    Scene scene;
    CommandHistory hist;
    KCHECK_EQ(code(hist.execute(scene, make_composite_command("Nothing", {}))), std::string("command.empty_composite"));
    KCHECK(!hist.can_undo());
}

// --------------------------------------------------------------- duplicate ---

KTEST(duplicate_copies_a_subtree_with_new_uuids_and_places_it_after_the_original) {
    SceneEditor ed;
    const Uuid first = make(ed, "first");
    const Uuid root = make(ed, "root", Uuid(), at(1, 2, 3));
    const Uuid c1 = make(ed, "c1", root, at(4));
    const Uuid c2 = make(ed, "c2", root);
    const Uuid gc = make(ed, "gc", c1, at(0, 5));
    const Uuid last = make(ed, "last");
    const std::string s0 = snap(ed.scene());
    KCHECK_EQ(children_names(ed.scene(), Uuid()), std::string("first,root,last,"));

    auto copy = ed.duplicate(root, "root copy");
    KCHECK(copy.ok());
    if (!copy.ok()) return;
    KCHECK_EQ(children_names(ed.scene(), Uuid()), std::string("first,root,root copy,last,"));   // right after the original
    KCHECK_EQ(count(ed.scene()), static_cast<std::size_t>(10));

    const SceneObject* cp = find(ed.scene(), copy.value());
    KCHECK(cp && cp->name == "root copy" && cp->transform == at(1, 2, 3) && cp->kind == ObjectKind::Cube);
    KCHECK_EQ(children_names(ed.scene(), copy.value()), std::string("c1,c2,"));
    if (!cp || cp->children.size() != 2) return;                // fail cleanly rather than index into nothing
    const SceneObject* cc1 = ed.scene().get(cp->children[0]);
    KCHECK(cc1->transform == at(4));
    if (cc1->children.size() != 1) { KCHECK(false); return; }
    KCHECK_EQ(ed.scene().get(cc1->children[0])->name, std::string("gc"));
    KCHECK(ed.scene().get(cc1->children[0])->transform == at(0, 5));

    std::set<std::string> uuids;                              // every UUID in the scene is distinct
    ed.scene().visit_preorder([&](ObjectHandle, const SceneObject& o) { uuids.insert(o.id.to_string()); });
    KCHECK_EQ(uuids.size(), static_cast<std::size_t>(10));
    KCHECK(copy.value() != root && cc1->id != c1);

    // The copy's grandchild sits in the same world place as the original grandchild.
    const Mat4 w_orig = world_matrix(ed.scene(), ed.scene().find_by_uuid(gc)).value();
    KCHECK(nearly_equal(world_matrix(ed.scene(), cc1->children[0]).value(), w_orig, 1e-12));

    // One undo step, labelled; undo/redo are exact and redo keeps the same UUIDs.
    KCHECK_EQ(ed.undo_name().value_or(""), std::string("Duplicate 'root'"));
    const std::string after = snap(ed.scene());
    KCHECK(ed.undo().ok());
    KCHECK_EQ(snap(ed.scene()), s0);
    KCHECK(ed.redo().ok());
    KCHECK_EQ(snap(ed.scene()), after);
    (void)first; (void)c2; (void)last;
}

KTEST(duplicate_of_a_last_child_and_of_a_nested_object_stays_under_the_same_parent) {
    SceneEditor ed;
    const Uuid p = make(ed, "P");
    const Uuid a = make(ed, "a", p);
    const Uuid b = make(ed, "b", p);                       // last child: no reorder step needed
    KCHECK(ed.duplicate(b, "b2").ok());
    KCHECK_EQ(children_names(ed.scene(), p), std::string("a,b,b2,"));
    KCHECK(ed.duplicate(a, "a2").ok());                    // not last: lands between a and b
    KCHECK_EQ(children_names(ed.scene(), p), std::string("a,a2,b,b2,"));
    KCHECK(ed.undo().ok());
    KCHECK(ed.undo().ok());
    KCHECK_EQ(children_names(ed.scene(), p), std::string("a,b,"));
}

KTEST(duplicate_failures_change_nothing) {
    SceneEditor ed;
    const Uuid a = make(ed, "a");
    const std::string before = snap(ed.scene());
    auto missing = ed.duplicate(Uuid::from_random_words(7, 7), "x");
    KCHECK(!missing.ok());
    if (!missing.ok()) KCHECK_EQ(missing.error().code, std::string("command.object_not_found"));
    KCHECK_EQ(snap(ed.scene()), before);
    KCHECK(ed.begin_play().ok());
    auto refused = ed.duplicate(a, "x");
    KCHECK(!refused.ok());
    if (!refused.ok()) KCHECK_EQ(refused.error().code, std::string("editor.read_only_while_playing"));
    KCHECK(ed.end_play().ok());
    KCHECK_EQ(snap(ed.scene()), before);
}

KTEST(duplicate_randomized_keeps_unique_uuids_and_undoes_exactly) {
    SceneEditor ed;
    std::vector<Uuid> ids;
    std::uint64_t seed = 0xD1B54A32D192ED03ull;
    auto rnd = [&seed](std::uint32_t n) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>((seed >> 33) % n);
    };
    for (int i = 0; i < 12; ++i) {
        const Uuid parent = (ids.empty() || rnd(3) == 0) ? Uuid() : ids[rnd(static_cast<std::uint32_t>(ids.size()))];
        ids.push_back(make(ed, "o", parent, at(static_cast<double>(rnd(100)), static_cast<double>(rnd(100)))));
    }
    const int steps = static_cast<int>(ktest::scaled(60, 200));
    for (int i = 0; i < steps; ++i) {
        const std::string before = snap(ed.scene());
        const std::size_t n_before = count(ed.scene());
        const Uuid pick = ids[rnd(static_cast<std::uint32_t>(ids.size()))];
        auto copy = ed.duplicate(pick, "c");
        KCHECK(copy.ok());
        if (!copy.ok()) return;
        std::set<std::string> uuids;
        ed.scene().visit_preorder([&](ObjectHandle, const SceneObject& o) { uuids.insert(o.id.to_string()); });
        KCHECK_EQ(uuids.size(), count(ed.scene()));
        KCHECK(count(ed.scene()) > n_before);
        const std::string after = snap(ed.scene());
        KCHECK(ed.undo().ok());
        KCHECK_EQ(snap(ed.scene()), before);
        KCHECK(ed.redo().ok());
        KCHECK_EQ(snap(ed.scene()), after);
        if (ids.size() < 40) ids.push_back(copy.value());      // copies can be duplicated and nested further
        if (count(ed.scene()) > 400) break;
    }
}

// ---------------------------------------------------------------- document ---

KTEST(document_duplicate_selects_the_copy_and_numbers_its_name) {
    EditorDocument d;
    const Uuid a = d.create(ObjectKind::Cube).value();          // "Cube"
    KCHECK(d.select(a).ok());
    auto c2 = d.duplicate_selected();
    KCHECK(c2.ok());
    KCHECK(d.selected() == c2.value());
    KCHECK_EQ(find(d.scene(), c2.value())->name, std::string("Cube 2"));
    auto c3 = d.duplicate_selected();                           // duplicating "Cube 2" gives "Cube 3", not "Cube 2 2"
    KCHECK(c3.ok());
    KCHECK_EQ(find(d.scene(), c3.value())->name, std::string("Cube 3"));
    KCHECK_EQ(d.undo_label(), std::string("Undo Duplicate 'Cube 2'"));

    KCHECK(d.select(a).ok());
    KCHECK(d.rename_selected("My 7 Thing").ok());              // only a trailing number counts
    auto c4 = d.duplicate_selected();
    KCHECK(c4.ok());
    KCHECK_EQ(find(d.scene(), c4.value())->name, std::string("My 7 Thing 2"));   // the base is the whole name; it is taken
}

KTEST(document_duplicate_errors_and_play) {
    EditorDocument d;
    auto none = d.duplicate_selected();
    KCHECK(!none.ok());
    if (!none.ok()) KCHECK_EQ(none.error().code, std::string("document.no_selection"));
    const Uuid a = d.create(ObjectKind::Sphere).value();
    KCHECK(d.play().ok());
    auto playing = d.duplicate_selected();
    KCHECK(!playing.ok());
    if (!playing.ok()) KCHECK_EQ(playing.error().code, std::string("editor.read_only_while_playing"));
    KCHECK(d.stop().ok());
    KCHECK(d.selected() == a);
    KCHECK_EQ(count(d.scene()), static_cast<std::size_t>(1));
}
