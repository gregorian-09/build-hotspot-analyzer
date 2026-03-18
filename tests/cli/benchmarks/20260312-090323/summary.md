# Repo Apply/Rebuild Benchmark Summary

- Timestamp (UTC): `2026-03-12T09:31:44Z`
- Branch: `feat/repo-wide-benchmark`
- Commit: `b898a5c831885c6ca442340ea3310a82eff3e3b4`
- Mode: `both`
- Replay selection: `discover-applied-only`
- Clean repo before case: `True`
- Tests root: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli`
- Output dir: `/home/gregorian-rayne/CLionProjects/build-hotspot-analyzer/tests/cli/benchmarks/20260312-090323`

## Overall Totals

- Records: **28**
- Attempted: **28**
- Successful: **8**
- Fleet baseline: **13071.00s**
- Fleet post-apply: **13071.00s**
- Fleet delta: **0ms (0.000%)**

## Mode Breakdown

- `discover`: records=14, attempted=14, success=4, delta=0ms (0.000%)
- `replay`: records=14, attempted=14, success=4, delta=0ms (0.000%)

## Status Counts

- `no_suggestions`: **20**
- `success`: **8**

## Per-Record Results

| Mode | Project | System | Compiler | Status | Before | After | Delta | Delta % | Applied | Found | Edits Supplied |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `discover` | `abseil` | `cmake` | `clang` | `success` | 820.59s | 820.59s | 0ms | 0.000% | 1 | 4 | 0 |
| `discover` | `benchmark` | `cmake` | `clang` | `no_suggestions` | 25.20s | 25.20s | 0ms | 0.000% | 0 | 3 | 0 |
| `discover` | `glfw` | `cmake` | `clang` | `success` | 22.40s | 22.40s | 0ms | 0.000% | 1 | 2 | 0 |
| `discover` | `leveldb` | `cmake` | `clang` | `no_suggestions` | 152.01s | 152.01s | 0ms | 0.000% | 0 | 4 | 0 |
| `discover` | `libjpeg-turbo` | `cmake` | `clang` | `no_suggestions` | 70.53s | 70.53s | 0ms | 0.000% | 0 | 2 | 0 |
| `discover` | `mimalloc` | `cmake` | `clang` | `success` | 36.55s | 36.55s | 0ms | 0.000% | 1 | 1 | 0 |
| `discover` | `rocksdb` | `cmake` | `clang` | `no_suggestions` | 9912.61s | 9912.61s | 0ms | 0.000% | 0 | 3 | 0 |
| `discover` | `yaml-cpp` | `cmake` | `clang` | `no_suggestions` | 157.39s | 157.39s | 0ms | 0.000% | 0 | 3 | 0 |
| `discover` | `zstd` | `cmake` | `clang` | `success` | 100.20s | 100.20s | 0ms | 0.000% | 1 | 1 | 0 |
| `discover` | `curl` | `make` | `clang` | `no_suggestions` | 240.92s | 240.92s | 0ms | 0.000% | 0 | 1 | 0 |
| `discover` | `libpng` | `make` | `clang` | `no_suggestions` | 7.97s | 7.97s | 0ms | 0.000% | 0 | 0 | 0 |
| `discover` | `redis` | `make` | `clang` | `no_suggestions` | 540.90s | 540.90s | 0ms | 0.000% | 0 | 4 | 0 |
| `discover` | `zlib` | `make` | `clang` | `no_suggestions` | 3.96s | 3.96s | 0ms | 0.000% | 0 | 0 | 0 |
| `discover` | `weston` | `meson` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `abseil` | `cmake` | `clang` | `success` | 820.59s | 820.59s | 0ms | 0.000% | 2 | 4 | 2 |
| `replay` | `benchmark` | `cmake` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `glfw` | `cmake` | `clang` | `success` | 22.40s | 22.40s | 0ms | 0.000% | 5 | 2 | 8 |
| `replay` | `leveldb` | `cmake` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `libjpeg-turbo` | `cmake` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `mimalloc` | `cmake` | `clang` | `success` | 36.55s | 36.55s | 0ms | 0.000% | 3 | 1 | 4 |
| `replay` | `rocksdb` | `cmake` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `yaml-cpp` | `cmake` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `zstd` | `cmake` | `clang` | `success` | 100.20s | 100.20s | 0ms | 0.000% | 6 | 1 | 6 |
| `replay` | `curl` | `make` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `libpng` | `make` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `redis` | `make` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `zlib` | `make` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
| `replay` | `weston` | `meson` | `clang` | `no_suggestions` | n/a | n/a | n/a | n/a | 0 | 0 | 0 |
