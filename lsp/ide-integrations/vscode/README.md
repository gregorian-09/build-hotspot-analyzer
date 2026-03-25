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

- `BHA: Analyze Build Performance`
- `BHA: Show Suggestions`
- `BHA: Apply Suggestion`
- `BHA: Apply All Suggestions`
- `BHA: Revert Changes`
- `BHA: Restart Language Server`

## Project

- Repository: <https://github.com/gregorian-09/build-hotspot-analyzer>
- Issue tracker: <https://github.com/gregorian-09/build-hotspot-analyzer/issues>

Maintainer: Gregorian Rayne
