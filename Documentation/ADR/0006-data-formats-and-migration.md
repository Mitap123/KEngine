# ADR-0006: Data formats, schema versioning and migration

- **Status:** accepted for the scene format v1 (implemented, tested in memory). Project manifest, asset metadata and migration framework are **proposed**.
- **Date:** 2026-11-18

## Context
Source data must be diffable, versioned, recoverable and safe to load from untrusted/hand-edited files. Never serialize memory layouts. Reference §5.7, §12.

## Decision
**Scene v1** (spec: `Documentation/Formats/scene-v1.md`):
- Human-readable **JSON** text, deterministic output (insertion-ordered keys, shortest round-trip number form, 2-space indent, trailing newline).
- Top-level `schema` = `"kengine.scene"` and integer `schema_version` = `1`. Persistent UUIDs only; no handles, indices or addresses.
- **Strict loading:** invalid JSON, wrong types, bad UUIDs, duplicate UUIDs, nil UUID, unknown kinds, missing parents, cycles and malformed transforms are errors with a stable `code` and a JSON-pointer-style path in the message.
- **Version policy:** file version greater than the build's → error ("newer"), never best-effort; version below 1 → error; equal → load. Older versions will route through a **migration registry** (ordered `vN → vN+1` steps) once a v2 exists.
- **Unknown fields:** ignored with a **warning** that says they are *not preserved on save*. Rationale: typos such as `postion` must be visible. Cost: a newer-minor producer's extra fields would be dropped on re-save by an older tool; mitigated because a higher `schema_version` is refused outright.
- File extension: `.kscene` is **proposed** (content is JSON); the library operates on strings and does not enforce an extension.

**Own JSON parser instead of a dependency:** `Engine/Serialization/Json.*`, strict RFC 8259 (no comments/trailing commas, UTF-8 validated, duplicate keys rejected, depth limit 128, non-zero→zero underflow rejected). Reason: zero third-party dependencies at M0 and no network to fetch one. It must stay small; if feature needs grow (comments, streaming, large files), replace it behind `Json.h` with a vetted library after a license/maintenance review.

**Project manifest (proposed, M0.5 project task):** JSON as well (`kproject.json`) so one parser serves all authored formats. The reference sketches TOML (`kproject.toml`); TOML is friendlier for hand-editing/comments, so revisit in M4 when manifests gain build profiles and dependencies. Decide before M4, not now.

**Saves (implemented, M05-T05):** write to a temporary file in the same directory, fsync, copy the previous file to `<name>.bak`, then atomically rename over the target; refuses non-regular targets and symlinks. See `Documentation/Formats/scene-v1.md` (File I/O). **Migrations (planned):** back up before any in-place migration; produce a migration report; refuse to overwrite when a step fails.

**Asset identity (planned, M3):** stable asset GUID in a sidecar `.meta`, independent of path; missing references reported, not silently dropped.

## Hostile-input policy (added after measurement, 2026-11-19)
Scene files are untrusted input (anyone can hand you one). Measured on the first implementation, release build, one core:
- a chain of 40,000 parent→child objects took **7.3 s** to load (cycle check per object: O(n²)); a JSON object with 200,000 keys took **140 s** (linear duplicate-key scans: O(n²)); 60,000 unknown fields produced 60,000 stored/printed warnings.
- After the fixes: the 40,000 chain loads in **0.1 s**, the 200,000-key object in **0.12 s**, 200,000 root objects in **0.4 s**, warnings are capped (100 + one summary line).
Rules now in force: loading is O(n) in objects and members at any hierarchy depth (objects are created parent-first, so no per-object cycle check; cycles are found as objects never reached from a root, and the error names an object that is *on* the cycle); duplicate-key detection is hash-based past 16 members; at most 100 warnings are kept; nesting depth ≤ 128; file size ≤ 64 MiB by default (`load_scene_file(path, max_bytes)`).
**Known remaining cost:** the in-memory JSON tree is fat. Measured peak RSS was ~274 MiB for a 17.7 MiB file (~15×), so a file at the 64 MiB default cap can transiently need on the order of 1 GiB. Bounded, but large for a low-memory machine; lower the cap or slim the `Json` node (single variant instead of five members) if it matters.
Guarded by `Tests/Unit/test_robustness.cpp` (scaling tests, cap test, cycle-reporting test, deterministic mutation fuzzer).

## Options considered
Binary scene format (rejected for source: poor diffs); TOML/YAML for scenes (parser surface/ambiguity; YAML rejected); JSON with comments (non-standard).

## Evidence (verified)
`test_json.cpp` (strictness, UTF-8, depth, number round-trip) and `test_scene_serialization.cpp` (round trip, all error classes above, stable bytes on re-save, warnings for unknown fields). `test_scene_file.cpp` (round trip, backup, failure injection at every save stage, truncation at every prefix length, size limit, non-regular/symlink handling, 5,000-object scene). Not verified: fuzzing, real disk-full/power-loss behaviour, non-POSIX platforms, any migration.

## Trade-offs
Numbers are doubles, so integers are exact only to 2^53 (fine for transforms/versions; asset hashes must be strings). JSON is verbose for large scenes; binary cooked formats are a build-time (M4) concern, not a source format.

## Rollback
Format is versioned; adding fields is additive within a version only if the loader tolerates them (it does, with warnings). Breaking changes require a new `schema_version` and a migration step.

## Validation plan
Done for files (M05-T05). Later: parser fuzzing (`Tests/Fuzz`), migration tests when a v2 exists.
