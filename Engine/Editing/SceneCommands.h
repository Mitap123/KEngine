// K Engine — Engine/Editing/SceneCommands.h
//
// Concrete undoable scene edits. All take persistent UUIDs. The nil UUID as a
// parent means "root". Error codes: command.object_not_found (an object UUID
// is not in the scene), plus the scene.* codes of the underlying Scene call.
#pragma once

#include <memory>
#include <string>

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

// Moves the object under `new_parent` (nil = root) as its last child. Undo
// restores the old parent and the old sibling position.
std::unique_ptr<Command> make_reparent_command(const Uuid& id, const Uuid& new_parent);

}  // namespace k
