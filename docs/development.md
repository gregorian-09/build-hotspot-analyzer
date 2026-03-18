# Development Guide

## Local Workflow

1. Configure:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

2. Build:

```bash
cmake --build build -j
```

3. Test:

```bash
ctest --test-dir build --output-on-failure
```

## Codebase Areas

- Parsers: `headers/bha/parsers`, `sources/bha/parsers`
- Analyzers: `headers/bha/analyzers`, `sources/bha/analyzers`
- Suggesters: `headers/bha/suggestions`, `sources/bha/suggestions`
- Build adapters: `headers/bha/build_systems`, `sources/bha/build_systems`
- Exporters: `headers/bha/exporters`, `sources/bha/exporters`
- Storage/compare: `headers/bha/storage.hpp`, `sources/bha/storage`
- CLI: `cli/`
- LSP: `lsp/`
- Tests: `tests/`

## Test Coverage Snapshot

Current suite includes parser, analyzer, suggester, exporter, storage, git, and build-adapter tests.

Current ctest inventory (local run on 2026-03-15): `338` tests.

## Adding New Functionality

### Add a parser

1. Implement `ITraceParser`.
2. Register in `register_all_parsers()`.
3. Add parser-focused unit tests.
4. Validate with mixed trace corpus.

### Add an analyzer

1. Implement `IAnalyzer`.
2. Register in `register_all_analyzers()`.
3. Verify merged output in `run_full_analysis`.
4. Add targeted tests and boundary cases.

### Add a suggester

1. Implement `ISuggester`.
2. Register in `register_all_suggesters()`.
3. Add descriptor coverage (`list/describe` path).
4. Provide explainability origins and safety metadata.
5. Add tests for:
   - positive suggestion cases
   - false-positive guards
   - auto-apply edit correctness (if applicable)

## Style and Quality

- C++ standard: C++20
- strict warnings (`-Wall -Wextra -Wpedantic -Werror` and additional flags)
- keep warnings clean across supported compilers
- prefer deterministic behavior in benchmark/test harnesses

## Documentation Linting

Repo-level docs lint config:
- `.markdownlint-cli2.jsonc`
- `.vale.ini`
- `.github/styles/BHA/*.yml`
- `.lychee.toml`

Optional local checks:

```bash
npx -y markdownlint-cli2 "README.md" "docs/**/*.md"
vale README.md docs
lychee --config .lychee.toml README.md docs/**/*.md
```

CI workflow:
- `.github/workflows/docs.yml`

## Benchmark and Harness Scripts

Relevant scripts in `tests/`:
- `run_repo_apply_benchmark.py`
- `run_template_subproject.py`
- `run_forward_decl_subproject.py`
- `run_header_split_subproject.py`
- `run_move_to_cpp_subproject.py`
- `run_pimpl_subproject.py`

These scripts are used to validate:
- suggestion generation quality
- auto-apply behavior
- rebuild validation and rollback behavior
- real-repo benchmark deltas

Real-repo methodology and recorded run references:
- `repo_validation.md`

## Release Readiness Checklist

1. All unit tests pass.
2. Representative integration harnesses pass.
3. No major regressions in snapshot compare and benchmark reports.
4. CLI help output and docs are synchronized.
5. Export formats (including SARIF/annotations) validated in CI.
