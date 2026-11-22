# Building K Engine

## Verified route today: GNU make

Verified on: Ubuntu 24.04.5, x86_64, GCC 13.3.0, GNU make 4.3, Python 3.12.3 (1 core, 4 GB RAM). No other platform or compiler has been tried.

```bash
make                      # Debug build of Build/make-debug/{k,k_unit_tests}
make check                # build + unit tests + CLI tests
make STRICT=1 check       # warnings as errors (what CI-equivalent runs use)
make BUILD_TYPE=Release check
make SANITIZE=1 check     # AddressSanitizer + UBSan (separate Build/make-debug-asan)
make clean                # removes Build/
```

Outputs live under `Build/` (git-ignored; never hand-edit). Each configuration has its own directory, so switching configs does not force rebuilds.

Requirements: a C++20 compiler (`CXX=...` to override), GNU make, Python 3 for CLI tests.

## CMake: written, NOT verified
`CMakeLists.txt` exists but has never been run (no cmake in the dev environment). First real use should be:

```bash
cmake -S . -B Build/cmake -DCMAKE_BUILD_TYPE=Debug -DKENGINE_WARNINGS_AS_ERRORS=ON
cmake --build Build/cmake
ctest --test-dir Build/cmake --output-on-failure
```
Record the outcome (date, cmake version, result) here when it is run. Until then treat it as unverified. See ADR-0009.

## Windows 10 / 11 (x64) — first verified run 2026-11-19
Environment that worked for the owner: Windows 10 Pro 19045, Visual Studio Build Tools (MSVC 19.51.36260.0, generator "Visual Studio 18 2026"), Windows SDK 10.0.22000.0, Qt 6.12.0 `msvc2022_64`, CMake, Python 3 on PATH. CMake sets `/Zc:__cplusplus /utf-8 /permissive-` for MSVC. Follow `Editor/Spike/README.md` (Steps 0-3; the Debug exes are in `Build\cmake\Editor\Spike\Debug`).
Observed: configure and build succeeded for the portable code, `k_gpu_probe` and `k_viewport_spike`; `ctest`: `cli` passed, `unit` 134/135 before the fix to the Windows scene-file path (expected 135/135 now: rerun needed). Not tried: Release configuration, MinGW, Ninja, Windows 11.

## Editor (`k_editor`) — first compile pending
`-DKENGINE_BUILD_EDITOR=ON` (Windows only) builds `Editor/App`; see `Editor/App/README.md` for the exact commands, the new ctest tests (`editor_smoke`, `editor_screenshot`) and what to report. `make check-full` / `-DKENGINE_FULL_TESTS=ON` run the full-size unit tests.

## Platform coverage
Tested: Linux x86_64 + GCC 13.3 (all unit/CLI tests, sanitizers) and Windows 10 + MSVC 19.51 (build, CLI tests, 134/135 unit tests before the scene-file fix). Not tested: Windows 11, MinGW, Clang, macOS. The generic non-POSIX, non-Windows scene-file fallback is unverified.
