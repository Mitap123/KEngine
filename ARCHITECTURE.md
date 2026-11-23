# Architecture (current state)

Describes code that EXISTS. Planned parts are labelled **(planned)**. Decisions: `Documentation/ADR/`.

## Layers
```
Tools/CLI  (k)  ─┐
Tests/*          ├──►  Engine/*  (C++20, no Qt, no third-party deps, portable)
Editor/Spike (Windows: Qt Widgets + Direct3D 11, UNVERIFIED) ─┘
```

## Modules that exist
| Module | Path | Responsibility |
|---|---|---|
| Core | `Engine/Core` | `Result<T>`/`Status`/`Error` (stable string `code` + message), version constant |
| Identity | `Engine/Identity` | `Uuid` (persistent identity), `Handle` + `SlotMap<T>` (generational runtime references). ADR-0003 |
| Scene | `Engine/Scene` | **Authoring** `Scene`: objects with UUID, name, kind (empty/cube/sphere/plane), Euler transform, ordered hierarchy. Create/destroy (subtree)/reparent (cycle-safe)/rename/set transform. Not thread-safe |
| Serialization | `Engine/Serialization` | Strict `Json`; `SceneSerializer` (scene ⇄ JSON, format `kengine.scene` v1); `SceneFile` (atomic save with backup, bounded load; POSIX-verified). ADR-0006 |
| Math | `Engine/Math` | `Vec3`, row-major `Mat4` (TRS, Euler `Ry*Rx*Rz`, look-at, D3D-style perspective, inverse). ADR-0010 |
| Scene transforms | `Engine/Scene/SceneTransforms` | local/world matrices (iterative, any depth) |
| Rendering (API-neutral) | `Engine/Rendering` | primitive meshes, `OrbitCamera` (+screen rays), CPU `pick`, `DrawList` (the only thing a renderer consumes). No graphics API, no Qt |
| Editor core (Qt-free) | `Editor/Core` | `EditorDocument` (file, selection by UUID, edits, Play), Explorer rows, Inspector axis helpers, play demo, camera "Frame All", demo scene. Unit-tested. |
| Editor app (Windows, Qt 6 Widgets + Direct3D 11) | `Editor/App` | `k_editor`: MainWindow, EditorViewport. Thin binding of widgets to `EditorDocument`. **Never compiled with real Qt yet.** Renderer source shared from `Editor/Spike` |
| Editing | `Engine/Editing` | `Command`/`CommandHistory` (undo/redo, merge, dirty tracking), UUID-addressed scene commands, `SceneEditor` (document model: edits, save/open, isolated Play). The Editor UI talks only to this. ADR-0011 |
| CLI | `Tools/CLI` | `k --version`, `k --help`, `k doctor [--json]` (build facts + engine self-checks), `k scene validate|dump <file> [--json]`; `doctor` and `scene` take `--log-file <path>` (JSON Lines run log, append, flushed per line) |

## Invariants enforced by code and tests
- UUIDs are unique per scene and never nil; handles never resolve after destroy/slot reuse; generation exhaustion retires a slot.
- Mutating APIs return `Status`/`Result` with stable codes; invalid handles are errors, not UB.
- Handles are domain-typed: mixing domains is a compile error. Scene files never contain handles; loading validates schema, version, UUIDs, parents and cycles.
- Deep hierarchies do not recurse on the call stack (iterative traversal/destroy; tested at depth 100,000).

## Planned (not implemented)
- Runtime `World`/ECS, scheduler, headless stepping (M1; ADR-0001). (Play isolation of the authoring scene already exists: ADR-0011.)
- Qt Editor shell (docks, inspector, explorer) on top of `SceneEditor`, project creation (M0.5). A Direct3D 11 viewport *spike* exists but is unverified (ADR-0004/0005).
- Reflection/Inspector metadata (ADR-0002); C ABI, Python/C++/Rust bindings (ADR-0007); assets, build/export, simulation (M3–M6).

## Known architectural risks
ADR-0003: two instances of the same handle domain still share a handle type (use UUIDs across scenes). ADR-0004: the Direct3D 11 / Qt code has never been compiled or run.
