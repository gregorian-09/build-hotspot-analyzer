# Build Hotspot Analyzer

<p align="center">
  <img src="docs/assets/logo.png" alt="Build Hotspot Analyzer banner" width="960">
</p>

Build Hotspot Analyzer (`bha`) is a C++20 toolchain for measuring, explaining, and improving C and C++ build performance.

Current version: `v0.1.0`

It combines:
- multi-compiler trace ingestion
- structured build-time analysis
- actionable optimization suggestions
- optional safe auto-apply and rollback workflows
- export pipelines for HTML, JSON, SARIF, CSV, Markdown, and PR annotations
- LSP-powered IDE integrations for editor-driven workflows

## Why BHA Exists

Modern C++ builds fail slowly for many different reasons:
- expensive translation units
- pathological include graphs
- repeated template instantiations
- ineffective or missing precompiled headers
- poor unity build partitioning
- bloated headers and unstable implementation boundaries
- weak cache and distributed-build suitability

BHA is built to close that gap with a single workflow:
1. record or ingest traces from real builds
2. analyze where time and memory go
3. explain why those hotspots exist
4. generate targeted suggestions
5. optionally apply safe edits
6. rebuild, compare, and export results for humans and CI

## Core Capabilities

### Trace ingestion
- Clang time-trace JSON
- GCC time-trace JSON
- MSVC traces
- Intel Classic traces
- Intel oneAPI traces
- NVCC traces

### Analysis
- translation unit hotspots
- include/dependency cost
- template-instantiation cost
- symbol and function cost
- circular dependency detection
- stack and memory summaries when present
- cache hit/miss opportunity analysis
- distributed-build suitability
- regression comparison against saved baselines

### Suggestions
- `pch`
- `forward-decl`
- `include-removal`
- `move-to-cpp`
- `template-instantiation`
- `unity-build`
- `header-split`
- `pimpl`

### Build-system coverage
- `cmake`
- `ninja`
- `make` and autotools flows
- `msbuild`
- `meson`
- `bazel`
- `buck2`
- `scons`
- `xcode`
- `unreal`

### Outputs
- `html`
- `json`
- `csv`
- `sarif`
- `md`
- GitHub and GitLab annotation formats

## Project Layout

- `headers/`: public and internal C++ headers
- `sources/`: core implementation
- `cli/`: command implementation for `bha`
- `lsp/`: optional language server and editor integrations
- `resources/`: HTML/CSS/JS templates and assets
- `tests/`: unit tests, integration harnesses, fixtures, subprojects
- `docs/`: user, architecture, CI, exporter, IDE, and contributor documentation

## Build

### Core build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Enable optional modules

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBHA_ENABLE_LSP=ON \
  -DBHA_BUILD_REFACTOR_TOOLS=ON
cmake --build build -j
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

## Quick Start

### Analyze a build and emit suggestions

```bash
./build/bha build --clean --output traces --analyze
./build/bha suggest traces --detailed -o suggestions.json --format json
./build/bha export traces --format html --dark-mode -o report.html
```

### Save and compare snapshots

```bash
./build/bha snapshot save baseline build/trace.json
./build/bha snapshot baseline set baseline
./build/bha snapshot save optimized build/trace_after.json
./build/bha compare --baseline optimized --gate-tu 5 --gate-header 8 --gate-template 10
```

### Discover available commands

```bash
./build/bha --help
./build/bha <command> --help
```

## CLI Command Surface

- `analyze`
- `suggest`
- `apply`
- `build`
- `record`
- `export`
- `snapshot`
- `compare`

## IDE Integrations

BHA ships editor clients around the `bha-lsp` server.

Supported in-tree clients:
- VS Code: `lsp/ide-integrations/vscode`
- Neovim: `lsp/ide-integrations/neovim`
- Emacs: `lsp/ide-integrations/emacs`

### Build the server

```bash
cmake -S . -B build -DBHA_ENABLE_LSP=ON
cmake --build build -j
```

### VS Code

Package and install locally:

```bash
cd lsp/ide-integrations/vscode
npm ci
npm run package
code --install-extension build-hotspot-analyzer-0.1.0.vsix
```

Runtime settings:
- `buildHotspotAnalyzer.serverPath`
- `buildHotspotAnalyzer.autoAnalyze`
- `buildHotspotAnalyzer.trace.server`

Main commands:
- `BHA: Record Build Traces`
- `BHA: Record Build Traces (Advanced)`
- `BHA: Analyze Build Performance`
- `BHA: Show Suggestions`
- `BHA: Show Activity Log`
- `BHA: Apply Suggestion`
- `BHA: Apply All Suggestions`
- `BHA: Revert Changes`
- `BHA: Restart Language Server`

Long-running VS Code commands show progress notifications so build recording, analysis, apply, and revert operations are visible while they run.
Record, analyze, and apply operations expose a cancel button in the progress notification. Cancellation requests are forwarded to
the language server, and build output streams into the activity log during trace recording and rebuild validation.

VS Code also writes command activity, build output summaries, and server/runtime diagnostics to the
`Build Hotspot Analyzer` output channel. Open it with `BHA: Show Activity Log` or through the Output panel.

When VS Code records traces into a custom directory, follow-up analysis reuses that directory for the current workspace.

Recommended workflow:
1. `BHA: Record Build Traces`
2. `BHA: Analyze Build Performance`
3. `BHA: Show Suggestions`

Advanced trace recording supports explicit overrides for compiler, build type, parallel jobs, extra build arguments,
trace output directory, and an optional build-system override when auto-detection is not what you want.

### Neovim

Client module:
- `lsp/ide-integrations/neovim/lua/bha/init.lua`

Requirements:
- `nvim-lspconfig`
- `bha-lsp` on `PATH` or configured explicitly

### Emacs

Client module:
- `lsp/ide-integrations/emacs/bha-lsp.el`

Requirements:
- `lsp-mode`
- `bha-lsp` on `PATH` or configured explicitly

### Distribution strategy

BHA does not require Marketplace publication to be usable.

Recommended order:
1. GitHub-first distribution
2. local or release-attached `.vsix` install for VS Code
3. direct GitHub install for Neovim and Emacs
4. Marketplace / Open VSX publication later, when needed

This keeps the product usable even when Microsoft Marketplace account setup is blocked by Azure billing or organization requirements.

For the full IDE guide, see `docs/ide_integrations.md`.

## Brand Assets

Primary assets:
- banner: `docs/assets/logo.png`
- square icon: `docs/assets/logo-icon.png`
- monochrome banner: `docs/assets/logo-mono.png`

Source assets:
- banner SVG: `docs/assets/logo.svg`
- square icon SVG: `docs/assets/logo-icon.svg`
- monochrome SVG: `docs/assets/logo-mono.svg`

The VS Code extension icon is synchronized from:
- `lsp/ide-integrations/vscode/media/icon.png`

## Documentation Map

- `docs/readme.md`: documentation index
- `docs/installation.md`: installation and build profiles
- `docs/quickstart.md`: end-to-end workflows
- `docs/cli_reference.md`: CLI reference
- `docs/lsp_reference.md`: LSP commands, settings, and apply flow
- `docs/ide_integrations.md`: VS Code, Neovim, Emacs, local setup, and user-facing distribution
- `docs/suggestions_reference.md`: suggester behavior, guardrails, and constraints
- `docs/export_ci.md`: exporters, SARIF, PR annotations, and CI usage
- `docs/architecture.md`: internal architecture and component layout
- `docs/development.md`: contributor workflow and testing
- `docs/coverage_matrix.md`: documentation coverage status
- `docs/troubleshooting.md`: common failures and fixes
- `docs/repo_validation.md`: large-repo validation and benchmark notes
- `docs/docs_strategy.md`: documentation strategy used for this repository

## Repository

- Repository: `https://github.com/gregorian-09/build-hotspot-analyzer`
- Issues: `https://github.com/gregorian-09/build-hotspot-analyzer/issues`

Maintainer: Gregorian Rayne
