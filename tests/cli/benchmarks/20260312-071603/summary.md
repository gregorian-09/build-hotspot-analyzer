# Repo Apply/Rebuild Benchmark Summary

- Timestamp (UTC): `2026-03-12T07:22:43Z`
- Branch: `feat/repo-wide-benchmark`
- Commit: `b898a5c831885c6ca442340ea3310a82eff3e3b4`
- Mode: `both`
- Replay selection: `discover-applied-only`
- Clean repo before case: `True`
- Tests root: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli`
- Output dir: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli/benchmarks/20260312-071603`

## Overall Totals

- Records: **6**
- Attempted: **6**
- Successful: **2**
- Fleet baseline: **73.11s**
- Fleet post-apply: **73.11s**
- Fleet delta: **0ms (0.000%)**

## Mode Breakdown

- `discover`: records=3, attempted=3, success=1, delta=0ms (0.000%)
- `replay`: records=3, attempted=3, success=1, delta=0ms (0.000%)

## Status Counts

- `rollback_triggered`: **4**
- `success`: **2**

## Per-Record Results

| Mode | Project | System | Compiler | Status | Before | After | Delta | Delta % | Applied | Found | Edits Supplied |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `discover` | `abseil` | `cmake` | `clang` | `rollback_triggered` | 820.59s | n/a | n/a | n/a | 3 | 4 | 0 |
| `discover` | `benchmark` | `cmake` | `clang` | `rollback_triggered` | 25.20s | n/a | n/a | n/a | 1 | 3 | 0 |
| `discover` | `mimalloc` | `cmake` | `clang` | `success` | 36.55s | 36.55s | 0ms | 0.000% | 1 | 1 | 0 |
| `replay` | `abseil` | `cmake` | `clang` | `rollback_triggered` | 820.59s | n/a | n/a | n/a | 20 | 4 | 66 |
| `replay` | `benchmark` | `cmake` | `clang` | `rollback_triggered` | 25.20s | n/a | n/a | n/a | 10 | 3 | 68 |
| `replay` | `mimalloc` | `cmake` | `clang` | `success` | 36.55s | 36.55s | 0ms | 0.000% | 3 | 1 | 4 |

## Failures / Skips

- `discover:cmake/clang/abseil`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/abseil`: `rollback_triggered` (rollback-succeeded)
- `discover:cmake/clang/benchmark`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/benchmark`: `rollback_triggered` (rollback-succeeded)
