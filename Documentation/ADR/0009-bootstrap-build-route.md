# ADR-0009: Bootstrap Makefile alongside CMake

- **Status:** accepted (temporary arrangement).
- **Date:** 2026-11-18

## Context
The reference prescribes CMake (+ Ninja). The environment used for M0 has GCC 13.3, GNU make and Python 3.12 but **no cmake, no ninja, no network** to obtain them. M0 requires a reproducible build/test route that actually runs.

## Options
1. Wait for CMake — blocks all verification.
2. Write only CMake — unverifiable here; violates "build and test after each change".
3. **Provide a verified minimal Makefile and a CMakeLists.txt, both globbing the same directories.**
4. Hand-roll a build script in Python — more code than a Makefile for the same job.

## Decision
Option 3. The Makefile is the **verified** route today (Debug, Release, ASan+UBSan, strict warnings). `CMakeLists.txt` is the intended primary build for the Editor/Qt (which a Makefile should not grow to handle) and is marked **unverified until first run**.

## Consequences
- Two files to keep consistent; globs keep source lists in sync, but compile flags/options are duplicated.
- The Makefile is deliberately limited to the Qt-free core, the CLI and tests. It will not gain Qt, packaging or cross-platform logic.

## Exit criterion
When CMake builds and tests pass in CI and on a developer machine, make CMake the documented primary route and either remove the Makefile or keep it as a documented fallback. Record the date in BUILDING.md.

## Validation
`make check`, `make BUILD_TYPE=Release check`, `make SANITIZE=1 check` (results in TESTING.md). CMake: unverified.
