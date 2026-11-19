# ADR-0010: Math and coordinate conventions

- **Status:** accepted (implemented; covered by unit tests)
- **Date:** 2026-11-19

## Context
Transforms, cameras, picking, serialization and every renderer must agree on handedness, matrix layout, rotation order and depth range. Scene format v1 previously said the Euler order was "not yet normative"; it must be fixed before files are authored by users.

## Decision
| Topic | Convention |
|---|---|
| Handedness / up | Right-handed, **+Y up**. Camera looks down **−Z** in view space. (Matches glTF, which the later asset pipeline will import.) |
| Matrix storage | `Mat4` is **row-major**: element (r,c) is `m[r*4+c]`; `double` on the CPU. |
| Vector math | **Column vectors**: `p' = M * p`; `A * B` applies `B` first. |
| TRS | `M = T * R * S` (scale, then rotate, then translate). |
| Euler angles | **Degrees**, `{x, y, z}`; rotation `R = Ry * Rx * Rz` (Z applied first, then X, then Y). Positive angle = counter-clockwise looking from +axis toward the origin. |
| Transforms | A scene object's transform is **local** (relative to its parent). `world = parent_world * local`. |
| Perspective | Right-handed, **clip-space depth in [0,1]** (Direct3D); near plane maps to 0, far to 1; `w = −z_view`. |
| Winding | Front faces are **counter-clockwise** seen from outside (Direct3D: `FrontCounterClockwise = TRUE`). Normals point outward. |
| GPU upload | Copy the 16 row-major floats unchanged; HLSL declares `row_major float4x4` and uses `mul(M, v)`. No transposition anywhere. |
| Precision | `double` for authoring/CPU math, `float` for GPU data. |
| Primitives | cube: edge 1 centered at origin; sphere: radius 0.5; plane: 1×1 in the XZ plane at y=0, normal +Y, rendered double-sided. Scale via the transform. |
| Editor camera | Orbit camera: eye = target + d·(cosφ sinθ, sinφ, cosφ cosθ) with yaw θ, pitch φ (clamped ±89°). |

## Options considered
Left-handed Y-up (Unity/Direct3D native): rejected to avoid mirrored imports from glTF. Z-up (Blender/CAD): rejected, Y-up is the common game-engine default. Euler order XYZ extrinsic: rejected in favour of Ry·Rx·Rz so that yaw (Y) is outermost, the usual editor/camera behaviour (Godot's default order).

## Evidence (verified)
`test_math.cpp` (rotation handedness, Euler order sensitivity, TRS order, multiplication order, inverse round trips including a tiny scale far from the origin, look-at, perspective depth and FOV), `test_rendering_geometry.cpp` (winding vs normals, closed meshes, world matrices, row-major draw-list layout), `test_camera_picking.cpp` (camera formulas, pan signs, screen rays round-trip, picking under scale/rotation/parents). 12 deliberate defects (e.g. wrong Euler order, flipped winding, wrong depth range, parent transform ignored) were each caught by these tests.

## Trade-offs
Euler angles have gimbal lock at pitch ±90° and are order-dependent; acceptable for authoring in the Inspector. The Runtime may store quaternions; conversion is a later task and must respect this order. Local-vs-world semantics are now fixed in the file format (v1 had not shipped, so no version bump).

## Consequences
`Documentation/Formats/scene-v1.md` states the Euler order and local-transform semantics as normative. Any new renderer or importer must pass the same conventions or convert at its boundary.

## Rollback
Changing any row is a format-affecting change: requires a new `schema_version` and a migration step.
