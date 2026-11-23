# Feature matrix

Statuses: PLANNED, PROTOTYPE, IMPLEMENTED, TESTED, STABLE, BLOCKED. Each status is scoped to the feature as defined in the row. Environment for all evidence: Ubuntu 24.04 x86_64, GCC 13.3.0, Debug/Release/ASan+UBSan unless stated (see DEVELOPMENT_STATUS.md).

| Feature (scope) | Status | Evidence / note |
|---|---|---|
| UUID v4 generate/parse/format | TESTED | `test_uuid.cpp` |
| Generational handles + SlotMap (incl. exhaustion retirement) | TESTED | `test_handle.cpp`, randomized model check, mutation check |
| Authoring Scene model (create/destroy/reparent/transform, in memory) | TESTED | `test_scene.cpp` |
| Strict JSON parse/write | TESTED | `test_json.cpp`; **not fuzzed**, not tested on large inputs |
| Scene format v1 serialize/load **in memory** (UUIDs, hierarchy, transforms, schema/version validation) | TESTED | `test_scene_serialization.cpp` |
| Hostile-input hardening of the JSON parser and scene loader (linear-time loading, capped warnings, deterministic mutation fuzzing) | TESTED | `test_robustness.cpp`; fuzzing is deterministic, not coverage-guided; memory use ~15x file size (measured) |
| Scene file save/load on POSIX (atomic temp+fsync+rename, `.bak`, size cap, error mapping) | TESTED | `test_scene_file.cpp`; Linux/GCC only; write/fsync/backup/rename failures simulated via hook; no power-loss test |
| Scene file I/O on non-POSIX platforms (fallback code path) | PROTOTYPE | compiled only when not POSIX; never built or run |
| CLI `k --version`, `--help`, `doctor [--json]` | TESTED | `Tests/CLI/test_cli.py` |
| Make build (Debug/Release/ASan+UBSan, strict warnings) | TESTED | `make check` variants |
| CMake build on Windows 10 (MSVC 19.51, Qt 6.12) | TESTED | owner: build 0 errors/0 warnings; ctest 4/4 (unit 135/135, cli, png_probe, viewport_screenshot). Not tried: MinGW, Ninja, Release, Windows 11 |
| CI workflow | WRITTEN, NOT RUN on GitHub (Linux make x3 + Windows cmake/MSVC, no Qt) | M0-T06 |
| Project creation / manifest | PLANNED | |
| Domain-typed handles (`ObjectHandle`; cross-domain use is a compile error) | TESTED | `static_assert`s in `test_handle.cpp`, compile-fail check |
| Math (Mat4/Vec3), scene local/world transforms | TESTED | `test_math.cpp`, `test_rendering_geometry.cpp` (incl. depth-100,000 chains) |
| Primitive meshes (cube/sphere/plane), orbit camera, screen rays, CPU picking, DrawList | TESTED | `test_rendering_geometry.cpp`, `test_camera_picking.cpp`; 12 mutation checks |
| `k_gpu_probe` (adapter / feature-level report) | TESTED | owner ran it: Intel HD Graphics 0x8086:0x0046 -> feature level **10_0**; WARP and Basic Render Driver 11_0 |
| Direct3D 11 renderer, feature level 10_0 floor (hardware device, SM 4.0 shaders, back-buffer capture) | TESTED | owner re-ran after the fix: lit shapes (lit 50.67%, black 0%), `viewport_screenshot` passed on the real GPU, frame ~0.5 ms |
| Qt viewport widget + spike app: window, init, render, `--screenshot`, `--no-warp` | IMPLEMENTED | observed on the owner's machine (console output, exit 0, PNG) |
| Spike interactive controls (orbit/pan/zoom/click-select/resize) | TESTED | owner confirmed all 7 behaviours by hand on the target machine (2026-11-19) |
| Editor core logic, Qt-free: `EditorDocument` (new/open/save/save-as, selection by UUID, create with unique names, rename, transform, delete, undo/redo labels, Play/Stop), Explorer rows, Inspector axis edits, play demo, "Frame All", demo scene | TESTED | `test_editor_core.cpp` (incl. the Hello Cube workflow without a GUI); 10 mutation checks all caught (2 survived first, tests added) |
| Qt Editor shell `k_editor` (menus, toolbar, Explorer/Inspector/Console docks, viewport, Play/Stop, `--smoke`, `--screenshot`) | IMPLEMENTED | **compiled cleanly with real Qt 6.12 / MSVC** (owner, 2026-11-19); behaviour not observed yet: `editor_smoke` and `editor_screenshot` have not run (DLL-not-found, PATH); manual checklist pending |
| Viewport verified on the target GPU (i5-560M iGPU, FL 10_0) | TESTED | rendering, shading, orbit/pan/zoom/select/clear/resize confirmed by the owner |
| Windows scene-file save path (CreateFileW + FlushFileBuffers + MoveFileExW) | TESTED | owner's rerun: `unit` 135/135 on Windows 10 (MSVC Debug), incl. fault injection at every stage; real disk-full/power-loss not tested |
| Selection sync, transform editing UI | BLOCKED | depends on Editor shell |
| Undo/redo for create/delete(subtree)/transform/rename/reparent, merge of drags, dirty tracking (`SceneEditor`, Qt-free) | TESTED | `test_commands.cpp` incl. randomized snapshot-model check; 14+ mutation checks. No UI yet |
| Reparent with sibling index and keep-world-transform (`decompose_trs`, `EditorDocument::reparent/can_reparent`, Qt-free); shear/zero-scale cases are rejected, not approximated | TESTED (core only) | `test_reparent.cpp`: round trips incl. gimbal poles and reflections, randomized 600-step world-preservation + exact-undo check, 10/10 injected bugs caught. **No Explorer drag-and-drop UI exists** |
| Play/Stop isolation (value copy of the authoring scene; edits/undo blocked while playing) | TESTED | `test_commands.cpp` (`play_*`, 500 repeated sessions). Engine-side only: no runtime loop, no Editor buttons yet |
| CLI `k scene validate <file>` and `k scene dump <file> [--json]` (stable error codes, exit codes, escaped terminal output) | TESTED | `Tests/CLI/test_cli.py`; 3 CLI mutation checks |
| CLI screenshot / visual probe | BLOCKED | needs the verified Direct3D path (M05-T03b) |
| Runtime World/ECS, scripting, assets, export, native SDKs, simulation | PLANNED | later milestones |
