# Repo Apply/Rebuild Benchmark Summary

- Timestamp (UTC): `2026-03-12T08:03:30Z`
- Branch: `feat/repo-wide-benchmark`
- Commit: `b898a5c831885c6ca442340ea3310a82eff3e3b4`
- Mode: `both`
- Replay selection: `discover-applied-only`
- Clean repo before case: `False`
- Tests root: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli`
- Output dir: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli/benchmarks/20260312-080231`

## Overall Totals

- Records: **2**
- Attempted: **2**
- Successful: **1**
- Fleet baseline: **22.40s**
- Fleet post-apply: **22.40s**
- Fleet delta: **0ms (0.000%)**

## Mode Breakdown

- `discover`: records=1, attempted=1, success=1, delta=0ms (0.000%)
- `replay`: records=1, attempted=1, success=0, delta=0ms (0.000%)

## Status Counts

- `rollback_triggered`: **1**
- `success`: **1**

## Per-Record Results

| Mode | Project | System | Compiler | Status | Before | After | Delta | Delta % | Applied | Found | Edits Supplied |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `discover` | `glfw` | `cmake` | `clang` | `success` | 22.40s | 22.40s | 0ms | 0.000% | 1 | 2 | 0 |
| `replay` | `glfw` | `cmake` | `clang` | `rollback_triggered` | 22.40s | n/a | n/a | n/a | 5 | 2 | 8 |

## Failures

- `replay:cmake/clang/glfw`: `rollback_triggered` (rollback-succeeded)
