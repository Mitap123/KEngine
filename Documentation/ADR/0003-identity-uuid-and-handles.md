# ADR-0003: Persistent UUID and generational runtime handles

- **Status:** accepted; implemented and tested (see Evidence).
- **Date:** 2026-11-18

## Context
References must survive save/load and source control (persistent), yet live references must be cheap and must never alias a recycled object (runtime). Reference §5.3.

## Decision
- **Persistent identity:** `k::Uuid`, 128-bit RFC 4122 v4, canonical lowercase `8-4-4-4-12` text. Unique per `Scene`, never nil. The only identity ever serialized. Generation uses `std::random_device`; `from_random_words` allows deterministic tests.
- **Runtime identity:** `k::Handle {uint32 index, uint32 generation}`; generation `0` is the null handle. Owned by a `SlotMap<T>`.
- **Guarantees (all tested):** destroy invalidates; slot reuse increments generation; stale/forged/out-of-range handles resolve to `nullptr` with a typed `HandleStatus` (`Valid/Null/OutOfRange/Stale`), never UB; payload is released at erase; a slot whose generation reaches the maximum is **retired**, never reused, so the counter cannot wrap and resurrect an old handle.
- **Asset GUID** is a separate identity introduced in M3; same text form is acceptable but it is a distinct type.
- Handles contain no pointers and are never serialized.

## Options considered
- Hash map from id → object (rejected for runtime: extra indirection and hashing on every access; kept only as the UUID→handle index in `Scene`).
- Raw pointers or indices without generations (rejected: stale-reference bugs).
- Sequential 64-bit ids (rejected for persistent ids: merge conflicts across branches/users).
- UUID v7 (time-ordered; attractive for diffs) — not chosen: random v4 is simplest and sufficient; can be revisited since the text form is the same.

## Evidence (verified)
`Tests/Unit/test_uuid.cpp`, `test_handle.cpp` (including a 20,000-step randomized model check, generation-exhaustion test and compile-time typed-handle checks), `test_scene.cpp`. Passed on Linux x86_64 / GCC 13.3 in Debug, Release and ASan+UBSan. A mutation check (deliberately breaking generation increment, stale check, retirement) made the tests fail.

## Trade-offs
`std::random_device` per UUID is slower than a cached engine; acceptable (authoring-time operation). A `Handle` does not name its container.

## Former risk, now resolved (M05-T02b)
A plain `Handle` did not name its container, so a handle from one scene could silently resolve against a copy of it (for example a Play clone). Handles are now **domain-typed**: `BasicHandle<Tag>`, with `ObjectHandle` for the authoring scene. `SlotMap<T, H>` only accepts its own handle type; passing another domain's handle (or the untyped `Handle`) to `Scene` or a typed `SlotMap` is a **compile error**, proven by `static_assert`s in `test_handle.cpp` and a compile-fail check. Remaining limitation: two *instances* of the same domain (two `Scene`s) still share a handle type; a cloned scene's handles are numerically valid in the original. Code that holds handles across scenes must use UUIDs. A runtime handle type will be added with the Runtime World (M1).

## Consequences
All cross-language wrappers hold handles, never object memory (ADR-0007). Registry lifetime/shutdown order must be explicit when the Runtime exists.

## Migration / rollback
Handle layout can change without a file-format change because it is never serialized. Packing into a `uint64` for the C ABI is specified in ADR-0007.
