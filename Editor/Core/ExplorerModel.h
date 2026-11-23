// K Engine — Editor/Core/ExplorerModel.h
// Flat, ordered rows for the scene Explorer tree (Qt-free; the UI turns rows into tree items).
#pragma once

#include <vector>

#include "Engine/Scene/Scene.h"

namespace k {

struct ExplorerRow {
    Uuid id;
    Uuid parent;  // nil for root objects
    std::string name;
    ObjectKind kind = ObjectKind::Empty;
    int depth = 0;
};

// Hierarchy preorder (parents before children, siblings in order). O(n) at any depth.
std::vector<ExplorerRow> build_explorer_rows(const Scene& scene);

// Display label: "Cube  [cube]".
std::string explorer_label(const ExplorerRow& row);

// Where a drag-and-drop in the Explorer puts the dragged object, relative to the row it was dropped on.
enum class DropPosition {
    OnItem,       // as the last child of the target row
    AboveItem,    // just before the target row, among the target's siblings
    BelowItem,    // just after the target row
    OnEmptySpace, // below all rows: as the last root object (the target is ignored)
};

struct DropPlacement {
    Uuid parent;                              // nil = root
    std::size_t sibling_index = Scene::kAppend;  // position among the new siblings, counted AFTER the dragged object is removed
};

// Turns a drop into a (parent, index) that EditorDocument::reparent() understands, including the
// "counted after removal" shift when the object moves down inside its own parent. Dropping an object
// above or below itself resolves to its current place (a no-op). Errors: command.object_not_found
// (dragged or target missing), scene.cycle (OnItem onto itself). Does not check the other cycle cases
// (target inside the dragged subtree); EditorDocument::can_reparent() does.
Result<DropPlacement> resolve_drop(const Scene& scene, const Uuid& dragged, const Uuid& target, DropPosition position);

}  // namespace k
