// K Engine — Engine/Rendering/DrawList.h
//
// The bridge from the authoring Scene to any renderer: a flat list of what to
// draw, with no graphics-API types. Every renderer backend (Direct3D 11 today)
// consumes this; backends never read the Scene directly.
#pragma once

#include <vector>

#include "Engine/Scene/Scene.h"

namespace k {

struct DrawItem {
    ObjectHandle object;
    ObjectKind kind = ObjectKind::Empty;  // never Empty in a DrawList
    // World matrix, 16 floats ROW-MAJOR, column-vector convention. Upload
    // unchanged; HLSL side: `row_major float4x4`, mul(M, v). See ADR-0010.
    float world[16] = {};
    bool selected = false;
};

// One item per drawable object (Empty objects are skipped), in hierarchy
// preorder. `selected` marks at most one item (null handle = no selection).
std::vector<DrawItem> build_draw_list(const Scene& scene, ObjectHandle selected = kNullObjectHandle);

}  // namespace k
