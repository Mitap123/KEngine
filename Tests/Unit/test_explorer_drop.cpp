// Explorer drag-and-drop: resolve_drop() and EditorDocument::can_drop()/reparent_by_drop().
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "Editor/Core/EditorDocument.h"
#include "Engine/Scene/SceneTransforms.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {

std::string snap(const Scene& s) { return save_scene_to_string(s).value(); }
std::string code(const Status& st) { return st.ok() ? std::string() : st.error().code; }
const SceneObject* find(const Scene& s, const Uuid& id) { return s.get(s.find_by_uuid(id)); }

Uuid mk(EditorDocument& d, const std::string& name, const Uuid& parent = Uuid()) {
    const Uuid id = d.create(ObjectKind::Cube).value();
    KCHECK(d.select(id).ok());
    KCHECK(d.rename_selected(name).ok());
    if (!parent.is_nil()) KCHECK(d.reparent(id, parent, Scene::kAppend, ReparentMode::KeepLocal).ok());
    return id;
}

std::string order(const Scene& s, const Uuid& parent) {
    std::string r;
    const std::vector<ObjectHandle>& kids = parent.is_nil() ? s.roots() : find(s, parent)->children;
    for (ObjectHandle h : kids) r += s.get(h)->name + " ";
    return r;
}

std::size_t index_of(const Scene& s, const Uuid& id) { return s.sibling_index(s.find_by_uuid(id)).value(); }
Uuid parent_of(const Scene& s, const Uuid& id) {
    const SceneObject* p = s.get(find(s, id)->parent);
    return p ? p->id : Uuid();
}

std::uint64_t g_seed = 0x2545F4914F6CDD1Dull;
std::uint32_t rnd(std::uint32_t n) {
    g_seed = g_seed * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<std::uint32_t>((g_seed >> 33) % n);
}
double frac() { return static_cast<double>(rnd(2000001)) / 1000000.0 - 1.0; }

}  // namespace

KTEST(drop_above_and_below_place_the_object_exactly_next_to_the_target) {
    // Every (source, target, side) combination of four root siblings, checked by name order.
    const char* names[] = {"a", "b", "c", "d"};
    for (int s = 0; s < 4; ++s) {
        for (int t = 0; t < 4; ++t) {
            for (DropPosition pos : {DropPosition::AboveItem, DropPosition::BelowItem}) {
                EditorDocument d;
                Uuid id[4];
                for (int i = 0; i < 4; ++i) id[i] = mk(d, names[i]);
                KCHECK(d.reparent_by_drop(id[s], id[t], pos).ok());
                // Reference: remove the source from the list, insert it next to the target.
                std::vector<std::string> ref;
                for (int i = 0; i < 4; ++i) if (i != s) ref.push_back(names[i]);
                if (s != t) {
                    std::size_t at = 0;
                    while (ref[at] != names[t]) ++at;
                    ref.insert(ref.begin() + static_cast<std::ptrdiff_t>(at + (pos == DropPosition::BelowItem ? 1 : 0)), names[s]);
                } else {
                    ref.insert(ref.begin() + s, names[s]);        // above/below itself: unchanged
                }
                std::string want;
                for (const std::string& n : ref) want += n + " ";
                KCHECK_EQ(order(d.scene(), Uuid()), want);
            }
        }
    }
}

KTEST(drop_on_an_item_appends_as_last_child_and_empty_space_makes_the_last_root) {
    EditorDocument d;
    const Uuid p = mk(d, "p");
    const Uuid x = mk(d, "x", p);
    const Uuid y = mk(d, "y");
    const Uuid z = mk(d, "z");
    KCHECK(d.reparent_by_drop(y, p, DropPosition::OnItem).ok());
    KCHECK_EQ(order(d.scene(), p), std::string("x y "));
    KCHECK(d.reparent_by_drop(x, Uuid(), DropPosition::OnEmptySpace).ok());     // target ignored
    KCHECK_EQ(order(d.scene(), Uuid()), std::string("p z x "));
    KCHECK(d.reparent_by_drop(z, y, DropPosition::AboveItem).ok());              // into another parent, before y
    KCHECK_EQ(order(d.scene(), p), std::string("z y "));
    (void)x;
}

KTEST(drop_in_place_changes_nothing_and_adds_no_undo_step) {
    EditorDocument d;
    const Uuid a = mk(d, "a"), b = mk(d, "b"), c = mk(d, "c");
    const std::string before = snap(d.scene());
    const std::string label = d.undo_label();
    KCHECK(d.reparent_by_drop(b, b, DropPosition::AboveItem).ok());
    KCHECK(d.reparent_by_drop(b, b, DropPosition::BelowItem).ok());
    KCHECK(d.reparent_by_drop(b, a, DropPosition::BelowItem).ok());              // already right after a
    KCHECK(d.reparent_by_drop(b, c, DropPosition::AboveItem).ok());              // already right before c
    KCHECK(d.reparent_by_drop(c, Uuid(), DropPosition::OnEmptySpace).ok());      // already the last root
    KCHECK_EQ(snap(d.scene()), before);
    KCHECK_EQ(d.undo_label(), label);
}

KTEST(drop_rejects_cycles_missing_objects_and_play_without_changing_anything) {
    EditorDocument d;
    const Uuid p = mk(d, "p");
    const Uuid c = mk(d, "c", p);
    const Uuid g = mk(d, "g", c);
    const Uuid ghost = Uuid::from_random_words(5, 5);
    const std::string before = snap(d.scene());

    KCHECK_EQ(code(d.can_drop(p, p, DropPosition::OnItem)), std::string("scene.cycle"));
    KCHECK_EQ(code(d.can_drop(p, g, DropPosition::OnItem)), std::string("scene.cycle"));        // into its own grandchild
    KCHECK_EQ(code(d.can_drop(p, g, DropPosition::AboveItem)), std::string("scene.cycle"));     // beside a descendant = inside p
    KCHECK_EQ(code(d.can_drop(ghost, p, DropPosition::OnItem)), std::string("command.object_not_found"));
    KCHECK_EQ(code(d.can_drop(p, ghost, DropPosition::OnItem)), std::string("command.object_not_found"));
    KCHECK(d.can_drop(ghost, Uuid(), DropPosition::OnEmptySpace).error().code == "command.object_not_found");
    KCHECK(d.can_drop(g, Uuid(), DropPosition::OnEmptySpace).ok());
    KCHECK(d.can_drop(g, p, DropPosition::BelowItem).ok());                                       // sibling of c: fine
    KCHECK_EQ(code(d.reparent_by_drop(p, g, DropPosition::OnItem)), std::string("scene.cycle"));
    KCHECK_EQ(snap(d.scene()), before);

    KCHECK(d.play().ok());
    KCHECK_EQ(code(d.can_drop(g, Uuid(), DropPosition::OnEmptySpace)), std::string("editor.read_only_while_playing"));
    KCHECK_EQ(code(d.reparent_by_drop(g, Uuid(), DropPosition::OnEmptySpace)), std::string("editor.read_only_while_playing"));
    KCHECK(d.stop().ok());
    KCHECK_EQ(snap(d.scene()), before);
}

KTEST(drop_randomized_matches_its_meaning_keeps_world_and_undoes_exactly) {
    EditorDocument d;
    std::vector<Uuid> ids;
    for (int i = 0; i < 14; ++i) {
        const Uuid parent = (ids.empty() || rnd(3) == 0) ? Uuid() : ids[rnd(static_cast<std::uint32_t>(ids.size()))];
        const Uuid id = mk(d, "n" + std::to_string(i), parent);
        const double s = 0.5 + std::fabs(frac()) * 2.0;     // uniform scale only: KeepWorld can always represent the move
        Transform t;
        t.position = {frac() * 20, frac() * 20, frac() * 20};
        t.rotation_euler_deg = {frac() * 90, frac() * 180, frac() * 90};
        t.scale = {s, s, s};
        KCHECK(d.select(id).ok());
        KCHECK(d.set_selected_transform(t).ok());
        d.end_edit_gesture();
        ids.push_back(id);
    }
    std::size_t moves = 0, rejected = 0, in_place = 0;
    const int steps = static_cast<int>(ktest::scaled(150, 500));
    for (int i = 0; i < steps; ++i) {
        const Uuid dragged = ids[rnd(static_cast<std::uint32_t>(ids.size()))];
        const Uuid target = ids[rnd(static_cast<std::uint32_t>(ids.size()))];
        const DropPosition pos = static_cast<DropPosition>(rnd(4));
        const std::string before = snap(d.scene());
        const std::string label = d.undo_label();
        std::vector<std::pair<Uuid, Mat4>> worlds;
        d.scene().visit_preorder([&](ObjectHandle h, const SceneObject& o) { worlds.emplace_back(o.id, world_matrix(d.scene(), h).value()); });

        const Status can = d.can_drop(dragged, target, pos);
        const Status did = d.reparent_by_drop(dragged, target, pos);
        KCHECK_EQ(can.ok(), did.ok());                       // the hover check agrees with the real drop
        if (!did) {
            KCHECK_EQ(code(did), std::string("scene.cycle")); // uniform scales: the only possible refusal
            KCHECK_EQ(snap(d.scene()), before);
            ++rejected;
            continue;
        }
        const Scene& sc = d.scene();
        if (pos == DropPosition::OnEmptySpace) {
            KCHECK(parent_of(sc, dragged).is_nil());
            KCHECK_EQ(index_of(sc, dragged), sc.roots().size() - 1);
        } else if (pos == DropPosition::OnItem) {
            KCHECK(parent_of(sc, dragged) == target);
            KCHECK_EQ(index_of(sc, dragged), find(sc, target)->children.size() - 1);
        } else if (dragged != target) {
            KCHECK(parent_of(sc, dragged) == parent_of(sc, target));
            if (pos == DropPosition::AboveItem) KCHECK_EQ(index_of(sc, dragged) + 1, index_of(sc, target));
            else KCHECK_EQ(index_of(sc, dragged), index_of(sc, target) + 1);
        }
        for (const auto& [oid, w] : worlds) KCHECK(nearly_equal(world_matrix(sc, sc.find_by_uuid(oid)).value(), w, 1e-6));

        const std::string after = snap(sc);
        if (after == before) {                               // a drop that changed nothing must not add an undo step
            KCHECK_EQ(d.undo_label(), label);
            ++in_place;
            continue;
        }
        ++moves;
        KCHECK(d.undo().ok());
        KCHECK_EQ(snap(d.scene()), before);
        KCHECK(d.redo().ok());
        KCHECK_EQ(snap(d.scene()), after);
    }
    KCHECK(moves > 40);                                      // the run must really exercise moves, refusals and no-ops
    KCHECK(rejected > 0);
    KCHECK(in_place > 0);
}

KTEST(resolve_drop_returns_the_documented_placements_and_errors) {
    EditorDocument d;
    const Uuid a = mk(d, "a"), b = mk(d, "b"), c = mk(d, "c");     // roots at index 0, 1, 2
    const Uuid kid = mk(d, "kid", b);                               // b/kid at index 0
    const Scene& s = d.scene();
    auto at = [&](const Uuid& drag, const Uuid& target, DropPosition p) {
        auto r = resolve_drop(s, drag, target, p);
        KCHECK(r.ok());
        return r.ok() ? r.value() : DropPlacement{};
    };
    // a (index 0) moved below c (index 2): c's index drops by one after a is removed, then +1 for "below".
    DropPlacement p = at(a, c, DropPosition::BelowItem);
    KCHECK(p.parent.is_nil());
    KCHECK_EQ(p.sibling_index, static_cast<std::size_t>(2));
    p = at(c, a, DropPosition::AboveItem);                          // moving up: no shift
    KCHECK(p.parent.is_nil());
    KCHECK_EQ(p.sibling_index, static_cast<std::size_t>(0));
    p = at(a, kid, DropPosition::AboveItem);                        // different parent: no shift, parent is kid's parent
    KCHECK(p.parent == b);
    KCHECK_EQ(p.sibling_index, static_cast<std::size_t>(0));
    p = at(c, b, DropPosition::OnItem);
    KCHECK(p.parent == b);
    KCHECK_EQ(p.sibling_index, Scene::kAppend);
    p = at(kid, a, DropPosition::OnEmptySpace);                     // the target is ignored
    KCHECK(p.parent.is_nil());
    KCHECK_EQ(p.sibling_index, Scene::kAppend);
    p = at(b, b, DropPosition::BelowItem);                          // above/below itself = where it already is
    KCHECK(p.parent.is_nil());
    KCHECK_EQ(p.sibling_index, static_cast<std::size_t>(1));
    p = at(kid, kid, DropPosition::AboveItem);
    KCHECK(p.parent == b);
    KCHECK_EQ(p.sibling_index, static_cast<std::size_t>(0));

    auto self = resolve_drop(s, a, a, DropPosition::OnItem);
    KCHECK(!self.ok());
    if (!self.ok()) KCHECK_EQ(self.error().code, std::string("scene.cycle"));
    const Uuid ghost = Uuid::from_random_words(8, 8);
    auto no_drag = resolve_drop(s, ghost, a, DropPosition::OnItem);
    auto no_target = resolve_drop(s, a, ghost, DropPosition::AboveItem);
    KCHECK(!no_drag.ok() && !no_target.ok());
    if (!no_drag.ok()) KCHECK_EQ(no_drag.error().code, std::string("command.object_not_found"));
    if (!no_target.ok()) KCHECK_EQ(no_target.error().code, std::string("command.object_not_found"));
    auto empty_ok = resolve_drop(s, a, ghost, DropPosition::OnEmptySpace);   // a missing target is irrelevant here
    KCHECK(empty_ok.ok());
}
