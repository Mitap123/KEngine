# Testing

Reports must distinguish **passed / failed / skipped / not-run**. The unit runner prints `passed= failed= not_run=`; a run in which zero cases execute (bad filter) exits 2.

## Layers that exist
| Layer | Location | Command | What it covers |
|---|---|---|---|
| Unit | `Tests/Unit/*.cpp` (in-repo `KTEST` harness) | `make test` | UUID, handles/SlotMap (+ typed-handle compile-time checks), Scene, JSON, scene serialization, scene file I/O, math, world transforms, primitive meshes, orbit camera, picking, draw list, command history/undo-redo, SceneEditor, Play isolation (temp dirs under the system temp path, removed after each test) |
| CLI | `Tests/CLI/test_cli.py` (Python `unittest`) | `make cli-test` | `k --version`, `--help`, `doctor` (+`--json`), `scene validate`/`dump` (valid, invalid, hostile, deep files), usage errors, exit codes, and `--log-file` (event sequence, append, usage/failed-open/failed-write behaviour, output unchanged by logging, invalid-UTF-8 and control characters in names cannot corrupt or forge log lines; the failed-write case uses `/dev/full` and is skipped where it does not exist) |
| Sanitizers | same tests | `make SANITIZE=1 check` | ASan + UBSan on all of the above |

**Fast vs full unit tests:** by default the heavy tests use reduced sizes (the first Windows Debug run took 311 s; Linux Debug was 35 s, now 4.3 s). `KENGINE_TEST_FULL=1` (`make check-full`, or `-DKENGINE_FULL_TESTS=ON` and ctest `unit_full`) runs the full sizes: 100,000-object hierarchies, full fuzz sweeps, 1,500-step undo model. Run the full mode before a release or after touching loading/hierarchy code. The runner prints per-test milliseconds.

Filter unit cases: `Build/make-debug/k_unit_tests <substring>`; list: `--list`.

| Visual self-test | `Tests/Visual/test_png_probe.py` | `make visual-selftest` | the PNG reader (all filter types, rejection of unsupported files; cross-checked once against PIL's independent encoder) and the black/lit pixel analysis, on synthetic images |
| Visual probe (Windows) | `Tests/Visual/test_viewport_screenshot.py` | ctest `viewport_screenshot` (when `-DKENGINE_BUILD_EDITOR_SPIKE=ON`) | runs `k_viewport_spike --screenshot`, fails if surfaces render black or too little lit geometry is visible. **Added after the first hardware run found all-black surfaces; not yet run by the owner** |

| Reparent (Qt-free) | `Tests/Unit/test_reparent.cpp` | part of `make test` | `decompose_trs` round trips (random, gimbal, reflection) and rejections (shear, zero axis, perspective, NaN); KeepWorld/KeepLocal/index/rollback/undo-exactness; a randomized check that every object's world matrix survives each move and undo is byte-exact; document no-op, `can_reparent`, Play refusal |
| Composite / Duplicate (Qt-free) | `Tests/Unit/test_composite.cpp` | part of `make test` | composite apply/undo/redo and both rollback paths (failing step; undo after the scene was changed behind the history), empty list; Duplicate of leaves and subtrees (UUIDs all distinct, transforms and names kept, placement among siblings, one labelled undo step, redo keeps UUIDs), failures change nothing, Play refusal, randomized duplicate/undo/redo; document naming ("Cube" -> "Cube 2" -> "Cube 3") |
| Editor core (Qt-free) | `Tests/Unit/test_editor_core.cpp` | part of `make test` | document/selection/file/Play behaviour, Explorer rows, Inspector axis edits, play demo, camera framing (all 8 corners of every object inside the frustum), demo scene, Hello Cube workflow |
| Editor UI smoke (Windows) | ctest `editor_smoke` (`k_editor --smoke`) | `-DKENGINE_BUILD_EDITOR=ON` | drives the real widgets through the workflow. **Not yet run** |

## Layers that do NOT exist yet
Integration (Editor workflows), GUI interaction, determinism, performance/benchmarks, fuzzing (JSON/scene parsers are good first targets), coverage. Nothing in these layers has been run; none is claimed.

**CI:** `.github/workflows/ci.yml` exists (Linux: `make` Debug / ASan+UBSan / Release, all `STRICT=1`; Windows: CMake + MSVC build and ctest, no Qt). It has **not been run on GitHub yet**; the Linux commands were run locally with identical results (see DEVELOPMENT_STATUS.md). Editor/Qt tests and the GPU spike are not in CI.

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

## Notes on editing-model tests
- Undo/redo is verified by comparing the *serialized scene text* (UUIDs, names, kinds, transforms, hierarchy, sibling order) with a reference after every step, including a randomized run (fixed seed, 1,500 steps, 40% edit / 30% undo / 30% redo) that also asserts it exercised enough edits, rejected edits, undos and redos to be meaningful. The first version of that mix (70% edits) failed its own coverage guard; the mix was rebalanced rather than the threshold lowered.
- One mutation (merge-into-saved-step stays clean) survived; analysis showed the guarded branch was unreachable through the public API, so the dead code and its misleading test were removed instead of testing around them.
- A scene-file/CLI test with a hostile name (newline, ESC, BEL) asserts the text output cannot be forged.

## Notes on robustness / hostile-input tests (`Tests/Unit/test_robustness.cpp`)
- **Complexity guards** are timing-based: they load an input of size n and 4n and fail if the larger one costs more than ~10x the smaller (linear ~4x, the old quadratic code ~22x). A heavily loaded machine can make them flaky; if one fails, rerun before suspecting the code.
- **Deterministic mutation fuzzer:** every prefix, every single-byte deletion and 16 replacement bytes at every position of four seed files (~45k mutants), plus random multi-edit mutants (default 20,000; set `KENGINE_FUZZ_ITERATIONS` for more). Invariants: no crash/sanitizer report, stable error codes, `dump` is a fixed point, and anything loaded can be saved and reloaded to identical bytes. The run asserts it hit both accepted and rejected outcomes. **Not** coverage-guided and not libFuzzer; a long coverage-guided campaign has not been done.
- At the iteration counts above the fuzzer found **no** defects. The real defects (two O(n²) loaders, unbounded warnings) were found by measuring pathological files.
- 7 of 8 hardening mutations were caught outright; one (a dump that drops a `\n` case) was an equivalent mutant (the default branch still escapes it as `\u000a`), and its non-equivalent variant (raw newline) is caught by 3 tests.
