# Feature matrix

Statuses: PLANNED, PROTOTYPE, IMPLEMENTED, TESTED, STABLE, BLOCKED. Each status is scoped to the feature as defined in the row. Environment for all evidence: Ubuntu 24.04 x86_64, GCC 13.3.0, Debug/Release/ASan+UBSan unless stated (see DEVELOPMENT_STATUS.md).

| Feature (scope) | Status | Evidence / note |
|---|---|---|
| UUID v4 generate/parse/format | TESTED | `test_uuid.cpp` |
| Generational handles + SlotMap (incl. exhaustion retirement) | TESTED | `test_handle.cpp`, randomized model check, mutation check |
| Authoring Scene model (create/destroy/reparent/transform, in memory) | TESTED | `test_scene.cpp` |
| Strict JSON parse/write | TESTED | `test_json.cpp`; **not fuzzed**, not tested on large inputs |
| Scene format v1 serialize/load **in memory** (UUIDs, hierarchy, transforms, schema/version validation) | TESTED | `test_scene_serialization.cpp` |
| Scene file save/load on POSIX (atomic temp+fsync+rename, `.bak`, size cap, error mapping) | TESTED | `test_scene_file.cpp`; Linux/GCC only; write/fsync/backup/rename failures simulated via hook; no power-loss test |
| Scene file I/O on non-POSIX platforms (fallback code path) | PROTOTYPE | compiled only when not POSIX; never built or run |
| CLI `k --version`, `--help`, `doctor [--json]` | TESTED | `Tests/CLI/test_cli.py` |
| Make build (Debug/Release/ASan+UBSan, strict warnings) | TESTED | `make check` variants |
| CMake build | PROTOTYPE | written, never executed (no cmake available) |
| CI workflow | PLANNED | M0-T06 |
| Project creation / manifest | PLANNED | |
| Domain-typed handles (`ObjectHandle`; cross-domain use is a compile error) | TESTED | `static_assert`s in `test_handle.cpp`, compile-fail check |
| Math (Mat4/Vec3), scene local/world transforms | TESTED | `test_math.cpp`, `test_rendering_geometry.cpp` (incl. depth-100,000 chains) |
| Primitive meshes (cube/sphere/plane), orbit camera, screen rays, CPU picking, DrawList | TESTED | `test_rendering_geometry.cpp`, `test_camera_picking.cpp`; 12 mutation checks |
| Direct3D 11 renderer (feature level 10_1 target), GPU probe | PROTOTYPE | written, **never compiled/run**; stub-level syntax check only |
| Qt viewport widget + spike app (orbit/pan/zoom/select/screenshot) | PROTOTYPE | written, **never compiled/run** |
| Qt Editor shell (docks, explorer, inspector) | BLOCKED | needs the owner's Windows/Qt machine; not started |
| Viewport verified on the target GPU (i5-560M iGPU, D3D 10.1) | BLOCKED | waiting for the owner to run `k_gpu_probe` / `k_viewport_spike` |
| Selection sync, transform editing UI | BLOCKED | depends on Editor shell |
| Undo/Redo command history | PLANNED | engine-side core can proceed without Qt |
| Play/Stop isolation | PLANNED | engine-side core can proceed without Qt; ADR-0003 handle-domain risk to address first |
| CLI scene validate / tree dump | PLANNED | M05-T08 |
| Runtime World/ECS, scripting, assets, export, native SDKs, simulation | PLANNED | later milestones |
