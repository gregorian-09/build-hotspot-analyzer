# Build Hotspot Analyzer for VS Code

Build Hotspot Analyzer integrates `bha-lsp` into VS Code to analyze C and C++ build performance, surface optimization suggestions, and apply safe edits where supported.

## Features

- Analyze build performance from VS Code
- Show optimization suggestions produced by `bha-lsp`
- Apply one suggestion or all applicable suggestions
- Revert applied changes through the language server workflow
- Inspect LSP traffic when debugging extension behavior

## Requirements

- A working `bha-lsp` binary on your `PATH`, or configure `buildHotspotAnalyzer.serverPath`
- A C or C++ workspace

## Settings

- `buildHotspotAnalyzer.serverPath`: path to the `bha-lsp` executable
- `buildHotspotAnalyzer.autoAnalyze`: run analysis automatically on startup
- `buildHotspotAnalyzer.trace.server`: trace LSP communication (`off`, `messages`, `verbose`)

## Commands

- `BHA: Record Build Traces`
- `BHA: Record Build Traces (Advanced)`
- `BHA: Analyze Build Performance`
- `BHA: Show Suggestions`
- `BHA: Show Activity Log`
- `BHA: Apply Suggestion`
- `BHA: Apply All Suggestions`
- `BHA: Revert Changes`
- `BHA: Restart Language Server`

Long-running commands surface progress notifications in VS Code so recording, analysis, apply, and revert operations remain visible while they run.

The extension also writes command activity, build summaries, and server/runtime diagnostics to the `Build Hotspot Analyzer`
output channel. Use `BHA: Show Activity Log` or open the Output panel and select `Build Hotspot Analyzer`.

If trace recording uses a custom trace output directory, the VS Code client reuses that directory for follow-up analysis in the same workspace.

Recommended workflow:
1. Record traces
2. Analyze traces
3. Review and apply suggestions

The advanced trace recording command exposes optional overrides for compiler, build type, parallel jobs, extra build
arguments, trace output directory, and build-system selection when auto-detection is not appropriate.

## Project

- Repository: <https://github.com/gregorian-09/build-hotspot-analyzer>
- Issue tracker: <https://github.com/gregorian-09/build-hotspot-analyzer/issues>

Maintainer: Gregorian Rayne
