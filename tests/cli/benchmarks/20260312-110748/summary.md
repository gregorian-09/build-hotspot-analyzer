# Repo Apply/Rebuild Benchmark Summary

- Timestamp (UTC): `2026-03-12T11:20:59Z`
- Branch: `feat/repo-wide-benchmark`
- Commit: `b898a5c831885c6ca442340ea3310a82eff3e3b4`
- Mode: `discover`
- Replay selection: `discover-applied-only`
- Clean repo before case: `True`
- Tests root: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli`
- Output dir: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli/benchmarks/20260312-110748`

## Overall Totals

- Records: **1**
- Attempted: **1**
- Successful: **1**
- Fleet baseline: **712.88s**
- Fleet post-apply: **709.90s**
- Fleet delta: **2.97s (0.417%)**

## Mode Breakdown

- `discover`: records=1, attempted=1, success=1, delta=2.97s (0.417%)

## Status Counts

- `success`: **1**

## Per-Record Results

| Mode | Project | System | Compiler | Status | Before | After | Delta | Delta % | Applied | Found | Edits Supplied |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `discover` | `opencv` | `cmake` | `clang` | `success` | 712.88s | 709.90s | 2.97s | 0.417% | 1 | 2 | 0 |
