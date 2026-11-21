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

}  // namespace k
