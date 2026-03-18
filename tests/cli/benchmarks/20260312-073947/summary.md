# Repo Apply/Rebuild Benchmark Summary

- Timestamp (UTC): `2026-03-12T07:40:48Z`
- Branch: `feat/repo-wide-benchmark`
- Commit: `b898a5c831885c6ca442340ea3310a82eff3e3b4`
- Mode: `discover`
- Replay selection: `discover-applied-only`
- Clean repo before case: `True`
- Tests root: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli`
- Output dir: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli/benchmarks/20260312-073947`

## Overall Totals

- Records: **1**
- Attempted: **1**
- Successful: **0**
- Fleet baseline: **0ms**
- Fleet post-apply: **0ms**
- Fleet delta: **0ms (0.000%)**

## Mode Breakdown

- `discover`: records=1, attempted=1, success=0, delta=0ms (0.000%)

## Status Counts

- `rollback_triggered`: **1**

## Per-Record Results

| Mode | Project | System | Compiler | Status | Before | After | Delta | Delta % | Applied | Found | Edits Supplied |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `discover` | `benchmark` | `cmake` | `clang` | `rollback_triggered` | 25.20s | n/a | n/a | n/a | 1 | 3 | 0 |

## Failures / Skips

- `discover:cmake/clang/benchmark`: `rollback_triggered` (rollback-succeeded)
