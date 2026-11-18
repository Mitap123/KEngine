# ADR-0008: Determinism requirements and limits

- **Status:** accepted (scope statement for M0–M0.5).
- **Date:** 2026-11-18

## Context
Simulation-ready design needs headless stepping and, where guaranteed, determinism/replay (M6). Overclaiming determinism is a correctness and trust risk. Reference §6.3, §4.5 of the master prompt.

## Decision
- **M0–M0.5 promise no runtime determinism** because there is no runtime loop yet.
- What *is* deterministic and tested now: scene serialization output (same scene state → identical bytes, stable ordering) and `Scene` operation results for the same call sequence.
- What is intentionally **non-deterministic**: `Uuid::generate()` (random). Tests inject bits via `Uuid::from_random_words`.
- Future (M6) determinism will be **opt-in per declared subsystem**, defined by: fixed timestep, seeded RNG with serializable state, recorded input/events, state hashing, no wall-clock reads in simulation, no unordered-container iteration in simulation paths, and compiler flags that preserve IEEE semantics (no fast-math) for those targets.
- **Not promised:** cross-platform / cross-compiler bit-identical floating-point or physics results; any fixed headless speed multiplier (throughput is measured and reported with workload/hardware).

## Evidence
Verified only: stable save output (`scene_serialization_round_trip...` compares bytes of a re-save).

## Consequences
Any later claim of determinism must name the subsystem, platform/compiler scope and the test that proves it.

## Validation plan
M6: replay equality tests and state-hash comparisons for declared scopes on named toolchains; documented known limitations.
