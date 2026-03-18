# Quick Start

This guide runs a full baseline workflow:
1. capture traces
2. analyze hotspots
3. generate suggestions
4. export report
5. snapshot and compare

If `bha` is not installed on your PATH, use `./build/bha`.

## 1. Build With Trace Capture

From your project root:

```bash
bha build --clean --output traces --analyze
```

Tips:
- use `--build-system` when auto-detection is ambiguous
- use `--compiler` when toolchain choice matters
- use `--memory` to capture stack usage artifacts

## 2. Analyze Existing Trace Files

```bash
bha analyze traces --top 20 --include-includes --include-templates
```

Machine-readable output:

```bash
bha analyze traces --format json -o analysis.json
```

## 3. Generate Optimization Suggestions

```bash
bha suggest traces --detailed --limit 50 -o suggestions.json --format json
```

Target specific suggestion families:

```bash
bha suggest traces --type pch --type template-instantiation --type unity-build
```

List available suggesters:

```bash
bha suggest --list-suggesters
```

## 4. Export Reports

Interactive HTML:

```bash
bha export traces --format html --dark-mode -o report.html
```

CI-friendly SARIF:

```bash
bha export traces --format sarif -o bha.sarif
```

Markdown summary:

```bash
bha export traces --format md --include-suggestions -o report.md
```

## 5. Save Baseline and Compare

`snapshot save` currently accepts a single trace file path.

```bash
bha snapshot save baseline build/trace.json
bha snapshot baseline set baseline
```

After optimization work, re-run build/trace and save:

```bash
bha snapshot save after_opt build/trace_after.json
bha compare --baseline after_opt --gate-tu 5 --gate-header 8 --gate-template 10
```

Exit code:
- `0` when gates pass
- `1` when overall/category thresholds fail

## 6. Apply Edit Bundles Directly (No Suggestion Trigger Required)

If you already have `text_edits` in JSON:

```bash
bha apply --edits-file edits.json --validate-build --build-cmd "cmake --build build -j"
```

This path lets teams run controlled edit application independently from live suggester invocation.

## 7. Optional LSP Workflow

Run `bha-lsp`, then from client:
- execute `bha.analyze`
- inspect `bha.getSuggestionDetails`
- apply via `bha.applySuggestion` or `bha.applyAllSuggestions`
- rollback via `bha.revertChanges`

See `lsp_reference.md` for payload contracts.
