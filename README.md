# Build Hotspot Analyzer (BHA)

Build Hotspot Analyzer is a C++20 toolchain for measuring, explaining, and improving C/C++ build performance.

It combines:
- multi-compiler trace ingestion
- structured performance analysis
- actionable optimization suggestions
- optional safe auto-apply and rollback workflows (CLI + LSP)
- CI-oriented outputs (SARIF, GitHub/GitLab annotations, regression gates)

Current version: `v0.1.0`

## What BHA Covers

BHA is designed for production build optimization loops:
1. Capture traces from real builds.
2. Identify expensive translation units, headers, templates, and symbols.
3. Generate optimization suggestions with estimated savings.
4. Optionally apply edits and validate via rebuild.
5. Export outputs for developers, CI, and code-scanning platforms.
6. Track regressions over time using snapshots and gates.

## Core Features

- Parsers: Clang, GCC, MSVC, Intel, NVCC traces.
- Analyzers: file, dependency, template, symbol, PCH, performance, cache/distribution suitability.
- Suggesters:
  - `pch`
  - `forward-decl`
  - `include-removal`
  - `move-to-cpp`
  - `template-instantiation`
  - `unity-build`
  - `header-split`
  - `pimpl`
- Build adapters:
  - `cmake`, `ninja`, `make` (including autotools flows), `msbuild`, `meson`, `bazel`, `buck2`, `scons`, `xcode`, `unreal`
- Exporters: `json`, `html`, `csv`, `sarif`, `md`, plus PR-native annotations.
- Comparison gates:
  - global threshold
  - category gates for TU/header/template regressions
- Optional LSP server + IDE clients (`vscode`, `neovim`, `emacs`).

## Project Layout

- `headers/` public and internal C++ headers
- `sources/` core implementations
- `cli/` command implementations for `bha`
- `lsp/` optional language-server module (`bha-lsp`)
- `tools/refactor/` optional semantic refactor helper (`bha-refactor`)
- `resources/` HTML/CSS/JS templates and embedded assets
- `tests/` unit tests, integration harnesses, subprojects, benchmark runners

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Enable optional modules:

```bash
cmake -S . -B build \
  -DBHA_ENABLE_LSP=ON \
  -DBHA_BUILD_REFACTOR_TOOLS=ON
cmake --build build -j
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

## Quick Start

1. Build with traces:

```bash
./build/bha build --clean --output traces --analyze
```

2. Generate suggestions:

```bash
./build/bha suggest traces --detailed -o suggestions.json --format json
```

3. Export report:

```bash
./build/bha export traces --format html --dark-mode -o report.html
```

4. Save and compare snapshots:

```bash
./build/bha snapshot save baseline build/trace.json
./build/bha snapshot baseline set baseline
./build/bha snapshot save after_opt build/trace_after.json
./build/bha compare --baseline after_opt --gate-tu 5 --gate-header 8 --gate-template 10
```

## CLI Commands

- `analyze`
- `suggest`
- `apply`
- `build`
- `record`
- `export`
- `snapshot`
- `compare`

Run:

```bash
./build/bha --help
./build/bha <command> --help
```

## Optional LSP Workflow

When built with `-DBHA_ENABLE_LSP=ON`, BHA exposes:
- `bha.analyze`
- `bha.applySuggestion`
- `bha.applyEdits`
- `bha.applyAllSuggestions`
- `bha.getSuggestionDetails`
- `bha.revertChanges`
- `bha.listSuggesters`
- `bha.runSuggester`
- `bha.explainSuggestion`

LSP backups default to `.lsp-optimization-backup/` and can include rollback + trust-loop metrics.

## Documentation Map

- `docs/readme.md` docs index
- `docs/installation.md` installation and build profiles
- `docs/quickstart.md` end-to-end workflows
- `docs/cli_reference.md` complete CLI reference
- `docs/lsp_reference.md` LSP command and config reference
- `docs/ide_integrations.md` VS Code/Neovim/Emacs usage and distribution
- `docs/suggestions_reference.md` suggester behavior and guardrails
- `docs/export_ci.md` exporters, annotations, SARIF, CI gates
- `docs/architecture.md` internal architecture and extension points
- `docs/development.md` contributor workflow and testing
- `docs/coverage_matrix.md` feature-to-doc coverage status
- `docs/troubleshooting.md` common failures and fixes
- `docs/repo_validation.md` large-repo validation methodology and benchmark artifacts
- `docs/docs_strategy.md` production documentation strategy used for this repo
