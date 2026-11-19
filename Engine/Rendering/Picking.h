// K Engine — Engine/Rendering/Picking.h
//
// CPU ray picking against the authoring scene. Each object is tested in its
// own local space against the exact primitive (cube: AABB of half-extent 0.5,
// sphere: radius 0.5, plane: 1x1 quad at y=0, double-sided), so non-uniform
// scale and rotation are handled by transforming the ray, not the shape.
// Empty objects are not pickable. Objects whose world matrix is singular
// (for example scale 0) are skipped.
#pragma once

#include <optional>

#include "Engine/Rendering/OrbitCamera.h"
#include "Engine/Scene/Scene.h"

namespace k {

struct PickHit {
    ObjectHandle object;
    double distance = 0.0;  // along the (unit) ray direction, >= 0
};

// `ray.direction` need not be unit length; `distance` is then in units of
// that direction vector. Returns the nearest hit; ties keep the earlier
// object in hierarchy preorder. A ray origin inside a cube or sphere hits it at
// distance 0.
std::optional<PickHit> pick(const Scene& scene, const Ray& ray);

}  // namespace k
