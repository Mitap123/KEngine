# Development status (handoff)

**Date:** 2026-11-19 · **Milestone:** M0 in progress, M0.5 engine-side done through viewport support; Editor/viewport awaiting owner verification · **Branch:** `main`

## Summary
New project (no pre-existing repository). Session 1 established the control documents, ADRs, a verified build/test route, a minimal CLI, and the first M0.5 engine slice (identity, authoring scene, strict JSON, scene serialization). Session 2 added scene **file** save/load (atomic, with backup and bounded, validated load). The Qt Editor, renderer and everything GUI-related remains **blocked by the environment**, not started.

## Environment audit (M0-T01)
| Item | Finding |
|---|---|
| OS / CPU | Ubuntu 24.04.5 LTS, Linux 6.18.44, x86_64, **1 core**, ~4 GB RAM (disk space not recorded) |
| Compiler | GCC/g++ 13.3.0 (C++20 OK; `<charconv>` double, `<=>`, ASan, UBSan verified working). clang: absent |
| Build tools | GNU make 4.3, git 2.43.0, pkg-config 1.8.1. **cmake: absent. ninja: absent. ccache: absent** |
| Python | 3.12.3, `Python.h` present at /usr/include/python3.12 (embedding experiments possible). nanobind/pybind11 not installed |
| Rust | rustc/cargo absent |
| Qt | absent (no qmake/moc/Qt6 CMake config found) |
| Graphics | No `/dev/dri` (no GPU). Mesa GLX/EGL and libvulkan **runtime** libs present; no OpenGL/Vulkan dev headers except GLX headers. No glxinfo/vulkaninfo/Xvfb |
| Network / packages | bash network disabled; `pip download cmake` found no distribution; `apt-get` cannot locate cmake. Treated as offline |
| Profilers | not investigated |

Consequence: CMake/Qt/Rust/GPU work cannot be built or verified here. Work that does not need them proceeded.

## Session 3 (2026-11-19): target hardware = Direct3D 10.1 (Intel Core i5-560M)
**New fact from the owner:** development machine has Qt 6 + CMake (Windows assumed) and an Intel Core i5-560M; the app must support DX 10.1. This invalidated the earlier ADR-0004 proposal (OpenGL 3.3 core), because that iGPU is believed (UNVERIFIED) to offer only OpenGL 2.1.
**Decisions:** ADR-0004 rewritten (Direct3D 11 API at feature level 10_1, Qt Widgets only, `DrawList` as the API-neutral boundary, low-end profile); ADR-0010 (coordinate/math conventions, incl. the previously undefined Euler order and local-transform semantics); ADR-0003 risk addressed.
**Done and verified on Linux (unit tests, ASan+UBSan, Release):**
- M05-T02b domain-typed handles (`BasicHandle<Tag>`, `ObjectHandle`); cross-domain use is a compile error (static_asserts + compile-fail check).
- M05-T03a Qt-free viewport support: `Engine/Math` (Vec3, Mat4), `Scene/SceneTransforms`, `Rendering/{Primitives, OrbitCamera, Picking, DrawList}`. Tests check mesh closure/winding/normals structurally, picking under rotation, non-uniform scale and parent transforms, camera formulas and screen-ray round trips.
- Test runner converts uncaught exceptions into failures; the symlink test skips (with a printed note) where symlinks cannot be created (Windows without privilege); CMake sets `/Zc:__cplusplus /utf-8 /permissive-` for MSVC. These were fixes for *predicted* Windows failures, not observed ones.
**Written but NEVER compiled or run (Windows-only, in `Editor/Spike`, UNVERIFIED):** `D3D11Renderer` (feature levels 11_0/10_1/10_0, SM 4.0 shaders, classic swap chain, back-buffer capture), `gpu_probe` (Qt-free adapter/feature-level report), `ViewportWidget` + `main` (Qt 6 Widgets, orbit/pan/zoom/pick/screenshot), `Editor/Spike/CMakeLists.txt`, option `KENGINE_BUILD_EDITOR_SPIKE`.
- **What the only check on that code proves:** `D3D11Renderer.cpp` and `gpu_probe.cpp` pass `g++ -fsyntax-only` against stub headers I wrote from memory. A negative control confirmed the check catches typos and type errors. It does **not** validate Direct3D signatures, HLSL, or any behaviour. The Qt files have no check at all (no Qt in the sandbox).
- **Correction to my own work:** I replaced a `for (auto [kind, mesh] : {pair{...}, {...}})` loop in the renderer believing it was a compile error; testing showed it is valid C++. The replacement is harmless but was not a bug fix.
**Mutation check for the new code (throw-away copy, 12 mutations, all caught):** Euler order swapped (1 failing case), TRS order (4), perspective depth term (3), sphere winding flipped (2), cube face winding flipped (1), parent transform ignored (4), pick direction renormalized (4), pan sign (1), farthest-hit (1), single-sided plane (1), Empty objects drawn (3), relative singularity threshold in `Mat4::inverse` (1). The last one is a defect I introduced and fixed myself earlier in the session (a threshold keyed to the largest element wrongly rejected a tiny scale placed far from the origin); a regression test now guards it. Baseline after restoring: 103/103.

## Session 2 (2026-11-19): M05-T05 engine-side — scene file save/load
**Done:** `Engine/Serialization/SceneFile.{h,cpp}` (atomic save: temp in same dir, fsync, `.bak` copy, rename; refuses non-regular files/symlinks; bounded load that refuses non-regular files and prefixes errors with the path), `Tests/Unit/test_scene_file.cpp` (13 cases).
**Defect found and fixed in my own test harness:** `KCHECK_EQ` kept `const auto&` references to operands, which dangled for `Result::value()` on temporaries (GCC `-Wdangling-reference` caught it). Macro now copies operands.
**Test assumption corrected:** a file truncated by only the trailing newline is still valid JSON; the truncation test now covers every prefix that cuts into the document and asserts the newline case explicitly.
**Mutation check (throw-away copy, 7 mutations, all caught):** temp file not removed after failed rename (2 failing cases), backup never copied (1), size limit not enforced (1), non-regular files read (3), symlink replaced silently (1), temp file leaked on write failure (2), backup failure ignored (1). One additional mutation (F1) did not apply because my `sed` expression was wrong; it was covered by the corrected F1b. Baseline before and after: 64/64.

## Completed in session 1
- M0-T01 audit (above). M0-T02 inventory (`DEPENDENCIES.md`; licenses of proposed deps UNVERIFIED). M0-T03 nine ADRs.
- M0-T04 **partial**: Makefile verified; `CMakeLists.txt` written, never run.
- M0-T05 CLI: `k --version`, `--help`, `doctor [--json]`.
- M05-T02 identity + scene model + scene serialization in memory (format `kengine.scene` v1).
- Control docs: AGENTS.md, README, ARCHITECTURE, ROADMAP, FEATURE_MATRIX, BUILDING, TESTING, TASKS.json, Documentation/Formats/scene-v1.md.

## Verification (exact commands, Ubuntu 24.04 / GCC 13.3.0)
Latest run (end of session 3), each from a **clean tree** (`make clean` first), compiled with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror` (zero warnings):
| Command | Result |
|---|---|
| `make STRICT=1 check` | exit 0; unit `passed=103 failed=0 not_run=0`; CLI `Ran 10 tests ... OK` |
| `make SANITIZE=1 STRICT=1 check` | exit 0; unit 103/0/0; CLI 10 OK; no ASan/UBSan reports |
| `make BUILD_TYPE=Release STRICT=1 check` | exit 0; unit 103/0/0; CLI 10 OK |
Nothing in `Editor/` is part of these runs (it is only built with `-DKENGINE_BUILD_EDITOR_SPIKE=ON` on Windows).

Earlier run (end of session 2):
| Command | Result |
|---|---|
| `make STRICT=1 check` (Debug) | exit 0; unit `passed=64 failed=0 not_run=0`; CLI `Ran 10 tests ... OK` |
| `make SANITIZE=1 STRICT=1 check` | exit 0; unit 64/0/0; CLI `Ran 10 tests ... OK`; no ASan/UBSan reports |
| `make BUILD_TYPE=Release STRICT=1 check` | exit 0; unit 64/0/0; CLI 10 OK |
No scratch directories were left in the system temp dir by the tests. Compiled with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`: zero warnings. (Session 1's full-clean-tree results were 51/51 in all three configurations.)

**Mutation check** (manual, in a throw-away copy; not automated, not part of the repo): each of 8 deliberate defects was caught — destroy leaves UUID index entry (3 failing cases), UUID version bits unset (2), newer schema version accepted (1), duplicate JSON keys accepted (1), generation not incremented on erase (4), generation exhaustion never retires slot (1), stale check ignores generation (4), reparent without cycle check (**test binary hangs**, killed by timeout — it is detected by timeout, not by an assertion). Baseline after restoring: 51/51.

Process notes: an earlier mutation run was invalidated by a stale-object (mtime) bug in my script and was redone; first results were discarded.

## Tests/checks NOT run
CMake configure/build/ctest (no cmake); any Qt/GUI/viewport/screenshot test; integration, determinism, performance, fuzz, coverage; CI (not written); Clang or any non-GCC/non-Linux build; non-POSIX scene-file fallback; real disk-full / I/O-error / power-loss behaviour (only simulated by the fault hook); permission-denied cases (suite runs as root); parser fuzzing; scenes beyond 5,000 objects / files beyond a few hundred KB.

## Known risks / bugs
1. Handles are domain-typed now, but two `Scene` instances share a handle type: a cloned scene's handles are numerically valid in the original. Use UUIDs across scenes (ADR-0003).
2. If `reparent`'s cycle check were bypassed, traversal loops forever (no defensive guard in `visit_preorder`). Only `reparent` can create parents, and it is tested, but a defensive guard could be added.
3. JSON numbers are doubles (exact integers to 2^53). Unknown scene fields are dropped on re-save (documented).
4. (Resolved in ADR-0010: Euler order `Ry*Rx*Rz`, transforms are local.) Euler angles still have gimbal lock at pitch ±90°.
5. The Direct3D 11 / Qt path is unverified on real hardware (ADR-0004); the hardware facts about the i5-560M iGPU are recollection. Qt licensing (ADR-0005) and the manifest format (ADR-0006) are unresolved.
9. Spike shader normals are exact only for uniform scale. Non-ASCII file paths on Windows are untested (`path.string()` in error messages may mis-encode). The non-POSIX scene-file fallback and MSVC/MinGW builds are unverified. Windows 7/8 are out of scope (owner decision).
6. `.kscene` extension is only proposed.
7. Scene file I/O: the `.bak` copy is not atomic (a crash mid-copy can damage the backup, never the target); `fsync` ordering and directory-fsync effectiveness cannot be tested here; a crash can leave stray `.tmp` files; save does not create missing directories (by design).
8. `fault_injection` is a production-visible test seam in `SaveOptions`; keep it empty outside tests.

## Decisions by the project owner
- 2026-11-19: K Engine's license is **deferred** (not needed for now). No LICENSE file. Re-open before any binary distribution (ADR-0005).
- 2026-11-19: supported platforms are **Windows 10 and 11 (x64)**; Windows 7/8 are out of scope.

## Open questions for the project owner
1. Qt kit (MSVC or MinGW), compiler, and GPU driver version, needed to interpret the spike results.
2. Commercial Qt license an option, and will Editor binaries be redistributed? (ADR-0005; only matters at distribution time.)
3. Is a non-Windows Editor/viewport ever required? (It would need a second renderer backend behind `DrawList`.)

## Next task (exact)
**M05-T03b (owner action): run the spike on the i5-560M machine.** Follow `Editor/Spike/README.md` Steps 0–3 and send back: `ctest` output (this is also the first real run of `CMakeLists.txt`), the full `k_gpu_probe` output, the viewport console output/exit codes (including `--screenshot out.png --no-warp`), `out.png`, and Windows/driver/Qt/compiler versions. Expect possible small compile fixes in the Windows-only files; send the compiler output.
Why this first: ADR-0004 rests on unverified hardware facts and the renderer has never run; building the Editor shell (M05-T01) on top of it would compound unknowns.
**If the owner cannot run it yet (Qt-free work that can continue meanwhile):** M05-T06 engine part (command history for create/delete/rename/transform with undo/redo, built on `Scene`), then M05-T07 engine part (isolated Play via scene copy), then M05-T08 (CLI `scene validate/dump`, now unblocked).
