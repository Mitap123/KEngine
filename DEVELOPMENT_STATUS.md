# Development status (handoff)

**Date:** 2026-11-18 · **Milestone:** M0 in progress, M0.5 engine-side started · **Branch:** `main`

## Summary
New project (no pre-existing repository). This session established the control documents, ADRs, a verified build/test route, a minimal CLI, and the first M0.5 engine slice (identity, authoring scene, strict JSON, scene serialization in memory). The Qt Editor, renderer and everything GUI-related is **blocked by the environment**, not started.

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

## Completed this session
- M0-T01 audit (above). M0-T02 inventory (`DEPENDENCIES.md`; licenses of proposed deps UNVERIFIED). M0-T03 nine ADRs.
- M0-T04 **partial**: Makefile verified; `CMakeLists.txt` written, never run.
- M0-T05 CLI: `k --version`, `--help`, `doctor [--json]`.
- M05-T02 identity + scene model + scene serialization in memory (format `kengine.scene` v1).
- Control docs: AGENTS.md, README, ARCHITECTURE, ROADMAP, FEATURE_MATRIX, BUILDING, TESTING, TASKS.json, Documentation/Formats/scene-v1.md.

## Verification (exact commands, clean tree, Ubuntu 24.04 / GCC 13.3.0)
| Command | Result |
|---|---|
| `make STRICT=1 check` | exit 0; unit `passed=51 failed=0 not_run=0`; CLI `Ran 10 tests ... OK` |
| `make SANITIZE=1 STRICT=1 check` | exit 0; unit 51/0/0; CLI 10 OK; no ASan/UBSan reports |
| `make BUILD_TYPE=Release STRICT=1 check` | exit 0; unit 51/0/0; CLI 10 OK |
Compiled with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`: zero warnings.

**Mutation check** (manual, in a throw-away copy; not automated, not part of the repo): each of 8 deliberate defects was caught — destroy leaves UUID index entry (3 failing cases), UUID version bits unset (2), newer schema version accepted (1), duplicate JSON keys accepted (1), generation not incremented on erase (4), generation exhaustion never retires slot (1), stale check ignores generation (4), reparent without cycle check (**test binary hangs**, killed by timeout — it is detected by timeout, not by an assertion). Baseline after restoring: 51/51.

Process notes: an earlier mutation run was invalidated by a stale-object (mtime) bug in my script and was redone; first results were discarded.

## Tests/checks NOT run
CMake configure/build/ctest (no cmake); any Qt/GUI/viewport/screenshot test; integration, determinism, performance, fuzz, coverage; CI (not written); Clang or any non-GCC/non-Linux build. Parser fuzzing and large-input tests not done.

## Known risks / bugs
1. **Handles don't name their container** (ADR-0003): cloned scenes share handle values; add typed per-domain handles before Runtime/Play work.
2. If `reparent`'s cycle check were bypassed, traversal loops forever (no defensive guard in `visit_preorder`). Only `reparent` can create parents, and it is tested, but a defensive guard could be added.
3. JSON numbers are doubles (exact integers to 2^53). Unknown scene fields are dropped on re-save (documented).
4. Euler rotation order is not yet normative (decide with the viewport/gizmo task).
5. Renderer (ADR-0004), Qt licensing (ADR-0005), manifest format (ADR-0006) are unresolved proposals.
6. `.kscene` extension is only proposed.

## Open questions for the project owner
1. License for K Engine (no LICENSE file added deliberately).
2. Is there a dev machine/CI with Qt 6 + CMake + OpenGL to unblock M05-T01/T03 and verify `CMakeLists.txt`? (ADR-0004 spike steps are listed.)
3. Commercial Qt license an option, and will Editor binaries be redistributed? (ADR-0005)

## Next task (exact)
**M05-T05 (engine-side): scene file save/load.** Prerequisites: none outstanding (M05-T02 done; dependency re-scoped in TASKS.json).
Scope: `Engine/Serialization` + `Tests/Serialization`. Atomic save (temp file in same directory + flush + rename; keep a backup), load with size cap and I/O errors mapped to stable codes, tests for truncated/malformed/missing files and a failure-injection test for the atomic path. Out of scope: project manifest, prefabs, migrations beyond schema check, Editor UI.
After that (also Qt-free): typed handles (ADR-0003 risk) → command-history core for create/delete/transform (M05-T06 engine part) → scene clone for Play isolation (M05-T07 engine part). Then, when Qt/CMake exist: run `cmake` route (record in BUILDING.md), do the ADR-0004 spike, start M05-T01.
