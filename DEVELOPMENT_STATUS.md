# Development status (handoff)

**Date:** 2026-11-23 · **Milestone:** M0 complete except CI (M0-T06: workflow written, not yet run on GitHub); M0.5: engine side and viewport verified on the target machine; Qt Editor shell WRITTEN BUT NEVER COMPILED with real Qt (stopped here at the owner's request) · **Branch:** `main`

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

## Session 9 (2026-11-23): Linux re-verification + CI workflow (no Windows/Qt access)
**Re-verified on the final session-8 tree (Linux, GCC 13.3, 1 core):** `make STRICT=1 check` exit 0 (22 s incl. build of the rest); `make SANITIZE=1 STRICT=1 check` exit 0 (48 s, no sanitizer report; first ASan+UBSan run since the Editor core was added); `make BUILD_TYPE=Release STRICT=1 check` exit 0; `make BUILD_TYPE=Release STRICT=1 check-full` exit 0. Every run: unit 152/152 (not_run=0), CLI 21 OK, visual self-test 8 OK.
**CI (M0-T06, partial):** added `.github/workflows/ci.yml`. `linux-core` (ubuntu-24.04) runs the three make configurations above; `windows-core` (windows-2022) runs CMake configure/build (Debug)/ctest without Qt, which also exercises the CMake route's portable part. Logs are uploaded as artifacts (always). Checked only that the YAML parses (PyYAML); **not run on GitHub**, so the Windows job (default generator, runtime) is unverified. Not claimed: any other OS/compiler, Editor or GPU tests in CI.
**Not done / still unknown:** everything under "Next task" (owner's Windows run and the Editor manual checklist) is unchanged.
**Process note:** the first background build died with its shell (no exit line); reran detached, results above are from the reruns.

## Session 8 (2026-11-19): first real-Qt compile of the Editor
**Owner's run (Windows 10, MSVC 19.51, Qt 6.12.0, CMake with `-DKENGINE_BUILD_EDITOR=ON`):**
- **`k_editor` compiled and linked cleanly on the first attempt** (`main.cpp`, `MainWindow.cpp`, `EditorViewport.cpp`; no errors reported). So the stub-level check had been a fair predictor of compile-ability for this code. `kengine_editor_core` and all unit tests also built.
- ctest: `unit` **passed in 80.5 s** (was 311 s before the fast/full split; still slow on that machine), `cli` passed (6.5 s), `png_probe` passed. **`viewport_screenshot`, `editor_smoke` and `editor_screenshot` failed with exit code 3221225781 = 0xC0000135 = DLL not found**: the new PowerShell session did not have `D:\Qt\6.12.0\msvc2022_64\bin` on PATH (it was added by hand in the previous session). This is an environment/test-design problem, **not** a behaviour failure: no Editor behaviour has been observed yet.
**Fix (unverified, no CMake here):** the Qt-based ctest tests now prepend Qt's `bin` directory to PATH themselves (`ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:..."`, directory derived from `Qt6_DIR`; `cmake_minimum_required` raised to 3.22 for that property). Troubleshooting note added to `Editor/App/README.md`. Lesson: tests must not depend on the caller's environment; the previous spike test only passed because PATH happened to be set.
**Still unknown (needs the owner):** whether `k_editor --smoke` passes, what the window looks like, and the manual checklist.

## Session 7 (2026-11-19): re-verification, faster tests, Editor core + Qt shell (stopped early)
**Owner's re-verification (all good):** build 0 errors/0 warnings; ctest **4/4** (`unit` 135/135 incl. the new Windows save path, `cli` 21, `png_probe` 8, `viewport_screenshot`); shapes now lit (lit 50.67%, black 0%); by hand on the i5-560M: orbit, pan (right-drag and Shift+left-drag), zoom, click-select (orange, label shows the name), click-empty clears, resize/maximize all work; label `Intel(R) HD Graphics | FL 10_0 | hardware | frame ~0.5 ms`. M05-T03b is **done**. The two defects of session 6 are confirmed fixed.
**New finding from that report:** `unit` took **311 s** on Windows Debug (MSVC iterator debugging + a 2010 CPU). Measured on Linux Debug it was 35 s; five tests dominated. Fix: tests now have **fast (default) and full sizes** (`KENGINE_TEST_FULL=1`, `make check-full`, `-DKENGINE_FULL_TESTS=ON`/ctest `unit_full`): Linux Debug 35 s -> 4.3 s, full mode still passes; the runner prints per-test milliseconds. The Windows time with the new default has **not been measured**.
**Editor core (Qt-free, verified on Linux):** `Editor/Core` = `EditorDocument` (new/open/save/save-as with failure atomicity, selection by UUID auto-cleared, create with unique default names, rename, transform edit with merge, delete, undo/redo labels, Play/Stop, `adopt_scene`), `ExplorerModel`, `InspectorModel` (replace exactly one axis, clamping, scale >= 0.001), `PlayDemo`, `CameraFraming` (aspect-aware, tested by projecting all 8 corners of every object into the frustum), `DemoScene`. 17 new unit tests incl. the **Hello Cube workflow without a GUI**. 10 mutation checks: 8 caught at once, 2 survived (failed `open` changing the path; a selection that only existed in the play copy surviving Stop) -> tests added -> all caught. A 11th mutant was invalid (unused variable) and was redone.
**Qt Editor shell (`Editor/App`, UNVERIFIED):** `MainWindow` (menus, toolbar, Explorer/Inspector/Console docks, status bar, Play/Stop with a 16 ms timer, close prompt for unsaved changes, `--smoke` self-test of the real widgets), `EditorViewport` (spike's viewport bound to `EditorDocument`), `main` (`--demo`, `--screenshot`, `--smoke`, `--no-warp`, file argument), `CMakeLists.txt` (`-DKENGINE_BUILD_EDITOR=ON`; ctest `editor_smoke`, `editor_screenshot`), `README.md` with build steps and the report checklist. **Never compiled with real Qt.** The only check is `g++ -fsyntax-only` against a hand-written minimal Qt stub in `/tmp` (NOT in the repo): it caught nothing in the final code and was shown to catch a typo and a wrong lambda arity (negative controls); it cannot validate real Qt signatures or behaviour. Review findings fixed before stopping: a clumsy ternary in `do_create`, `QString::arg(size_t)` ambiguity, a smoke step that left the cube at scale 0.001.
**Stopped at the owner's request** after the stub-level check; no further Editor work was done.
**Verification at the end of this session (Linux):** `make STRICT=1 check` (Debug) exit 0, unit 152/152, CLI 21, visual self-test 8; `make BUILD_TYPE=Release STRICT=1 check` exit 0, same counts. **ASan+UBSan was NOT rerun on the final tree** (last run: 135/135 before the editor core existed) and `make check-full` was not rerun after the final edits (it passed after the fast/full change, before the editor core).
**Own mistakes this session:** underestimated Linux test time (assumed seconds, was 35 s); two test gaps found only by mutation (above); first stub check hit gaps in my own stub (not in the code).

## Session 6 (2026-11-19): first run on the owner's machine
**Environment reported:** Windows 10 Pro 19045; Intel Core i5-560M; Intel HD Graphics (0x8086:0x0046), driver 8.15.10.2900 (2012-11-26), 64 MB dedicated + 1632 MB shared; MSVC 19.51.36260.0 (VS Build Tools), Windows SDK 10.0.22000.0, Qt 6.12.0 `msvc2022_64`.
**What the run proved:** the CMake route and the whole Windows layer built on the first attempt; `cli` ctest passed; `k_viewport_spike --screenshot` (also with `--no-warp`) created a **hardware** device, rendered, wrote a PNG and exited 0.
**What it refuted / found:**
1. **Hardware floor is Direct3D feature level 10_0, not 10_1.** `k_gpu_probe`: the adapter was granted 10_0 when 11_0/10_1/10_0 were requested (the software "Microsoft Basic Render Driver" and WARP reach 11_0). The owner's "DX 10.1" requirement is still met (the engine runs on this machine) by targeting the lower floor; ADR-0004, AGENTS.md and the matrices were corrected. The shaders were already Shader Model 4.0, which is why it ran.
2. **All surfaces rendered black.** Cause: the pixel shader reads `color` from the `Object` constant buffer but only `VSSetConstantBuffers` was called. Fixed with `PSSetConstantBuffers(1, ...)`. My stub-level syntax check could not catch this (it is behaviour, not syntax).
3. **`unit` 134/135 on Windows:** `scene_file_failure_at_any_stage...` expected `file.sync_failed`, got `file.write_failed`. The Windows build was using my *generic* fallback (ofstream, no fsync, wrong error mapping for the injected sync failure). I replaced it with a real Windows path: `CreateFileW(CREATE_NEW)` + `WriteFile` + `FlushFileBuffers` + `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`, and made path text UTF-8 (`path.string()` goes through the ANSI code page on Windows and mangles non-ASCII names). Checked here only by a syntax check of a forced-Windows copy against stubs (with a negative control); behaviour on Windows is **unverified until the owner reruns**.
**Added:** a visual probe: `Tests/Visual/png_probe.py` (pure-stdlib PNG reader + black/lit pixel analysis; decoder cross-checked against PIL's independent encoder on 18 images), 8 self-tests (`make visual-selftest`, ctest `png_probe`), and ctest `viewport_screenshot` that runs the exe and fails on black surfaces (not yet run by the owner). The first hardware run's bug is exactly what it would have caught.
**Not reported by the owner (unknown):** interactive controls (orbit/pan/zoom/click-select/resize) and the lit appearance after the fix. The report template's blanks for label/controls/driver were left as `___` except what the console showed. Frame times observed: ~2-12 ms, 28-38 ms on the capture frame (GPU read-back); not investigated.
**Verification (Linux, after these changes):** `make STRICT=1 check`, `make SANITIZE=1 STRICT=1 check`, `make BUILD_TYPE=Release STRICT=1 check` all exit 0 with unit 135/135, CLI 21 OK, visual self-tests 8 OK; no sanitizer reports.
**Own mistakes this session:** assumed 10_1 hardware from recollection (wrong); an unbound constant buffer; an untested generic fallback shipped as the Windows save path; an attempt to syntax-check the Windows branch with `-D_WIN32` broke libstdc++ itself, so I used a forced-platform copy instead.

## Session 5 (2026-11-19): hostile-input hardening (M05-T10)
**Method:** measured pathological scene files before changing anything. Findings (release build): a 40,000-object parent chain took 7.3 s to load (per-object cycle check, O(n²)); a JSON object with 200,000 keys took 140 s (linear duplicate-key scans, O(n²)); 60,000 unknown fields produced 60,000 stored and printed warnings; peak memory is ~15× file size (274 MiB for 17.7 MiB). A 64 MiB file (the default read cap) could therefore have hung the loader for hours.
**Fixes:** scene loading now creates objects parent-first from the roots (O(n); cycles are the objects never reached, and the error names one that is actually on the cycle); JSON duplicate-key detection is hash-based past 16 members and object construction no longer scans; warnings capped at 100 + a summary line. After: 40k chain 0.096 s, 200k-key object 0.12 s, 200k roots 0.43 s.
**Tests (`test_robustness.cpp`, 9 cases):** scaling guards (4× input must cost <10×; the old code was ~22×), duplicate-key detection on both detection paths and at the switch boundary, 100,000-object chain round trip, cycle-reporting, warning cap, and a deterministic mutation fuzzer (~45k systematic + 20k random mutants; invariants: no crash/sanitizer report, stable error codes, dump is a fixed point, loaded scenes re-save and reload byte-identically).
**Honest results:** the fuzzer found **no** defects at these iteration counts; the defects came from measurement. Mutation checks: 7 caught outright (always-linear duplicate check, stale hash index, uncapped warnings, reversed child order, cycle blamed on the wrong object, duplicate UUID accepted, raw newline in dump); one (dump without a `\n` case) was an equivalent mutant. Coverage-guided / long-running fuzzing has **not** been done.
**Process notes / own mistakes:** two background mutation runs died silently (cause unknown), and one left the throw-away copy mutated, so I restored it from the repo and reran; a sed filter for "R1" also removed "R10" (caught and rerun separately); an output flood from forgetting to redirect stderr. A Release `-Werror` build found a real buffer-size bug in a test helper (`%08zx` into a 40-byte buffer); fixed and all three configurations rerun.

## Session 4 (2026-11-19): editing model, isolated Play, CLI scene tools
**Owner decisions:** license deferred (no LICENSE file); supported platforms Windows 10 and 11 x64 (recorded in AGENTS.md, README, ADR-0004/0005).
**Done and verified on Linux (unit + CLI tests, ASan+UBSan, Release):**
- `Scene`: sibling-indexed insertion/reordering (`create_with_uuid`/`reparent` index, `sibling_index`), needed to undo deletes at the exact old position.
- `Engine/Editing` (ADR-0011): `Command`, `CommandHistory` (undo/redo, merge of drags, clean/dirty tracking, depth trimming), UUID-addressed commands (create, delete subtree, set-transform, rename, reparent), `SceneEditor` (document model: edits, save/open, isolated Play). M05-T06 and M05-T07 engine sides.
- CLI `k scene validate <file> [--json]` and `k scene dump <file> [--json]` (M05-T08 partial). Text output escapes control characters from file content.
- Tests: 22 new unit cases (incl. a 1,500-step randomized undo/redo model check against serialized snapshots, 500 repeated Play sessions) and 11 new CLI cases.
**Mutation checks (throw-away copy / restored in repo):** 14 editing mutations: 13 caught at once (e.g. delete-undo not restoring sibling index, snapshot omitting descendants, redo branch kept, save while playing writing the play copy, undo allowed while playing). **One survived (H7)**: investigation showed the guarded branch was unreachable through the public API (`mark_clean()` always closes the merge run), so I deleted the dead code and replaced the misleading test with one that checks the real property. 3 CLI mutations (unescaped names, invalid scene exits 0, flat dump) all caught.
**Own mistakes fixed along the way:** a confused test (second `redo()` with nothing to redo); a randomized test mix (70% edits) that failed its own coverage guard, rebalanced instead of weakening the guard; a `/bin/sh` vs bash syntax slip in a helper script (nothing ran, redone).
**Not done / limits:** no Editor UI, no runtime loop (Play only isolates a scene copy), no composite commands (duplicate/paste), property edits limited to name and transform, CLI has no structured log file and no screenshot probe.

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
Latest run, each from a **clean tree** (`make clean` first), compiled with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror` (zero warnings):
| Command | Result |
|---|---|
| `make STRICT=1 check` | exit 0; unit `passed=135 failed=0 not_run=0`; CLI `Ran 21 tests ... OK` |
| `make SANITIZE=1 STRICT=1 check` | exit 0; unit 135/0/0; CLI 21 OK; no ASan/UBSan/LSan reports |
| `make BUILD_TYPE=Release STRICT=1 check` | exit 0; unit 135/0/0; CLI 21 OK |
(The sanitizer and Release runs were rebuilt in steps rather than from `make clean`, after a final test-helper fix; the earlier clean-tree runs of session 4 were 126/126 in all three.)
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
Real-Qt compile and run of `Editor/App` (`editor_smoke`, `editor_screenshot`, manual checklist); ASan+UBSan on the final tree (editor core added after the last run); `make check-full` on the final tree; the Windows unit-test duration with the new fast default; Windows 11, Release/MinGW/Ninja builds.
(Older list, still true where not contradicted above:)
CMake configure/build/ctest (no cmake); any Qt/GUI/viewport/screenshot test; integration, determinism, performance, fuzz, coverage; CI (not written); Clang or any non-GCC/non-Linux build; non-POSIX scene-file fallback; real disk-full / I/O-error / power-loss behaviour (only simulated by the fault hook); permission-denied cases (suite runs as root); coverage-guided / long-running parser fuzzing (only a bounded deterministic mutation fuzzer ran).

## Known risks / bugs
1. Handles are domain-typed now, but two `Scene` instances share a handle type: a cloned scene's handles are numerically valid in the original. Use UUIDs across scenes (ADR-0003).
2. If `reparent`'s cycle check were bypassed, traversal loops forever (no defensive guard in `visit_preorder`). Only `reparent` can create parents, and it is tested, but a defensive guard could be added.
3. JSON numbers are doubles (exact integers to 2^53). Unknown scene fields are dropped on re-save (documented).
4. (Resolved in ADR-0010: Euler order `Ry*Rx*Rz`, transforms are local.) Euler angles still have gimbal lock at pitch ±90°.
5. The Direct3D 11 / Qt path is unverified on real hardware (ADR-0004); the hardware facts about the i5-560M iGPU are recollection. Qt licensing (ADR-0005) and the manifest format (ADR-0006) are unresolved.
10. Loading a scene file needs ~15× the file size in memory (measured); the 64 MiB default cap implies ~1 GiB transient. Destroying many siblings one at a time is O(siblings) each. The timing-based scaling tests can be flaky on a heavily loaded machine.
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
**Owner (≈5 min), from the repo root in PowerShell:**
```
cmake --build Build/cmake --config Debug
ctest --test-dir Build/cmake -C Debug --output-on-failure
```
(`cmake --build` re-configures because `CMakeLists.txt` changed; no manual PATH needed any more.) Expect 6/6. If `editor_smoke` fails, paste its printed `[ OK ]/[FAIL]` list (or run `$env:PATH="D:\Qt\6.12.0\msvc2022_64\bin;$env:PATH"; .\Build\cmake\Editor\App\Debug\k_editor.exe --smoke`). Then run `k_editor.exe --demo` by hand and answer the checklist in `Editor/App/README.md`. Optional: the per-test milliseconds of the slowest unit tests (`.\Build\cmake\Debug\k_unit_tests.exe`, ~80 s) so I can cut the Windows run time further.
**Then (me):** fix what the smoke/manual run shows; M05-T09 final acceptance; M0-T06 CI (first GitHub run + record it in BUILDING.md); Explorer drag-and-drop reparent; composite commands; project manifest + `k create`.
