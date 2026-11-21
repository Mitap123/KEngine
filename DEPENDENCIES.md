# Dependencies and licenses

Rule: verify license details from authoritative project/license files; never guess. Items marked **UNVERIFIED** come from recollection (no network or upstream files were available in the session that wrote this) and must be checked before adoption or distribution.

## Current (what the repository uses today)

| Item | Version (verified) | Purpose | License | Required/optional |
|---|---|---|---|---|
| C++ standard library | libstdc++ with GCC 13.3.0 | everything | toolchain-provided | required |
| GCC (g++) | 13.3.0 (Ubuntu 24.04) | compiler | GPL with runtime library exception (**UNVERIFIED**; toolchain, not redistributed) | required (dev) |
| GNU make | 4.3 | bootstrap build | GPL (toolchain, not redistributed; **UNVERIFIED**) | required (dev, bootstrap route) |
| Python | 3.12.3 | CLI tests only (`unittest`, stdlib) | PSF (**UNVERIFIED**) | required for `make cli-test` |
| git | 2.43.0 | version control | GPLv2 (**UNVERIFIED**) | dev |
| MSVC (Visual Studio Build Tools) | 19.51.36260.0 (owner's machine) | Windows compiler | Microsoft terms, **UNVERIFIED** | dev |
| Windows SDK | 10.0.22000.0 (owner's machine) | Direct3D 11 / DXGI / D3DCompiler headers and libs | Microsoft terms, **UNVERIFIED** | Editor/Spike |
| Qt 6 Widgets | 6.12.0 `msvc2022_64` (owner's machine) | Editor/Spike window | LGPLv3/GPLv3/commercial, **UNVERIFIED** (ADR-0005) | Editor-only |

No third-party library is linked into `Engine/`, `k` or the tests. The JSON parser and the test harness are in-repo code (ADR-0006, ADR-0009).

## Proposed (NOT added; none present in the build)

| Candidate | Purpose | Milestone | License (UNVERIFIED) | Notes / action before adoption |
|---|---|---|---|---|
| CMake + Ninja | primary build | M0-T04 follow-up | BSD-3 / Apache-2.0 | Not installed in dev env. |
| Qt 6 (Core, Gui, Widgets only) | Editor UI | M0.5 | LGPLv3/GPLv3/commercial, module-dependent | **Unknown obligations flagged**: see ADR-0005. Editor-only. |
| Direct3D 11 + DXGI + D3DCompiler (Windows system API; Windows SDK or MinGW-w64 headers) | first renderer backend (feature level 10_1 floor) | M0.5 | system component; SDK/headers license terms **UNVERIFIED** | ADR-0004. `d3dcompiler_47.dll` ships with recent Windows; older systems may need it redistributed (**UNVERIFIED**). OpenGL 3.3 was rejected. |
| ECS: flecs / EnTT / custom | runtime world | M1 | MIT (both, believed) | Benchmark + license check first (ADR-0001). |
| nanobind / pybind11 / raw CPython API | Python binding | M2 | BSD-3 (believed) / PSF | ADR-0007; headers for CPython 3.12 present, bindings libs absent. |
| Rust toolchain (cargo) | Rust SDK | M5 | MIT/Apache-2.0 (believed) | Not installed. |
| doctest / Catch2 | test framework | optional | MIT / BSL-1.0 (believed) | Replace in-repo harness only if it limits us. |
| GLM or similar math lib | math | M0.5+ | MIT (believed) | Only if hand-written math becomes a burden. |
| Physics, audio, glTF importer, profiler | later milestones | M3–M7 | various | Evaluate when needed (reference §3.1). |

## Policy
Before adding any dependency record: purpose and why stdlib/in-repo code is insufficient, version and maintenance status, license and redistribution obligations, platform support, size/build impact, security/update considerations, and whether it is required, optional or Editor-only. Keep Editor-only dependencies out of the Runtime.
