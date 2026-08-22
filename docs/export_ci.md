# Export and CI Integration

## Export Formats

BHA supports:
- `json`
- `html`
- `csv`
- `md`

Basic usage:

```bash
bha export traces --format <format> -o output.<ext>
```

## Format Guidance

- `html`: for interactive human analysis.
- `json`: for automation and downstream analytics ingestion.
- `csv`: for spreadsheet and ad-hoc trend slicing.
- `md`: for lightweight PR reports.

## Suggestion Payload Behavior

Suggestion payload inclusion is intentionally controlled by format and flags:
- use `--include-suggestions` for `csv` / `md`
- HTML/JSON exports are analysis-first to reduce report noise

## Snapshot-Based Regression Gates

Save baseline:

```bash
bha snapshot save baseline build/trace.json
bha snapshot baseline set baseline
```

Compare and gate:

```bash
bha compare --baseline current --threshold 5 --gate-tu 5 --gate-header 8 --gate-template 10
```

Gate semantics:
- non-zero exit when:
  - overall significant regression exceeds threshold
  - any active category gate fails

## Recommended CI Flow

1. Build with trace capture.
2. Run `bha analyze` and `bha export` (json/html/csv/md).
3. Save snapshot for current commit.
4. Compare against baseline snapshot with gates.
5. Optionally run LSP/runner auto-apply in dedicated optimization jobs.

## Trust Loop in CI/IDE

Apply workflows can persist predicted-vs-actual records to:
- `.lsp-optimization-backup/trust_loop.jsonl` (or configured backup dir)

Use these records to monitor suggestion estimation fidelity over time.

## Cache/Distribution Insights

Analysis output includes cache/distribution indicators:
- cache hit opportunity %
- cache risk compilations
- distributed suitability score
- sccache/FASTBuild detection flags

Use these to decide:
- whether to prioritize cache hygiene work
- whether distributed build infrastructure is likely to pay off
