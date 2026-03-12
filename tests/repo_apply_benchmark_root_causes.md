# Repo Apply Benchmark: Findings and Fix Log

Date: `2026-03-12`

## Scope

- Runner: `tests/run_repo_apply_benchmark.py`
- LSP path: `bha.analyze`, `bha.applyAllSuggestions`, `bha.applyEdits`
- Matrix root: `tests/cli`

## Findings

1. Replay path over-applied edits.
- Replay consumed all captured `textEdits`, including edits not proven build-safe by discover.
- Result: inflated replay failures and weak parity with discover output.

2. Repo state was non-deterministic between cases.
- Dirty repos under `tests/cli/repos/*` contaminated later cases.
- Result: inconsistent attribution of failures.

3. Failure payload capture was incomplete.
- Status and reason existed, but apply/build diagnostics were not fully persisted.

4. Replay executed after failed discover in combined mode.
- `--mode both` produced predictable duplicate failures.

5. LSP runtime settings were not being applied in-session.
- `initialize` settings were passed, but `workspace/didChangeConfiguration` was not emitted.
- Server defaults remained active in some runs (notably default build-timeout behavior).

6. Include-cleanup auto-apply in vendored code was fragile.
- Vendor tree edits (for example under `3rdparty`) generated low-value breakages.

## Implemented fixes

1. Replay selection policy hardening.
- Added `--replay-selection` with:
- `discover-applied-only` (default)
- `auto-direct-only`
- `all-edits`
- Default replay now follows discover-applied suggestion IDs.

2. Applied IDs surfaced from LSP.
- `bha.applyAllSuggestions` now returns `appliedSuggestionIds`.

3. Deterministic repo cleanup.
- Added controls:
- `--clean-repo-before-case` (default on)
- `--no-clean-repo-before-case`
- Cleanup sequence: `git reset --hard` + `git clean -fd`.

4. Failure diagnostics persisted in benchmark records.
- `applyErrors` captured and written into `results.json`.
- Record reason enriched with first diagnostic message where available.

5. Replay gating behavior adjusted.
- Combined mode no longer replays failed discover outputs as hard failures.

6. Runtime config application fixed.
- Runner now emits `workspace/didChangeConfiguration` immediately after `initialize`.

7. Conservative safety filter for auto-apply include cleanup.
- Discover auto-apply skips include-cleanup suggestions that touch:
- `/3rdparty/`, `/third_party/`, `/vendor/`, `/external/`, `/subprojects/`

## Validation summary

1. Canary run (`glfw`) succeeded in discover and replay paths.
2. Targeted subset (`abseil`, `benchmark`, `mimalloc`) no longer produced duplicate replay failures from failed discover state.
3. Full matrix run completed with clear status accounting and persisted diagnostics.

## OpenCV onboarding log

Project setup:
- Repo: `tests/cli/repos/opencv` (branch `4.x`)
- Matrix case: `tests/cli/cmake/clang/opencv`
- Build flags in `lsp/tests/lsp_test_client.py`:
- `-DBUILD_LIST=core,imgproc,imgcodecs`
- `-DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF`

Key run:
- Artifacts: `tests/cli/benchmarks/20260312-110748`
- Discover status: `success`
- Baseline: `712.88s`
- Post-apply: `709.90s`
- Delta: `+2.97s` (`+0.417%`)
- Applied change: PCH integration (`pch.h` + `3rdparty/tbb/CMakeLists.txt`)

## Remaining risk

- Some discover failures remain genuine suggester/build interactions (for example include/template/PCH interactions) and require suggester-level hardening rather than harness-level changes.
