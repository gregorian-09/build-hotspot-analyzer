# Repo Apply/Rebuild Benchmark Summary

- Timestamp (UTC): `2026-03-12T07:39:00Z`
- Branch: `feat/repo-wide-benchmark`
- Commit: `b898a5c831885c6ca442340ea3310a82eff3e3b4`
- Mode: `both`
- Replay selection: `discover-applied-only`
- Clean repo before case: `True`
- Tests root: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli`
- Output dir: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli/benchmarks/20260312-072808`

## Overall Totals

- Records: **28**
- Attempted: **15**
- Successful: **6**
- Fleet baseline: **330.25s**
- Fleet post-apply: **330.25s**
- Fleet delta: **0ms (0.000%)**

## Mode Breakdown

- `discover`: records=14, attempted=12, success=3, delta=0ms (0.000%)
- `replay`: records=14, attempted=3, success=3, delta=0ms (0.000%)

## Status Counts

- `build_failed`: **1**
- `no_suggestions`: **2**
- `rollback_triggered`: **6**
- `skipped`: **13**
- `success`: **6**

## Per-Record Results

| Mode | Project | System | Compiler | Status | Before | After | Delta | Delta % | Applied | Found | Edits Supplied |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `discover` | `abseil` | `cmake` | `clang` | `rollback_triggered` | 820.59s | n/a | n/a | n/a | 3 | 4 | 0 |
| `discover` | `benchmark` | `cmake` | `clang` | `rollback_triggered` | 25.20s | n/a | n/a | n/a | 1 | 3 | 0 |
| `discover` | `glfw` | `cmake` | `clang` | `success` | 22.40s | 22.40s | 0ms | 0.000% | 1 | 2 | 0 |
| `discover` | `leveldb` | `cmake` | `clang` | `rollback_triggered` | 152.01s | n/a | n/a | n/a | 2 | 4 | 0 |
| `discover` | `libjpeg-turbo` | `cmake` | `clang` | `rollback_triggered` | 70.53s | n/a | n/a | n/a | 1 | 2 | 0 |
| `discover` | `mimalloc` | `cmake` | `clang` | `success` | 36.55s | 36.55s | 0ms | 0.000% | 1 | 1 | 0 |
| `discover` | `rocksdb` | `cmake` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `discover` | `yaml-cpp` | `cmake` | `clang` | `rollback_triggered` | 157.39s | n/a | n/a | n/a | 2 | 3 | 0 |
| `discover` | `zstd` | `cmake` | `clang` | `success` | 100.20s | 100.20s | 0ms | 0.000% | 1 | 1 | 0 |
| `discover` | `curl` | `make` | `clang` | `build_failed` | 240.92s | n/a | n/a | n/a | 0 | 1 | 0 |
| `discover` | `libpng` | `make` | `clang` | `no_suggestions` | 7.97s | 7.97s | 0ms | 0.000% | 0 | 0 | 0 |
| `discover` | `redis` | `make` | `clang` | `rollback_triggered` | 540.90s | n/a | n/a | n/a | 1 | 4 | 0 |
| `discover` | `zlib` | `make` | `clang` | `no_suggestions` | 3.96s | 3.96s | 0ms | 0.000% | 0 | 0 | 0 |
| `discover` | `weston` | `meson` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `abseil` | `cmake` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `benchmark` | `cmake` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `glfw` | `cmake` | `clang` | `success` | 22.40s | 22.40s | 0ms | 0.000% | 5 | 2 | 8 |
| `replay` | `leveldb` | `cmake` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `libjpeg-turbo` | `cmake` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `mimalloc` | `cmake` | `clang` | `success` | 36.55s | 36.55s | 0ms | 0.000% | 3 | 1 | 4 |
| `replay` | `rocksdb` | `cmake` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `yaml-cpp` | `cmake` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `zstd` | `cmake` | `clang` | `success` | 100.20s | 100.20s | 0ms | 0.000% | 6 | 1 | 6 |
| `replay` | `curl` | `make` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `libpng` | `make` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `redis` | `make` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `zlib` | `make` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `weston` | `meson` | `clang` | `skipped` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |

## Failures / Skips

- `discover:cmake/clang/abseil`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/abseil`: `skipped` (discover-status-rollback_triggered)
- `discover:cmake/clang/benchmark`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/benchmark`: `skipped` (discover-status-rollback_triggered)
- `discover:cmake/clang/leveldb`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/leveldb`: `skipped` (discover-status-rollback_triggered)
- `discover:cmake/clang/libjpeg-turbo`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/libjpeg-turbo`: `skipped` (discover-status-rollback_triggered)
- `discover:cmake/clang/rocksdb`: `skipped` (excluded-project)
- `replay:cmake/clang/rocksdb`: `skipped` (discover-status-skipped)
- `discover:cmake/clang/yaml-cpp`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/yaml-cpp`: `skipped` (discover-status-rollback_triggered)
- `discover:make/clang/curl`: `build_failed` (Build failed with exit code 1)
- `replay:make/clang/curl`: `skipped` (discover-status-build_failed)
- `replay:make/clang/libpng`: `skipped` (discover-applied-ids-missing)
- `discover:make/clang/redis`: `rollback_triggered` (rollback-succeeded)
- `replay:make/clang/redis`: `skipped` (discover-status-rollback_triggered)
- `replay:make/clang/zlib`: `skipped` (discover-applied-ids-missing)
- `discover:meson/clang/weston`: `skipped` (traces-missing)
- `replay:meson/clang/weston`: `skipped` (discover-status-skipped)
