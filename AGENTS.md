# K Engine — Agent Operating Rules

## Mission
Build K Engine incrementally as a reliable, modular game engine and Editor. Read the active task before editing. The detailed product/architecture specification is in `K_ENGINE_ARCHITECTURE_ROADMAP_REFERENCE_V2.md`; consult only relevant sections.

## Always do
1. Inspect the repository, build environment and existing implementation before changing files.
2. Preserve working code and unrelated user changes.
3. Work on one bounded task at a time with explicit acceptance criteria.
4. Build and run the narrowest relevant tests after each coherent change.
5. Diagnose actual command output; never invent test results or benchmarks.
6. Update `DEVELOPMENT_STATUS.md`, task status and `FEATURE_MATRIX.md` after work.
7. Keep documentation aligned with implemented behavior; label planned features as planned.
8. Ask only when a missing decision is truly blocking; otherwise choose and document a reversible assumption.

## Architectural invariants
- Keep Editor authoring data separate from Runtime execution/ECS storage.
- Define and test the authoring-to-runtime bridge; reflection metadata is not ECS storage.
- Persistent UUIDs and asset GUIDs are distinct from live generational handles.
- Destroyed/stale handles must never resolve to a reused object.
- Never expose raw pointers, C++ layouts, STL types or Rust-native layouts across the stable C ABI.
- No C++ exception or Rust panic crosses the C ABI.
- Python callbacks obey the documented GIL and thread-affinity policy; ordinary gameplay callbacks are main/runtime-thread-only by default.
- Scene/project/asset/save formats are versioned; never serialize raw memory layouts.
- Play Mode operates on isolated runtime state; Stop does not silently overwrite authored scenes.
- Do not claim cross-platform determinism, full rewind, sandboxing or platform support without proof.

## Implementation discipline
- No fake UI, stub success path or unimplemented command presented as functional.
- No speculative large-scale refactor or dependency addition without justification.
- No long-running work on the Qt UI thread without a documented reason.
- IPC is disabled by default, local-only by default, authenticated and capability-scoped.
- Python and in-process native plugins are not automatically sandboxes.
- Tests skipped due to environment limits must be reported, not hidden.

## Verified commands (this repository)
```bash
make STRICT=1 check               # build (warnings = errors) + unit tests + CLI tests
make SANITIZE=1 STRICT=1 check    # same under ASan + UBSan
make BUILD_TYPE=Release check
Build/make-debug/k_unit_tests <substring>   # run matching unit cases (--list to list)
```
CMake is written but UNVERIFIED (no cmake in the original dev environment); see BUILDING.md and ADR-0009. Do not claim a command works unless it appears here or in BUILDING.md/TESTING.md.

## Conventions
- C++20, no exceptions for expected failures: return `Result<T>`/`Status` with a stable `code` (match on code in tests, never on message).
- `Engine/*` must not include Qt or Editor headers. Editor-only code goes under `Editor/`.
- Includes are repo-root relative (`#include "Engine/Scene/Scene.h"`).
- New source files under `Engine/<Module>/*.cpp` and `Tests/Unit/*.cpp` are picked up by glob (Make and CMake); no list to edit.
- Prefer failure-path tests. Do not weaken a test to pass without recording the decision (ADR).
- Never serialize Handles or addresses. Only UUIDs are persistent.

## Hardware floor and platform rules
- **Supported platforms (owner decision): Windows 10 and Windows 11, x64.** Linux is used only as a convenient environment for the portable, Qt-free core and its tests; it is not a product platform. K Engine's own license is deliberately undecided (deferred): do not add a LICENSE file.
- Target hardware floor (owner requirement): **Direct3D 10.1** class GPU (Intel Core i5-560M iGPU). Renderer: Direct3D 11 API at feature level 10_1 (ADR-0004). Shaders Shader Model 4.0 only; no compute/tessellation/MSAA by default; render on demand.
- `Engine/*` is portable and API-neutral; renderers consume only `DrawList`. Only `Editor/` may use Windows/Direct3D/Qt. Use Qt **Widgets** only (no Qt Quick/QRhi/QOpenGLWidget).
- Math/coordinate conventions are fixed by ADR-0010 (right-handed, +Y up, row-major `Mat4`, column vectors, Euler `Ry*Rx*Rz`, local transforms, CCW front faces, depth [0,1]). Do not change them without an ADR and a schema bump.
- Windows/Direct3D/Qt code in this repo is UNVERIFIED until the owner runs it; never describe it as working.

## Known open risks (details in ADRs)
- Handles are domain-typed, but two scenes share a handle type (ADR-0003): use UUIDs across scenes.
- Renderer/Qt path unverified on real hardware (ADR-0004); Qt licensing undecided (ADR-0005); project license not chosen.

## Required project records
Maintain `ARCHITECTURE.md`, `ROADMAP.md`, `FEATURE_MATRIX.md`, `DEVELOPMENT_STATUS.md`, `DEPENDENCIES.md`, `BUILDING.md`, `TESTING.md`, ADRs and `TASKS.json`/`TASKS.md` as relevant. Keep this file concise (under 150 lines); put details in those documents.

## Session start
Read this file, `DEVELOPMENT_STATUS.md`, the active task, and only the relevant architecture/ADR sections. Inspect actual source before relying on documentation.

## Session end
Record the exact task, changed files, build/test commands and results, tests not run and why, known risks, and the next smallest task.

## Definition of done
Acceptance criteria met; relevant targets build; relevant tests pass or skipped checks are explicitly recorded; public API/data-format changes are documented/versioned; task and feature status reflect real evidence.
