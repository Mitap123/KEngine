// K Engine — Engine/Scene/SceneTransforms.h
// Local/world matrices for the authoring scene (ADR-0010). A Transform is
// local to the parent; world = parent_world * local. No recursion: hierarchy
// depth cannot exhaust the call stack.
#pragma once

#include <optional>
#include <unordered_map>

#include "Engine/Math/Mat4.h"
#include "Engine/Scene/Scene.h"

namespace k {

Mat4 local_matrix(const Transform& t);

// Inverse of local_matrix(): splits an affine matrix into position, Euler angles
// (degrees, R = Ry*Rx*Rz) and per-axis scale (T*R*S). A reflection is represented as a
// negative X scale. Returns nullopt if the matrix cannot be represented that way (it has
// shear or perspective, a zero-length axis, or non-finite values): this is checked by
// recomposing and comparing, not assumed. Euler angles are normalised to X in [-90,90]; at
// X = +-90 (gimbal lock) Z is set to 0. The result is equivalent to `m`, not necessarily the
// same numbers a user typed.
std::optional<Transform> decompose_trs(const Mat4& m);

// World matrix of one object, or nullopt for an invalid handle. O(depth).
std::optional<Mat4> world_matrix(const Scene& scene, ObjectHandle h);

// World matrices of every object in one preorder pass (O(n)); keyed by the
// handle's slot index (valid only for this scene state).
std::unordered_map<std::uint32_t, Mat4> compute_world_matrices(const Scene& scene);

}  // namespace k
