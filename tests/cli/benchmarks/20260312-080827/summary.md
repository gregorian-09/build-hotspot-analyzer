# Repo Apply/Rebuild Benchmark Summary

- Timestamp (UTC): `2026-03-12T08:29:50Z`
- Branch: `feat/repo-wide-benchmark`
- Commit: `b898a5c831885c6ca442340ea3310a82eff3e3b4`
- Mode: `both`
- Replay selection: `discover-applied-only`
- Clean repo before case: `True`
- Tests root: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli`
- Output dir: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli/benchmarks/20260312-080827`

## Overall Totals

- Records: **28**
- Attempted: **28**
- Successful: **6**
- Fleet baseline: **330.25s**
- Fleet post-apply: **330.25s**
- Fleet delta: **0ms (0.000%)**

## Mode Breakdown

- `discover`: records=14, attempted=14, success=3, delta=0ms (0.000%)
- `replay`: records=14, attempted=14, success=3, delta=0ms (0.000%)

## Status Counts

- `apply_failed`: **2**
- `build_failed`: **2**
- `no_suggestions`: **2**
- `precondition_failed`: **9**
- `rollback_triggered`: **7**
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
| `discover` | `rocksdb` | `cmake` | `clang` | `rollback_triggered` | 9912.61s | n/a | n/a | n/a | 3 | 3 | 0 |
| `discover` | `yaml-cpp` | `cmake` | `clang` | `rollback_triggered` | 157.39s | n/a | n/a | n/a | 2 | 3 | 0 |
| `discover` | `zstd` | `cmake` | `clang` | `success` | 100.20s | 100.20s | 0ms | 0.000% | 1 | 1 | 0 |
| `discover` | `curl` | `make` | `clang` | `build_failed` | 240.92s | n/a | n/a | n/a | 0 | 1 | 0 |
| `discover` | `libpng` | `make` | `clang` | `no_suggestions` | 7.97s | 7.97s | 0ms | 0.000% | 0 | 0 | 0 |
| `discover` | `redis` | `make` | `clang` | `rollback_triggered` | 540.90s | n/a | n/a | n/a | 1 | 4 | 0 |
| `discover` | `zlib` | `make` | `clang` | `no_suggestions` | 3.96s | 3.96s | 0ms | 0.000% | 0 | 0 | 0 |
| `discover` | `weston` | `meson` | `clang` | `build_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `abseil` | `cmake` | `clang` | `precondition_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `benchmark` | `cmake` | `clang` | `precondition_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `glfw` | `cmake` | `clang` | `success` | 22.40s | 22.40s | 0ms | 0.000% | 5 | 2 | 8 |
| `replay` | `leveldb` | `cmake` | `clang` | `precondition_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `libjpeg-turbo` | `cmake` | `clang` | `precondition_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `mimalloc` | `cmake` | `clang` | `success` | 36.55s | 36.55s | 0ms | 0.000% | 3 | 1 | 4 |
| `replay` | `rocksdb` | `cmake` | `clang` | `precondition_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `yaml-cpp` | `cmake` | `clang` | `precondition_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `zstd` | `cmake` | `clang` | `success` | 100.20s | 100.20s | 0ms | 0.000% | 6 | 1 | 6 |
| `replay` | `curl` | `make` | `clang` | `precondition_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `libpng` | `make` | `clang` | `apply_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `redis` | `make` | `clang` | `precondition_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `zlib` | `make` | `clang` | `apply_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `weston` | `meson` | `clang` | `precondition_failed` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |

## Failures

- `discover:cmake/clang/abseil`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/abseil`: `precondition_failed` (discover-status-rollback_triggered)
- `discover:cmake/clang/benchmark`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/benchmark`: `precondition_failed` (discover-status-rollback_triggered)
- `discover:cmake/clang/leveldb`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/leveldb`: `precondition_failed` (discover-status-rollback_triggered)
- `discover:cmake/clang/libjpeg-turbo`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/libjpeg-turbo`: `precondition_failed` (discover-status-rollback_triggered)
- `discover:cmake/clang/rocksdb`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/rocksdb`: `precondition_failed` (discover-status-rollback_triggered)
- `discover:cmake/clang/yaml-cpp`: `rollback_triggered` (rollback-succeeded)
- `replay:cmake/clang/yaml-cpp`: `precondition_failed` (discover-status-rollback_triggered)
- `discover:make/clang/curl`: `build_failed` (Build failed with exit code 1)
- `replay:make/clang/curl`: `precondition_failed` (discover-status-build_failed)
- `replay:make/clang/libpng`: `apply_failed` (replay-edits-empty-after-filter)
- `discover:make/clang/redis`: `rollback_triggered` (rollback-succeeded)
- `replay:make/clang/redis`: `precondition_failed` (discover-status-rollback_triggered)
- `replay:make/clang/zlib`: `apply_failed` (replay-edits-empty-after-filter)
- `discover:meson/clang/weston`: `build_failed` (trace-bootstrap-failed-rc1)
- `replay:meson/clang/weston`: `precondition_failed` (discover-status-build_failed)
