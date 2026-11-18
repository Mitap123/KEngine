# Development status (handoff)

**Date:** 2026-11-19 · **Milestone:** M0 in progress, M0.5 engine-side in progress · **Branch:** `main`

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
Latest run (end of session 2, after the header change forced rebuilds):
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
1. **Handles don't name their container** (ADR-0003): cloned scenes share handle values; add typed per-domain handles before Runtime/Play work.
2. If `reparent`'s cycle check were bypassed, traversal loops forever (no defensive guard in `visit_preorder`). Only `reparent` can create parents, and it is tested, but a defensive guard could be added.
3. JSON numbers are doubles (exact integers to 2^53). Unknown scene fields are dropped on re-save (documented).
4. Euler rotation order is not yet normative (decide with the viewport/gizmo task).
5. Renderer (ADR-0004), Qt licensing (ADR-0005), manifest format (ADR-0006) are unresolved proposals.
6. `.kscene` extension is only proposed.
7. Scene file I/O: the `.bak` copy is not atomic (a crash mid-copy can damage the backup, never the target); `fsync` ordering and directory-fsync effectiveness cannot be tested here; a crash can leave stray `.tmp` files; save does not create missing directories (by design).
8. `fault_injection` is a production-visible test seam in `SaveOptions`; keep it empty outside tests.

## Open questions for the project owner
1. License for K Engine (no LICENSE file added deliberately).
2. Is there a dev machine/CI with Qt 6 + CMake + OpenGL to unblock M05-T01/T03 and verify `CMakeLists.txt`? (ADR-0004 spike steps are listed.)
3. Commercial Qt license an option, and will Editor binaries be redistributed? (ADR-0005)

## Next task (exact)
**M05-T02b: domain-typed handles** (new task in `TASKS.json`; addresses the ADR-0003 risk that a `Handle` does not name its container, which would let an Editor handle silently resolve against a cloned Play scene). Prerequisite: none outstanding.
Scope: `Engine/Identity`, `Engine/Scene`, `Tests/Unit`. Introduce an authoring `ObjectHandle` distinct from a future runtime handle type so mixing them is a compile error (prove with `static_assert` non-convertibility); keep behaviour and all 64 tests green; update ADR-0003. Out of scope: Runtime World/ECS, C ABI packing.
After that (Qt-free): command-history core for create/delete/transform (M05-T06 engine part) → scene clone for Play isolation (M05-T07 engine part) → CLI scene validate/dump (M05-T08, now unblocked by file I/O). When Qt/CMake exist: run the CMake route (record in BUILDING.md), do the ADR-0004 spike, start M05-T01.
