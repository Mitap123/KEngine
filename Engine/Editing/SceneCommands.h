// K Engine — Engine/Editing/SceneCommands.h
//
// Concrete undoable scene edits. All take persistent UUIDs. The nil UUID as a
// parent means "root". Error codes: command.object_not_found (an object UUID
// is not in the scene), plus the scene.* codes of the underlying Scene call.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Engine/Editing/Command.h"

namespace k {

// Creates an object with the given UUID (so redo recreates the same object),
// appended as the last child of `parent` (nil = root).
std::unique_ptr<Command> make_create_command(const Uuid& id, ObjectKind kind, std::string name, const Uuid& parent);

// Deletes the object and its whole subtree. Undo restores every object with its
// UUID, name, kind, transform and its original position among its siblings.
std::unique_ptr<Command> make_delete_command(const Uuid& id);

// Mergeable: consecutive set-transform commands on the same object coalesce.
std::unique_ptr<Command> make_set_transform_command(const Uuid& id, const Transform& transform);

std::unique_ptr<Command> make_rename_command(const Uuid& id, std::string new_name);

// What a reparent does to the moved object's Transform (which is local to its parent).
enum class ReparentMode {
    KeepLocal,  // transform numbers unchanged; the object may visibly jump
    KeepWorld,  // transform recomputed so the object (and its subtree) stays where it is
};

// Moves the object under `new_parent` (nil = root) at `sibling_index` among the new siblings
// counted after the object has been removed from its old position (Scene::kAppend, or any
// index >= the sibling count, = last; the same parent therefore reorders). Undo restores the
// old parent, the old sibling position and, for KeepWorld, the exact old transform.
//
// KeepWorld errors (the scene is left unchanged): command.singular_parent_transform (the new
// parent's world matrix cannot be inverted, e.g. a zero scale) and
// command.world_transform_not_representable (the needed local matrix has shear, which
// position/Euler/scale cannot express: a rotated object under a non-uniformly scaled parent).
// A KeepWorld move that does not change the object's world matrix leaves its transform bytes
// untouched. Other errors: command.object_not_found, scene.cycle.
std::unique_ptr<Command> make_reparent_command(const Uuid& id, const Uuid& new_parent,
                                               std::size_t sibling_index = Scene::kAppend,
                                               ReparentMode mode = ReparentMode::KeepLocal);

// Several commands as ONE undo step, e.g. Duplicate. apply() runs the steps in order; if step i
// fails, steps i-1..0 are reverted and the original error is returned (the scene is unchanged).
// revert() reverts in reverse order; if a step cannot be reverted (the scene was changed behind
// the history's back) the steps already reverted are applied again so the scene is unchanged.
// If even that rollback fails, command.rollback_failed is returned. An empty list fails with
// command.empty_composite. Never merges. Steps keep their own contracts (apply() may be called
// again after revert(), reproducing the same result).
std::unique_ptr<Command> make_composite_command(std::string name, std::vector<std::unique_ptr<Command>> steps);

}  // namespace k
