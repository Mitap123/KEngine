# Feature matrix

Statuses: PLANNED, PROTOTYPE, IMPLEMENTED, TESTED, STABLE, BLOCKED. Each status is scoped to the feature as defined in the row. Environment for all evidence: Ubuntu 24.04 x86_64, GCC 13.3.0, Debug/Release/ASan+UBSan unless stated (see DEVELOPMENT_STATUS.md).

| Feature (scope) | Status | Evidence / note |
|---|---|---|
| UUID v4 generate/parse/format | TESTED | `test_uuid.cpp` |
| Generational handles + SlotMap (incl. exhaustion retirement) | TESTED | `test_handle.cpp`, randomized model check, mutation check |
| Authoring Scene model (create/destroy/reparent/transform, in memory) | TESTED | `test_scene.cpp` |
| Strict JSON parse/write | TESTED | `test_json.cpp`; **not fuzzed**, not tested on large inputs |
| Scene format v1 serialize/load **in memory** (UUIDs, hierarchy, transforms, schema/version validation) | TESTED | `test_scene_serialization.cpp` |
| Scene file save/load, atomic save | PLANNED | M05-T05 |
| CLI `k --version`, `--help`, `doctor [--json]` | TESTED | `Tests/CLI/test_cli.py` |
| Make build (Debug/Release/ASan+UBSan, strict warnings) | TESTED | `make check` variants |
| CMake build | PROTOTYPE | written, never executed (no cmake available) |
| CI workflow | PLANNED | M0-T06 |
| Project creation / manifest | PLANNED | |
| Qt Editor shell (docks) | BLOCKED | Qt 6 not installed; no network |
| Viewport rendering, picking, screenshot probe | BLOCKED | no Qt/GL dev stack/GPU; ADR-0004 unverified |
| Selection sync, transform editing UI | BLOCKED | depends on Editor shell |
| Undo/Redo command history | PLANNED | engine-side core can proceed without Qt |
| Play/Stop isolation | PLANNED | engine-side core can proceed without Qt; ADR-0003 handle-domain risk to address first |
| CLI scene validate / tree dump | PLANNED | M05-T08 |
| Runtime World/ECS, scripting, assets, export, native SDKs, simulation | PLANNED | later milestones |
