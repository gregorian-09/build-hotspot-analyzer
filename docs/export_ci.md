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

CSV is a normalized bundle rather than a mixed multi-section file:

```bash
bha export traces --format csv --include-suggestions -o report-csv/
```

The bundle contains rectangular tables such as `summary.csv`, `files.csv`,
`headers.csv`, `dependency_edges.csv`, `templates.csv`, `suggestions.csv`,
`suggestion_edits.csv`, `build_steps.csv`, `targets.csv`, `modules.csv`, and
`symbols.csv`. A `.csv` output path produces only the rectangular file table;
use a directory when suggestions or relationship tables are needed.

## Format Guidance

- `html`: for interactive human analysis.
- `json`: for automation and downstream analytics ingestion.
- `csv`: for spreadsheet and ad-hoc trend slicing through normalized tables.
- `md`: for lightweight PR reports.

## Suggestion Payload Behavior

Suggestion payload inclusion is opt-in and consistent across JSON, HTML, CSV,
and Markdown:
- use `--include-suggestions` when the explainable optimization payload is needed
- CSV suggestions are written to `suggestions.csv` and related normalized tables
- suggestion generation may be expensive because it requires semantic project analysis

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
