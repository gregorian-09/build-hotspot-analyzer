# Build Hotspot Analyzer for VS Code

Build Hotspot Analyzer integrates `bha-lsp` into VS Code to analyze C and C++ build performance, surface optimization suggestions, and apply safe edits where supported.

## Features

- Analyze build performance from VS Code
- Navigate the current analysis from the persistent BHA activity-bar view
- Show optimization suggestions produced by `bha-lsp`
- Review measured, derived, or unavailable impact evidence before applying changes
- Preview concrete suggestion edits in a native VS Code diff view without writing files
- Apply one suggestion or all applicable suggestions
- Revert applied changes through the language server workflow
- Inspect LSP traffic when debugging extension behavior

Evidence-backed suggestions are applied against the saved workspace files.
The extension refuses to apply a suggestion when an affected file has unsaved
editor changes or when the suggestion's affected-file metadata is incomplete;
save or discard the changes and run analysis again before applying.
After a successful apply, clean open documents are refreshed from the
validated disk contents. A document that becomes dirty during the operation is
left untouched and reported for manual reconciliation.

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
Record, analyze, and apply operations expose a cancel button in the progress notification. Cancellation requests are forwarded to
the language server, and build output streams into the activity log during trace recording and rebuild validation.

The extension also writes command activity, build summaries, and server/runtime diagnostics to the `Build Hotspot Analyzer`
output channel. Use `BHA: Show Activity Log` or open the Output panel and select `Build Hotspot Analyzer`.

If trace recording uses a custom trace output directory, the VS Code client reuses that directory for follow-up analysis in the same workspace.
The last successful traced build profile is persisted per workspace and reused for apply-time rebuild validation across reloads when the
cached build directory, trace directory, and explicit compiler path are still valid.

Recommended workflow:
1. Open the `BHA` activity-bar view.
2. Record traces.
3. Analyze traces.
4. Select a suggestion to review its evidence, affected files, and optional native diff.
5. Apply only after reviewing the proposed change.

Inline code actions are intentionally limited to exact, automatically applicable
forward-declaration, include-reduction, and header-split edits. Project-wide or
advisory suggestions remain in the BHA view so their broader scope is visible.

The advanced trace recording command exposes optional overrides for compiler, build type, parallel jobs, extra build
arguments, trace output directory, and build-system selection when auto-detection is not appropriate.

## Project

- Repository: <https://github.com/gregorian-09/build-hotspot-analyzer>
- Issue tracker: <https://github.com/gregorian-09/build-hotspot-analyzer/issues>

Maintainer: Gregorian Rayne
