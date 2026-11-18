# Architecture (current state)

Describes code that EXISTS. Planned parts are labelled **(planned)**. Decisions: `Documentation/ADR/`.

## Layers
```
Tools/CLI  (k)  ─┐
Tests/*          ├──►  Engine/*  (C++20, no Qt, no third-party deps)
Editor/ (planned, Qt, Editor-only) ─┘
```

## Modules that exist
| Module | Path | Responsibility |
|---|---|---|
| Core | `Engine/Core` | `Result<T>`/`Status`/`Error` (stable string `code` + message), version constant |
| Identity | `Engine/Identity` | `Uuid` (persistent identity), `Handle` + `SlotMap<T>` (generational runtime references). ADR-0003 |
| Scene | `Engine/Scene` | **Authoring** `Scene`: objects with UUID, name, kind (empty/cube/sphere/plane), Euler transform, ordered hierarchy. Create/destroy (subtree)/reparent (cycle-safe)/rename/set transform. Not thread-safe |
| Serialization | `Engine/Serialization` | Strict `Json`; `SceneSerializer` (scene ⇄ JSON, format `kengine.scene` v1); `SceneFile` (atomic save with backup, bounded load; POSIX-verified). ADR-0006 |
| CLI | `Tools/CLI` | `k --version`, `k --help`, `k doctor [--json]` (build facts + engine self-checks) |

## Invariants enforced by code and tests
- UUIDs are unique per scene and never nil; handles never resolve after destroy/slot reuse; generation exhaustion retires a slot.
- Mutating APIs return `Status`/`Result` with stable codes; invalid handles are errors, not UB.
- Scene files never contain handles; loading validates schema, version, UUIDs, parents and cycles.
- Deep hierarchies do not recurse on the call stack (iterative traversal/destroy; tested at depth 100,000).

## Planned (not implemented)
- Runtime `World`/ECS, scheduler, Play isolation, headless stepping (M1; ADR-0001).
- Qt Editor shell, viewport/renderer, Undo/Redo commands, project creation (M0.5; ADR-0004/0005).
- Reflection/Inspector metadata (ADR-0002); C ABI, Python/C++/Rust bindings (ADR-0007); assets, build/export, simulation (M3–M6).

## Known architectural risks
See ADR-0003 ("handles don't name their container") and ADR-0004 (renderer unverified).
