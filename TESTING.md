# Testing

Reports must distinguish **passed / failed / skipped / not-run**. The unit runner prints `passed= failed= not_run=`; a run in which zero cases execute (bad filter) exits 2.

## Layers that exist
| Layer | Location | Command | What it covers |
|---|---|---|---|
| Unit | `Tests/Unit/*.cpp` (in-repo `KTEST` harness) | `make test` | UUID, handles/SlotMap (+ typed-handle compile-time checks), Scene, JSON, scene serialization, scene file I/O, math, world transforms, primitive meshes, orbit camera, picking, draw list (temp dirs under the system temp path, removed after each test) |
| CLI | `Tests/CLI/test_cli.py` (Python `unittest`) | `make cli-test` | `k --version`, `--help`, `doctor` (+`--json`), usage errors, exit codes |
| Sanitizers | same tests | `make SANITIZE=1 check` | ASan + UBSan on all of the above |

Filter unit cases: `Build/make-debug/k_unit_tests <substring>`; list: `--list`.

## Layers that do NOT exist yet
Integration (Editor workflows), GUI, rendering/screenshot, determinism, performance/benchmarks, fuzzing (JSON/scene parsers are good first targets), coverage, CI. Nothing in these layers has been run; none is claimed.

## Latest recorded results
See `DEVELOPMENT_STATUS.md` ("Verification") for exact command output summaries and the environment.

## Conventions
- Prefer failure-path tests; match errors by `code`, not message text.
- Do not weaken an assertion to make a test pass unless the underlying decision is recorded (example: JSON underflow policy, ADR-0006).
- Randomized tests use fixed seeds so failures reproduce.

## Notes on file-I/O tests
- Failure paths of the atomic save are exercised with the test-only `SaveOptions::fault_injection` hook (stages `create_temp`, `write`, `sync`, `backup`, `rename`). They prove cleanup and error mapping, not behaviour on a genuinely failing disk.
- The suite was developed running as `root`, which ignores file permissions, so "read-only directory" cases are deliberately **not** tested; do not add permission-based tests without handling that.
- The `KCHECK_EQ` macro copies its operands (a dangling-reference bug with rvalue-derived references was found and fixed during M05-T05).

## Notes on rendering-support tests
- Geometry is checked structurally, not by eye: unit normals, winding agrees with normals, no degenerate triangles, every directed edge matched by exactly one opposite edge (closed, consistently oriented surface), expected areas.
- Picking and camera tests compare against hand-computed distances (e.g. rotated and non-uniformly scaled cubes, children of scaled/rotated parents) and round-trip projected points through `screen_ray`.
- **Not tested:** anything that draws pixels. `Editor/Spike` (Direct3D 11, Qt) has never been compiled or run. Its syntax was checked only against hand-written stub headers (this catches typos/type errors, not API misuse).
- The test runner now converts an uncaught exception into a reported failure instead of aborting.
