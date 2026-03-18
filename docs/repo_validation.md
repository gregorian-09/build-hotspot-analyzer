# Repository Validation and Benchmark Methodology

## 1. Purpose

This document describes how BHA is validated on real-world open-source repositories, not just synthetic fixtures.

Goals:
- verify parser and suggester behavior against varied build systems
- validate auto-apply + rebuild + rollback workflows
- measure baseline vs post-apply build deltas
- capture reproducible artifacts for analysis and regression tracking

## 2. Validation Toolchain in This Repo

Primary scripts:
- `tests/clone_repos.sh`
- `tests/build_repos.sh`
- `tests/run_repo_apply_benchmark.py`
- `lsp/tests/lsp_test_client.py`
- `tests/repo_apply_benchmark_root_causes.md` (issue/fix log)

Main benchmark output root:
- `tests/cli/benchmarks/`

Per-run artifacts:
- `results.json`
- `summary.json`
- `summary.md`
- `failures.json`
- `logs/`
- `suggestion_edits/`

## 3. Real Repository Matrix (Configured)

Defined in `tests/clone_repos.sh` and build settings in `tests/build_repos.sh`.

### CMake

- abseil
- benchmark
- catch2
- glfw
- leveldb
- libjpeg-turbo
- mimalloc
- rocksdb
- yaml-cpp
- zstd

### Make

- curl
- libpng
- redis
- zlib

### Meson

- weston

Additional heavy project under `tests/cli/repos/`:
- opencv

## 4. End-to-End Benchmark Flow

1. Discover project/build-system/compiler cases from `tests/cli`.
2. Reset each repo (`git reset --hard`, `git clean -fd`) unless disabled.
3. Bootstrap traces if missing.
4. Start LSP server and run `bha.analyze`.
5. Discover mode:
   - apply suggestions (`bha.applyAllSuggestions`)
   - rebuild validation
   - capture baseline/post timings
6. Replay mode:
   - reapply persisted suggestion edits
   - rebuild + record deltas
7. Persist result artifacts.

Command modes:
- `discover`
- `replay`
- `both`

## 5. Delta Semantics

In benchmark outputs:
- `deltaMs = baselineTimeMs - postApplyTimeMs`
- positive delta means **faster after apply**
- negative delta means **slower after apply**

## 6. Representative Recorded Runs (From Artifacts)

The entries below come from committed benchmark artifacts in `tests/cli/benchmarks/`.

### Full matrix run snapshot

Run: `tests/cli/benchmarks/20260312-090323/summary.md`

- Records: 28
- Successful: 8
- Mixed project/build-system coverage across CMake, Make, Meson
- Includes abseil/glfw/mimalloc/zstd success cases and no-suggestion cases for others

### OpenCV iterative run (regression then improvement)

Runs:
- `tests/cli/benchmarks/20260312-104527/summary.md`
- `tests/cli/benchmarks/20260312-110748/summary.md`

Observed:
- earlier run: `-5.61s` delta (`-0.793%`) (regression)
- later run: `+2.97s` delta (`+0.417%`) (improvement)

Related investigation log:
- `tests/repo_apply_benchmark_root_causes.md`

### GLFW run

Run: `tests/cli/benchmarks/20260313-171832/summary.md`

Observed:
- baseline: `22.40s`
- post-apply: `4.04s`
- delta: `+18.36s` (`+81.966%`)

## 7. Root-Cause Driven Hardening

Tracked in:
- `tests/repo_apply_benchmark_root_causes.md`

Implemented hardening themes include:
- deterministic repo cleanup
- replay-selection policy controls
- diagnostics persistence in benchmark records
- in-session LSP runtime config application
- safety filtering for fragile vendor-tree include cleanup

## 8. Practical Guidance for Running Benchmarks

Example:

```bash
python3 tests/run_repo_apply_benchmark.py \
  --mode discover \
  --compiler clang \
  --build-system all \
  --analysis-timeout 300 \
  --apply-timeout 7800 \
  --build-timeout 7200
```

Useful controls:
- `--project <name>` (repeatable)
- `--exclude-project <name>`
- `--include-rocksdb`
- `--clean-repo-before-case` / `--no-clean-repo-before-case`
- `--replay-selection discover-applied-only|auto-direct-only|all-edits`

## 9. Why This Matters for Architecture

These validations verify that:
- the parser/analyzer/suggester contracts hold under non-trivial codebases
- auto-apply decisions survive real build graph and build-system complexity
- rollback and trust-loop behavior remains stable under failures
- CI outputs (summary + diagnostics artifacts) are actionable, not opaque

