# ADR-0007: Stable C ABI and Python binding strategy

- **Status:** proposed. Implementation deferred to M2 (Python) and M5 (native SDKs); only principles are fixed now so earlier code does not contradict them.
- **Date:** 2026-11-18

## Context
Extensions may be compiled independently in C++, Rust and Python. A stable boundary needs fixed layout, ownership, error and threading rules. Reference §8.

## Decision (principles to preserve from now on)
1. The stable boundary is a **versioned C ABI**; no C++ class layout, STL type, exception or Rust-native layout crosses it. No C++ exception or Rust panic may unwind across it (catch at the boundary, convert to an error code).
2. **Handles over pointers:** objects cross the boundary as an opaque 64-bit value packed from `Handle` (`generation << 32 | index`), tagged by domain once typed handles exist (see ADR-0003 risk). Wrappers never own engine object memory.
3. **API versioning:** a function table (`struct`) with `struct_size` and `api_version` fields, negotiated at module load; incompatible modules are refused before any call.
4. **Errors:** `int32` result codes plus a retrievable structured diagnostic (code string + message). Memory returned to callers has an explicit free function.
5. **Threading:** engine/gameplay callbacks run on the runtime thread only by default. Python callbacks acquire the GIL explicitly and are main/runtime-thread-only initially.
6. **Trust:** embedded Python and in-process native plugins are trusted code, **not sandboxes**.

## Python binding: options (decide in M2 with evidence)
- **nanobind** (small, modern; C++17+).
- **pybind11** (mature, header-heavy, larger ecosystem).
- **Raw CPython C API / ctypes-cffi over the C ABI** (no C++ binding dependency; more manual code).
No performance or compile-time claims are made here; M2 must benchmark call overhead and build time on this project's real API surface.

## Evidence
Verified in the dev environment: CPython 3.12.3 and `Python.h` are present, so embedding experiments are possible. nanobind and pybind11 are **not** installed, and there is no network to install them. Nothing about binding libraries has been tested.

## Trade-offs
Fixing principles early costs a little design now and prevents ABI breaks later; choosing a binding library early would be a guess.

## Consequences
Until M5, only internal C++ APIs exist and carry no ABI promise. New engine APIs should avoid exposing raw pointers where handles will be needed.

## Validation plan
M2: handle-based Python API with GIL/thread enforcement tests and traceback tests. M5: C ABI + C++/Rust/Python interop matrix (lifetime, stale-handle, error, version-mismatch cases).
