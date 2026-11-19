// K Engine — Engine/Rendering/Primitives.h
//
// CPU-side geometry for the built-in primitives. Pure data: no graphics API.
// Conventions (ADR-0010): right-handed, +Y up, triangles wind COUNTER-CLOCKWISE
// when viewed from outside (the front face), normals point outward.
//   cube   : unit cube centered at the origin, edges 1 (half-extent 0.5)
//   sphere : radius 0.5 centered at the origin
//   plane  : 1 x 1 in the XZ plane at y = 0, normal +Y (render double-sided)
#pragma once

#include <cstdint>
#include <vector>

#include "Engine/Scene/Scene.h"

namespace k {

struct MeshVertex {
    float position[3];
    float normal[3];
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;  // triangle list, 3 per triangle
};

MeshData make_cube();
// segments >= 3, rings >= 2 (clamped).
MeshData make_sphere(int segments = 24, int rings = 16);
MeshData make_plane();

// Geometry for a kind; Empty has none (nullptr). Built once, never freed.
const MeshData* mesh_for(ObjectKind kind);

}  // namespace k
