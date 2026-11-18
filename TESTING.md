# Testing

Reports must distinguish **passed / failed / skipped / not-run**. The unit runner prints `passed= failed= not_run=`; a run in which zero cases execute (bad filter) exits 2.

## Layers that exist
| Layer | Location | Command | What it covers |
|---|---|---|---|
| Unit | `Tests/Unit/*.cpp` (in-repo `KTEST` harness) | `make test` | UUID, handles/SlotMap, Scene, JSON, scene serialization |
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
