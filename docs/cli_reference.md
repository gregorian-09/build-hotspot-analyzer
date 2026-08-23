# CLI Reference

Executable: `bha`

## Global Usage

```bash
bha <command> [options]
```

Global flags:
- `-h`, `--help`
- `-v`, `--verbose`
- `-q`, `--quiet`
- `--json`
- `--no-color`

## Commands

- `analyze`
- `suggest`
- `apply`
- `build`
- `record`
- `export`
- `snapshot`
- `compare`
- `version`
- `help`

---

## `analyze`

Analyze build trace files to identify hotspots.

```bash
bha analyze [OPTIONS] [trace-files...]
```

Important options:
- `-o`, `--output`
- `-f`, `--format` (`text`, `json`)
- `-t`, `--top`
- `--list-files`
- `--list-headers`
- `--list-templates`
- `--include-templates`
- `--include-includes`
- `--min-time`
- `-j`, `--parallel`
- `--cmake-index FILE`

Examples:

```bash
bha analyze
bha analyze traces --top 25
bha analyze traces --format json -o analysis.json
```

For Clang `-ftime-trace` input, BHA recognizes only exact producer event names
for template instantiation timing. Prefixes and unrelated events are not
classified as templates; in particular, code-generation events are not template
evidence. Unknown time-trace event names remain unclassified.

When a CMake Instrumentation API v1 index is attached, the analysis includes
`build_session.step_metrics`. Each row is grouped by the producer's explicit
command role (`configure`, `generate`, `build`, `compile`, `link`, `custom`,
`test`, or `install`) and reports command count, timed command count, exact
timed duration sum, and observed exit-result counts. Missing producer fields
remain unavailable; BHA does not infer roles from command text or filenames.
If the query requested CMake `dynamicSystemInformation`, the same section
also reports the observed sample count, peak host memory used in KiB, and peak
CPU load immediately before and after commands. These are derived maxima over
the supplied producer samples; absent or null values are reported as
unavailable.
If the CMake Instrumentation API v1 index contains `staticSystemInformation`,
BHA also reports the producer-provided OS identity, platform, release/version,
architecture flag, logical/physical CPU counts, and total physical/virtual
memory in MiB. Processor and vendor strings are retained when present. Hostname
and other identifying fields are intentionally omitted, and absent producer
fields remain unavailable rather than being filled by local probing.
With CMake data version 1.1, compile snippets may also provide an exact
`traceFile` reference to a copied Clang `-ftime-trace` JSON artifact. The build
session reports the number of such producer references; BHA does not scan the
build tree or match trace files by filename.
The explicit `--cmake-index FILE` attachment is also supported by `suggest`,
`export`, and `build --analyze`; it can be used without positional trace files
when the index contains producer-referenced compile traces.
When CMake data version 1.1 capture-output fields are present, supported command
roles also report exact captured-output observations and producer-byte totals for
stdout and stderr. Raw output text is not copied into aggregate reports or
snapshots; absent streams remain unavailable and no output text is classified.

---

## `suggest`

Generate optimization suggestions.

```bash
bha suggest [OPTIONS] <trace-files...>
```

Important options:
- `-o`, `--output`
- `-f`, `--format` (`text`, `json`)
- `-n`, `--limit`
- `-p`, `--min-priority`
- `-c`, `--min-confidence`
- `--type` (repeatable)
- `--suggester` (repeatable)
- `--list-suggesters`
- `--describe-suggester <id>`
- `--explain`
- `--include-unsafe`
- `--disable-consolidation`
- timeout controls:
  - `--max-suggest-time`
  - `--max-suggester-time`
  - `--max-analyze-time`
  - `--max-analyzer-time`

Examples:

```bash
bha suggest traces --detailed
bha suggest traces --type pch --type template-instantiation
bha suggest --list-suggesters
bha suggest --describe-suggester include
```

---

## `apply`

Apply text edits from a JSON bundle without re-running suggesters.

```bash
bha apply --edits-file <FILE> [OPTIONS]
```

Important options:
- `-e`, `--edits-file` (required)
- `--project-root`
- `--validate-build`
- `--build-cmd`
- `--no-rollback`
- `--no-backup`
- `--backup-dir`

Accepted payload forms:
- top-level `edits`
- top-level `text_edits` or `textEdits`
- nested `suggestions[].edits` / `suggestions[].text_edits` / `suggestions[].textEdits`

Examples:

```bash
bha apply --edits-file edits.json
bha apply --edits-file details.json --validate-build --build-cmd "cmake --build build -j"
```

---

## `build`

Build project with trace capture.

```bash
bha build [OPTIONS]
```

Important options:
- `-s`, `--build-system`
- `-c`, `--config`
- `-j`, `--jobs`
- `-m`, `--memory`
- `-a`, `--analyze`
- `--clean`
- `-b`, `--build-dir`
- `-o`, `--output`
- `--compiler`
- `--cmake-args`
- `--configure-args`

Examples:

```bash
bha build
bha build --build-system cmake --clean --output traces
bha build --memory --analyze
```

---

## `record`

Capture compiler timing output that is not emitted as standalone JSON traces.

```bash
bha record [OPTIONS] -- <build-command...>
```

Important options:
- `-o`, `--output` (required)
- `-c`, `--compiler` (`gcc`, `msvc`, `auto`)
- `-a`, `--append`
- `-t`, `--timestamp`
- `--analyze`

Examples:

```bash
bha record -o traces/ -- make -j4 CXXFLAGS='-ftime-report'
bha record --compiler msvc -o build.log -- cl /Bt+ /c file.cpp
```

---

## `export`

Export analysis to machine-readable or human-readable formats.

```bash
bha export [OPTIONS] <trace-files...> -o <output-file>
```

Formats:
- `json`
- `html`
- `csv`
- `md`

Important options:
- `-o`, `--output` (required)
- `-f`, `--format`
- `-s`, `--include-suggestions` (for `csv`/`md`)
- `--dark-mode`
- `--title`
- `--max-files`
- `--max-suggestions`
- content toggles:
  - `--no-file-details`
  - `--no-dependencies`
  - `--no-templates`
  - `--no-symbols`
  - `--no-timing`

Notes:
- HTML/JSON exports are analysis-focused.
- Suggestion payload inclusion is intentionally constrained by format and flags.

Examples:

```bash
bha export traces --format html --dark-mode -o report.html
bha export traces --format json -o report.json
```

---

## `snapshot`

Manage stored snapshots for historical comparisons.

```bash
bha snapshot <subcommand> [OPTIONS]
```

Subcommands:
- `save <name> <trace-file>`
- `list`
- `show <name>`
- `delete <name>`
- `baseline set <name>`
- `baseline show`
- `baseline clear`

Important options:
- `-d`, `--description`
- `--tag`
- `--storage` (default `.bha/snapshots`)

Examples:

```bash
bha snapshot save baseline build/trace.json -d "main branch baseline"
bha snapshot baseline set baseline
bha snapshot list
```

---

## `compare`

Compare snapshots and enforce regression gates.

```bash
bha compare <old-snapshot> <new-snapshot> [OPTIONS]
bha compare --repeat <snapshot> <snapshot> [...] [OPTIONS]
bha compare --baseline <new-snapshot> [OPTIONS]
```

Use `--repeat` to summarize total build-time observations from two or more
explicitly named snapshots. The output reports the observed minimum, rounded
mean, nearest-rank median/P90/P99, maximum, and sample standard deviation when
there is more than one observation. It is descriptive only: it does not infer
confidence, significance, or causality, and duplicate snapshot names are
rejected.

Important options:
- `-b`, `--baseline`
- `-t`, `--top`
- `--threshold`
- `--gate-tu`
- `--gate-header`
- `--gate-template`
- `--storage`

Exit code behavior:
- `0` if regression gates pass
- `1` if overall significant regression or any active category gate fails

Examples:

```bash
bha compare v1 v2 --threshold 5
bha compare --repeat clean-1 clean-2 clean-3 --json
bha compare --baseline current --gate-tu 5 --gate-header 8 --gate-template 10
```

The comparison also reports `translation_unit_regressions`, an empirical
distribution computed from every path present in both snapshots. It contains
the matched-file count, the count of positive compile-time deltas, their total,
and nearest-rank minimum, median, P90, P99, and maximum values. These values
are reported before `--threshold` filters the detailed regression list; they do
not change regression or gate decisions. JSON output exposes the durations as
`*_delta_ms` fields.
