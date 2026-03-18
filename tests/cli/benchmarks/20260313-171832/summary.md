# Repo Apply/Rebuild Benchmark Summary

- Timestamp (UTC): `2026-03-13T17:19:08Z`
- Branch: `feat/pimpl-auto-apply-safe`
- Commit: `1e2bde816d638d6e7f4cf81b5b1a8c0022638ce2`
- Mode: `discover`
- Replay selection: `discover-applied-only`
- Clean repo before case: `True`
- Tests root: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli`
- Output dir: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli/benchmarks/20260313-171832`

## Overall Totals

- Records: **1**
- Attempted: **1**
- Successful: **1**
- Fleet baseline: **22.40s**
- Fleet post-apply: **4.04s**
- Fleet delta: **18.36s (81.966%)**

## Mode Breakdown

- `discover`: records=1, attempted=1, success=1, delta=18.36s (81.966%)

## Status Counts

- `success`: **1**

## Per-Record Results

| Mode | Project | System | Compiler | Status | Before | After | Delta | Delta % | Applied | Found | Edits Supplied |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `discover` | `glfw` | `cmake` | `clang` | `success` | 22.40s | 4.04s | 18.36s | 81.966% | 1 | 2 | 0 |
