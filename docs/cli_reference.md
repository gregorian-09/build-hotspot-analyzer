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

Examples:

```bash
bha analyze
bha analyze traces --top 25
bha analyze traces --format json -o analysis.json
```

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
- heuristic controls:
  - `--pch-*`
  - `--template-*`
  - `--unity-*`
  - `--header-*`
  - `--fwd-decl-min-time`
  - `--codegen-threshold`
  - `--unreal-*`
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
- `sarif`
- `md`

Important options:
- `-o`, `--output` (required)
- `-f`, `--format`
- `-s`, `--include-suggestions` (for `csv`/`md`/`sarif`)
- `--pr-annotations` (`github`, `gitlab`)
- `--annotations-output`
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
bha export traces --format sarif -o bha.sarif
bha export traces --format json --pr-annotations github --annotations-output gha.txt -o report.json
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
bha compare --baseline <new-snapshot> [OPTIONS]
```

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
bha compare --baseline current --gate-tu 5 --gate-header 8 --gate-template 10
```
