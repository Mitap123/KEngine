# ADR-0001: Authoring/Runtime layers and ECS

- **Status:** accepted for the M0.5 scope. The ECS *library* choice is **not** made here; it is deferred to M1 behind a benchmark.
- **Date:** 2026-11-18

## Context
The Editor needs a discoverable, undoable, reflectable object model (hierarchy, names, properties). The Runtime needs compact, cache-friendly storage and explicit scheduling. One structure rarely serves both well. Reference: ARCHITECTURE_ROADMAP §5.2.

## Constraints
- M0.5 (Hello Cube) needs no gameplay, physics or scripting; it needs an authoring scene, save/load, undo, and an isolated Play/Stop.
- No network in the current environment, so flecs/EnTT cannot be fetched or benchmarked now.
- Reflection must not dictate hot-path storage.

## Options
1. **Single object-oriented scene used by both Editor and Runtime.** Simple; couples Editor concerns to runtime hot paths; hard to run headless.
2. **Two layers: authoring `Scene` + runtime `World`, joined by a defined bridge.** More up-front design; clean headless/standalone story.
3. **ECS-only, Editor edits ECS directly.** Poor fit for hierarchy editing, undo and metadata.

## Decision
Option 2.
- **Authoring layer** (`Engine/Scene`, implemented): `Scene` of `SceneObject` records (UUID, name, kind, transform, hierarchy). Value type, no global singleton.
- **Runtime layer** (`Engine/Runtime`, planned M1): `World` with its own entity identity and storage. Library vs custom: decided in M1 by ADR + a representative benchmark (flecs, EnTT, custom). Until then **no ECS library is a dependency**.
- **Bridge (initial default, to be tested in M0.5-T07/M1):**
  - One `SceneObject` of kind cube/sphere/plane/empty → one runtime entity. Editor-only objects (gizmos, helpers; future) → no entity. Runtime-only entities (spawned during Play) → no `SceneObject`.
  - Component classes: *authoring-only* (editor metadata, name label), *runtime-only* (physics bodies, render proxies), *bridged* (Transform, primitive mesh kind). Bridged data is **copied** authoring→runtime at Play start; runtime changes never flow back unless the user triggers an explicit "apply".
  - Destruction: destroying a `SceneObject` destroys its descendants (implemented); a runtime entity's lifetime is owned by the World. References between layers use UUID, never raw handles from the other layer.
- **Play isolation (M0.5):** Play starts from an independent copy of the authoring scene state; Stop discards the copy. The concrete mechanism (deep copy vs serialize-reload) is chosen in M05-T07; both are testable against the same acceptance test.

## Evidence
- Verified: the authoring layer exists and passes unit tests (see TESTING.md).
- Not verified: any ECS library, any benchmark, any bridge code. These are proposals.

## Trade-offs
Two models mean a copy step and a mapping to maintain. Accepted: it buys isolation (Play can't corrupt authored data) and a Qt-free, headless runtime.

## Consequences
`Engine/Scene` must not include Qt or runtime headers. The Runtime must be buildable without the Editor.

## Migration / rollback
Because the ECS library is deferred, nothing to roll back now. If the bridge proves too costly for a measured workload, relax "one object → one entity" per object type, not globally.

## Validation plan
M05-T07 integration test (runtime edits do not touch source scene after Stop); M1 bridge tests (create/destroy/reference propagation); M1 ECS benchmark with documented hardware/workload before choosing a library.

## Open questions
Which authoring component set is exposed to the bridge in M1? Is `Scene` copy cheap enough for large scenes (measure before M3+)?
