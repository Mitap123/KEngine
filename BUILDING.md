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

## Not buildable yet
The Qt Editor, renderer, Runtime, Python/C++/Rust SDKs do not exist. Qt 6 is not installed in the dev environment (ADR-0004/0005).
