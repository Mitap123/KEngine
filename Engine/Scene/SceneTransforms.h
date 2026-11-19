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

// World matrix of one object, or nullopt for an invalid handle. O(depth).
std::optional<Mat4> world_matrix(const Scene& scene, ObjectHandle h);

// World matrices of every object in one preorder pass (O(n)); keyed by the
// handle's slot index (valid only for this scene state).
std::unordered_map<std::uint32_t, Mat4> compute_world_matrices(const Scene& scene);

}  // namespace k
