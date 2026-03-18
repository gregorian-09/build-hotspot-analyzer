# Repo Apply/Rebuild Benchmark Summary

- Timestamp (UTC): `2026-03-12T07:15:54Z`
- Branch: `feat/repo-wide-benchmark`
- Commit: `b898a5c831885c6ca442340ea3310a82eff3e3b4`
- Mode: `both`
- Replay selection: `discover-applied-only`
- Clean repo before case: `True`
- Tests root: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli`
- Output dir: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli/benchmarks/20260312-071433`

## Overall Totals

- Records: **2**
- Attempted: **2**
- Successful: **2**
- Fleet baseline: **44.80s**
- Fleet post-apply: **44.80s**
- Fleet delta: **0ms (0.000%)**

## Mode Breakdown

- `discover`: records=1, attempted=1, success=1, delta=0ms (0.000%)
- `replay`: records=1, attempted=1, success=1, delta=0ms (0.000%)

## Status Counts

- `success`: **2**

## Per-Record Results

| Mode | Project | System | Compiler | Status | Before | After | Delta | Delta % | Applied | Found | Edits Supplied |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `discover` | `glfw` | `cmake` | `clang` | `success` | 22.40s | 22.40s | 0ms | 0.000% | 1 | 2 | 0 |
| `replay` | `glfw` | `cmake` | `clang` | `success` | 22.40s | 22.40s | 0ms | 0.000% | 5 | 2 | 8 |
