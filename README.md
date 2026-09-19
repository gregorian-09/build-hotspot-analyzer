# Build Hotspot Analyzer

<p align="center">
  <img src="assets/logo.png" alt="Build Hotspot Analyzer" width="960">
</p>

Build Hotspot Analyzer (BHA) is a C++20 toolchain for investigating C and C++ build cost. It ingests compiler timing artifacts and explicit build-system evidence, normalizes them into one analysis model, and produces reports and guarded optimization suggestions. The `bha` CLI is the primary interface; the optional `bha-lsp` server and VS Code extension expose the same project-level apply engine.

**Version:** 0.1.0. This is a pre-release project. A trace identifies observed cost, not the cause of every build slowdown; a suggestion is not a promise of savings or a substitute for a full build and test suite.

## Contents

- [What BHA measures](#what-bha-measures)
- [Support boundaries](#support-boundaries)
- [Build BHA](#build-bha)
- [First analysis](#first-analysis)
- [Capture and attach evidence](#capture-and-attach-evidence)
- [Interpret the results](#interpret-the-results)
- [CLI reference](#cli-reference)
- [Optimization suggestions](#optimization-suggestions)
- [Apply and rollback](#apply-and-rollback)
- [Reports and data contract](#reports-and-data-contract)
- [Snapshots and regression gates](#snapshots-and-regression-gates)
- [LSP and IDE workflow](#lsp-and-ide-workflow)
- [Architecture and extension points](#architecture-and-extension-points)
- [Data and trust boundaries](#data-and-trust-boundaries)
- [Validation and development](#validation-and-development)
- [Troubleshooting](#troubleshooting)
- [Known limits](#known-limits)

## What BHA measures

BHA keeps several timing domains separate:

| Question | Evidence source | Important distinction |
| --- | --- | --- |
| Which translation units are expensive? | Per-TU compiler trace or timing report | The sum of TU durations is *serial compiler work*, not elapsed build time. |
| Which headers and templates account for frontend cost? | Producer include and per-specialization events | An aggregate template phase cannot identify a safe explicit-instantiation edit. |
| How long did the build and its commands take? | BHA adapter sidecar or CMake Instrumentation API | Command overlap and critical path require producer timestamps and, for critical path, complete dependency edges. |
| Where is cache time going? | Structured sccache counters supplied explicitly | No cache-hit rate is inferred from ordinary compiler traces. |
| Which targets, modules, or link steps matter? | CMake File API, P1689 scan, instrumentation command events | BHA does not invent ownership or dependency edges from filenames. |
| What memory data exists? | `.su` stack-usage files or Clang process-resource CSV | Stack frame estimates, process peak memory, and host memory are different quantities. |

The pipeline is `artifact -> parser -> BuildTrace -> analyzers -> AnalysisResult -> suggesters -> report or guarded apply`. Each metric has a capability record with `observed`, `derived`, or `unavailable` evidence, a producer and capture mode, scope, timing domain, aggregation, and any limitation. A numeric `0` alone is **not** proof of an observed zero: some internal counters default to zero when evidence is absent. The canonical export uses `null` for many unavailable quantities; always inspect the associated capability or savings-evidence field.

## Support boundaries

BHA's own source requires **C++20**. That does **not** force an analyzed project to use C++20: trace parsing is independent of the source standard. Semantic suggestions require a faithful `compile_commands.json` and a Clang LibTooling version able to parse that project's language mode, extensions, and flags. Consequently, C++98 through newer dialects and C dialects are not a blanket auto-edit compatibility guarantee. If the AST or producer evidence cannot be established, the affected suggestion must be skipped.

| Compiler or artifact | Current ingestion | Boundary |
| --- | --- | --- |
| Clang, AppleClang, compatible Clang-family drivers | `-ftime-trace` Chrome Trace Event JSON | Exact event names and valid event data are required; unsupported phases remain unclassified. |
| GCC | `-ftime-report` text captured per compile or with `bha record` | Report phases may be aggregate; GCC does not provide the Clang AST evidence required by some edits. |
| MSVC | `/Bt+` timing text captured with `bha record` or a suitable launcher | An MSBuild Build Insights setting is not itself a BHA-readable `.etl` parser. |
| Intel oneAPI ICX/ICPX | Clang-compatible time-trace JSON | Availability depends on the actual artifact schema; this is not Intel Classic support. |
| Intel Classic ICC/ICPC | No elapsed-time ingestion from `-qopt-report` | Optimization remarks are not compile timing. |
| NVIDIA NVCC | Compile-only CI probe, **no** `--time` ingestion | NVCC's vendor CSV is rejected rather than mapped to guessed phases. |

The default registered build adapters are **CMake** and **MSBuild**. Implementations for Ninja, Make, Meson, Bazel, Buck2, SCons, and Xcode exist behind an explicit experimental registration mode in the library; the shipped CLI registers only the core set. Do not assume `bha build --build-system ninja` works because an adapter source file exists. For another build driver, capture compiler artifacts yourself and run `bha analyze`/`bha export` on them. A compilation database can support semantic analysis independently of the build adapter.

The cross-platform CI matrix builds and tests on Linux (GCC and Clang), Windows (Visual Studio 2022/MSVC), and macOS (AppleClang), with sanitizer jobs for those platforms. It also compiles a trace fixture and checks analysis/export. This validates BHA on those runners, not arbitrary third-party codebases or every compiler release. The CUDA workflow checks an NVCC compile contract only.

## Build BHA

### Prerequisites and profiles

- CMake **3.28 or later**, a C++20 host compiler, and Python 3 for resource generation.
- `nlohmann_json`: installed CMake package preferred; CMake otherwise fetches v3.11.3.
- GoogleTest for `BHA_BUILD_TESTS=ON`: installed CMake package preferred; CMake otherwise fetches v1.14.0 (MSVC ASan builds use a source build).
- LLVM/Clang LibTooling headers and libraries for AST-backed suggestion generation. CMake reports whether tooling and dependency scanning were found. `clang-tidy` with `misc-include-cleaner` is also needed for include-removal suggestions.
- Node.js/npm only for the VS Code extension. `compile_commands.json` belongs to the **project being analyzed**, not merely the BHA build.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/bha version
```

Use a separate build directory per toolchain. On a memory-constrained WSL host, keep parallelism low; LibTooling translation units are large. The default build includes `bha` and tests but not the LSP server or refactor executable.

For the CLI project workflow, LSP, and structural PIMPL tool:

```bash
cmake -S . -B build-clang -DCMAKE_BUILD_TYPE=Release \
  -DBHA_ENABLE_LSP=ON -DBHA_BUILD_REFACTOR_TOOLS=ON \
  -DBHA_ENABLE_TEMPLATE_AST=ON -DBHA_REQUIRE_CLANG_TOOLING=ON
cmake --build build-clang --parallel 2
ctest --test-dir build-clang --output-on-failure
```

When LLVM is outside CMake's search paths, pass `-DBHA_CLANG_TOOLING_ROOT=<llvm-prefix>` or set `CMAKE_PREFIX_PATH`; do not encode a machine-specific LLVM path in the project. With `BHA_REQUIRE_CLANG_TOOLING=ON`, configuration fails instead of quietly building without that backend. `BHA_ENABLE_TEMPLATE_AST` controls the LibTooling backend used by multiple semantic suggestion paths, not only templates.

On Windows, run CMake from a **Visual Studio 2022 Developer Command Prompt** with MSVC and dependencies visible to CMake:

```bat
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DBHA_ENABLE_LSP=ON -DBHA_REQUIRE_CLANG_TOOLING=ON
cmake --build build-msvc --config Release --parallel 2
ctest --test-dir build-msvc -C Release --output-on-failure
```

An installed LLVM CMake package or an explicitly supplied `BHA_CLANG_TOOLING_ROOT` provides LibTooling. `vcpkg` is an optional dependency provider, not a required hardcoded path. On macOS, use AppleClang for the host build and an installed LLVM/Clang LibTooling package for AST-backed features; configure with a suitable prefix when CMake cannot discover it.

| CMake option | Default | Effect |
| --- | --- | --- |
| `BHA_BUILD_CLI` | `ON` | Build `bha`. |
| `BHA_BUILD_TESTS` | `ON` | Build GoogleTest and integration fixtures. |
| `BHA_ENABLE_LSP` | `OFF` | Build `bha-lsp` and add `bha project` to the CLI. |
| `BHA_BUILD_REFACTOR_TOOLS` | `OFF` | Build `bha-refactor`; PIMPL transformation still requires LibTooling. |
| `BHA_ENABLE_TEMPLATE_AST` | `ON` | Attempt to enable Clang-backed semantic indexing. |
| `BHA_REQUIRE_CLANG_TOOLING` | `OFF` | Fail configure when requested tooling is missing. |
| `BHA_ENABLE_SANITIZERS` | `OFF` | ASan+UBSan on Clang/GCC; supported ASan configuration on MSVC. |
| `BHA_ENABLE_COVERAGE` | `OFF` | Compiler coverage instrumentation outside MSVC. |
| `BHA_BUILD_SHARED` | `OFF` | Retained CMake option; the main `bha_lib` target is currently static. |

Installed headers and the CLI are governed by root CMake install rules. `bha-lsp` and `bha-refactor` are optional build-tree targets, not implicitly installed by `cmake --install`.

## First analysis

The following commands run **from the project being measured**, with `bha` on `PATH`. Use the absolute path to your built BHA executable if it is not installed.

```bash
bha build --build-system cmake --clean --build-dir build-bha \
  --output build-bha/traces --analyze
bha analyze build-bha/traces --top 20 --include-includes --include-templates
bha suggest build-bha/traces --detailed --limit 20
bha export build-bha/traces --format html -o build-bha/report.html
```

`--clean` removes the selected project's build directory before configuration; do not use it against a build tree you need to preserve. A real compile must occur for per-TU traces to exist. `bha build` also writes `bha-build-session.json` beside captured traces. That sidecar records adapter-observed build wall time or, when available, CMake producer events. Subsequent analyze/suggest/export commands automatically load the sidecar next to a supplied trace directory or file.

If you already have Clang traces, BHA does not need to drive the build:

```bash
cmake -S . -B build-traced -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_CXX_FLAGS=-ftime-trace
cmake --build build-traced --parallel 2
bha analyze build-traced
bha export build-traced --format json -o analysis.json
```

The trace directory may include unrelated JSON files; parser routing validates content rather than trusting a filename. Invalid/unsupported files are not converted into guessed data. For semantic suggestions, make the project's compilation database discoverable under the project/build tree, then check `bha suggest --list-suggesters` and diagnostics when a family does not appear.

## Capture and attach evidence

### Compiler traces

| Producer | Capture example | Parsed contribution |
| --- | --- | --- |
| Clang-family C/C++ | `clang++ -ftime-trace -c src.cpp -o src.o` | Per-TU frontend/backend events, exact `Source` include events, and template events when emitted. |
| GCC | `bha record -o gcc.report -- g++ -ftime-report -c src.cpp -o src.o` | GCC report phases; unsupported phase splits stay unavailable. |
| MSVC | `bha record --compiler msvc -o msvc.log -- cl /Bt+ /c src.cpp` | MSVC `/Bt+` timing rows. Run in a configured MSVC developer environment. |
| Intel oneAPI | Produce Clang-compatible time-trace JSON with the selected ICX/ICPX toolchain | Only the compatible JSON contract is accepted. |

`bha record` captures a child process's console output into a file and returns the child's failure status. It is useful for timing reports that do not become standalone JSON; it cannot manufacture per-specialization timing or include events that the compiler did not emit. `bha build --memory` requests compiler memory artifacts where supported. `.su` is stack-usage information, **not** resident memory for the build host.

### Build, target, cache, and resource sidecars

| Input | CLI attachment | What becomes available |
| --- | --- | --- |
| `bha-build-session.json` | Automatic beside a trace path | Adapter-observed wall time; exact command events if persisted. |
| CMake Instrumentation API v1 index | `--cmake-index FILE` | Producer command roles, timestamps, results, optional trace references and system information. |
| CMake File API reply index | `--cmake-file-api FILE --config CONFIG` | Configured target identity, dependencies, output artifacts, PCH declarations, and exact target joins where possible. |
| Structured sccache statistics JSON | `--cache-stats FILE` | Actual requests, hits, misses, errors, and hit rate when supported. |
| P1689 module scan JSON | `--module-deps FILE` | Producer-declared provides/requires and resolvable logical-name edges. |
| Clang `-fproc-stat-report` CSV | `--resource-stats FILE` | Process wall/user time and peak memory from reported rows. |
| `.su` files | Supply a directory containing them | Stack-usage summaries for TUs that can be matched. |

The attachment flags are available on `analyze`, `suggest`, and `export`; `build --analyze` accepts the cache, module, resource, and instrumentation inputs. A CMake index can be analyzed without positional compiler traces if it contains producer-referenced compile traces. BHA does not scan the build tree to invent a relationship between an instrumentation command and a compiler trace.

For a manually instrumented build, pass the producer artifacts explicitly:

```bash
bha export traces/ --format json -o report.json \
  --cmake-index build/.cmake/instrumentation/v1/data/index/index-example.json \
  --cmake-file-api build/.cmake/api/v1/reply/index-example.json \
  --cache-stats sccache-stats.json \
  --module-deps module-deps.json \
  --resource-stats process-stats.csv
```

The two `index-example.json` paths are placeholders: use index files actually emitted by that build. Do not mix artifacts from different configurations or build runs and then interpret their joins as exact.

`bha build` creates File API and instrumentation queries in its selected CMake build tree. When supported by the installed CMake version and producer, the instrumentation query requests `staticSystemInformation`, `dynamicSystemInformation`, `captureOutput`, and `compileTrace`. The resulting index is attached during `build --analyze` and the File API reply is used for target context. An older CMake or a trace-only build may produce none of these: missing host OS, CPU load, command role, target, or critical-path fields indicate missing producer evidence, not necessarily a parser failure. Raw captured stdout/stderr is not copied into aggregate reports or snapshots; only observed byte counts are summarized.

The source tree includes an LLD `--time-trace` parser for library use, but the CLI has no dedicated `--linker-trace` attachment flag. CLI linker metrics currently come from producer link-command events and their output metadata. Do not infer LTO time from generic link wall time.

## Interpret the results

The analyzer registry runs independent analyzers and merges their typed results. Its output domains are:

| Domain | Key data | Evidence caveat |
| --- | --- | --- |
| `performance`, `files` | Compile distributions, slow TUs, frontend/backend breakdown, stack data | Serial compile sum and elapsed build time are separate fields. |
| `dependencies` | Observed included headers, inclusion count, parse time, include graph | No source-text approximation for missing include events. Nested timing may be inclusive; self time requires appropriate event nesting. |
| `templates` | Named specialization counts and durations | Aggregate compiler phase time is not an actionable specialization. |
| `symbols` | Producer-provided symbol definitions/usages | Missing symbol records do not imply no symbols exist. |
| `build_session` | Timed command count, role groups, overlap, host telemetry, critical path | A critical path needs a complete producer dependency graph. |
| `linker` | Link invocations, duration, output bytes, optional trace/LTO fields | Link totals depend on exact link command evidence. |
| `targets` | File API targets, command joins, PCH entries | Ambiguous or unmatched commands remain unmatched. |
| `modules` | P1689 rules, provides/requires, dependency edges | No module timings are invented from the dependency graph. |
| `process_resources` | Process time and peak KiB | Only explicit Clang resource rows are counted. |
| `cache_distribution` | sccache counters and rate | Requires a structured stats file; ordinary traces cannot supply it. |

Use `metric_capabilities` in JSON to decide whether a value is measured, derived, or unavailable. Provenance records scope and timing aggregation, so a value in milliseconds is not automatically comparable to another millisecond field. Build session `serial_time_ms` is a sum; `wall_clock_time_ms` is a producer-observed elapsed span. The presence of one capability does not imply every related metric is present.

Headers may show repeated **inclusive** parse durations; adding those durations to compile time can double-count nested work. Suggestion `estimated_savings` defaults to unavailable when an edit has not been measured post-apply. A trace hotspot establishes *where* to investigate; controlled before/after builds under comparable cache and machine conditions establish whether an edit helped.

## CLI reference

Run `bha --help` or `bha <command> --help` for the exact options in your build. The built-in commands are:

Common flags are `-h`/`--help`, `-v`/`--verbose`, `-q`/`--quiet`, `--json`, and `--no-color`. The CLI also accepts `bha help <command>` and `bha version`.

| Command | Purpose | Key switches |
| --- | --- | --- |
| `analyze [traces...]` | Text or JSON hotspot analysis; defaults to `build/traces/` if present | `--format json`, `--top`, `--list-files`, `--list-headers`, `--list-templates`, `--min-time`, `--parallel`, evidence attachments. |
| `suggest [traces...]` | Generate, filter, and explain optimization candidates | `--list-suggesters`, `--describe-suggester`, repeatable `--type`/`--suggester`, `--min-priority`, `--min-confidence`, `--include-unsafe`, `--explain`, timeout controls. |
| `build` | Drive a supported project build and collect traces | `--build-system`, `--build-dir`, `--output`, `--compiler`, `--config`, `--jobs`, `--clean`, `--memory`, `--analyze`. |
| `record -- CMD...` | Capture GCC/MSVC timing output from a child command | Required `-o`, `--compiler`, `--append`, `--timestamp`, `--analyze`. |
| `export [traces...] -o PATH` | Produce a JSON, HTML, CSV, or Markdown report | `--format`, `--include-suggestions`, detail exclusions, `--max-files`, `--max-suggestions`, evidence attachments. |
| `snapshot` | Save/list/show/delete named snapshots and manage a baseline | `save`, `list`, `show`, `delete`, `baseline set/show/clear`, `--storage`. |
| `compare` | Compare named snapshots or describe repeated runs | `--baseline`, `--repeat`, `--threshold`, category gates, `--top`. |
| `project` | Shared CLI/IDE record, analyze, apply, and revert workflow | Only built when `BHA_ENABLE_LSP=ON`; see [Apply and rollback](#apply-and-rollback). |
| `version`, `help` | Version and command help | `bha help suggest` also shows command help. |

The usual machine-readable commands are:

```bash
bha analyze traces --format json -o analysis.json
bha suggest traces --format json -o suggestions.json --explain
bha export traces --format json -o report.json --include-suggestions
```

`suggest --explain` relaxes output filtering and consolidation to expose diagnostic evidence; it does **not** bypass AST, compile-database, producer-schema, or post-edit validation requirements. Timeout flags (`--max-analyze-time`, `--max-analyzer-time`, `--max-suggest-time`, `--max-suggester-time`) are milliseconds; `0` means no requested limit. A timeout can yield fewer results, not inferred replacements for missing evidence.

The `--json` global flag and `--format json` command options are not interchangeable with the canonical `export` document: use `bha export --format json` for the complete structured analysis contract. The CLI's `suggest -o` writes a suggestion JSON list even when the screen formatter is text.

## Optimization suggestions

Every candidate has a stable type, priority, confidence, source target, rationale, caveats, verification instructions, optional `hotspot_origins`, and an application mode. `advisory` is review-only; `direct-edits` contains concrete file replacements; `external-refactor` requires a separate specialized tool. `is_safe` means the implemented gates passed for that candidate, **not** that every platform or test case has been proven equivalent.

| Suggester | Evidence and operation | Current application behavior |
| --- | --- | --- |
| `pch` | Repeated measured header inclusions across distinct TUs plus matching compiler, language, working directory, and preprocessing command environment | **Advisory only.** BHA does not emit a compiler/build-system PCH edit or pre-claim savings. Configure PCH natively, then rebuild and remeasure. |
| `forward-decl` | Compile-database-backed Clang AST bindings prove selected uses are incomplete-type-safe; namespace and declaration shape are preserved | Direct edit replaces eligible include edges with declarations; macros, aliases, templates, dependent/complete-type uses, and unsupported scopes fail closed. |
| `include-removal` | `clang-tidy` `misc-include-cleaner` exported fixes plus compile-command-backed replacement validation | Direct edit only for a validated unused include. Missing `clang-tidy`, compilation database, fixes, or validation means no edit. |
| `template-instantiation` | Per-specialization timing **with source locations**, Clang AST identity, complete definition, and one existing explicit-instantiation owner | Direct edit inserts the canonical `extern template`; it does not synthesize an owner or accept aggregate GCC-style template time. |
| `unity-build` | Current CMake File API target/source model, complete compatible compile commands, and Clang syntax validation of proposed merged TU | Target-scoped CMake `UNITY_BUILD` edit. CMake regeneration, full build, and tests are still necessary for ODR/order-sensitive code. |
| `header-split` | Clang AST proves a forward-declaration split for selected includers; replacement passes post-edit syntax validation | Direct edit creates a companion `_fwd` header and rewrites only eligible include edges. This is not arbitrary architectural header partitioning. |
| `pimpl` | AST-identified project-owned class with private data and strict structural eligibility | **Advisory only** in suggestion generation. Optional `bha-refactor pimpl` is a separate transformation path; review ABI/design changes manually. |

The optional structural tool accepts an explicit PIMPL request rather than consuming an arbitrary suggestion ID:

```bash
bha-refactor pimpl --compile-commands build/compile_commands.json \
  --source src/widget.cpp --header include/widget.hpp --class Widget
```

It returns diagnostics and replacement data; review its output and the class's ABI/copy/move/lifetime behavior before deciding whether to apply it. Building `bha-refactor` without LibTooling does not make the transformation available.

Run `bha suggest --list-suggesters` or `bha suggest --describe-suggester <id>` for registration metadata. That metadata describes *potential* capability, not a promise that every returned candidate is auto-applicable; the actual suggestion's `application_mode`, `edits`, `is_safe`, and `auto_apply_blocked_reason` are authoritative. In particular, PCH currently remains advisory and header-split can emit direct edits despite coarse catalog hints.

No current suggester assigns a fixed percentage or header-size multiplier as proven savings. Candidate cost and realized build saving are different quantities. Current suggesters initialize their numeric saving to zero and leave `estimated_savings_evidence` unavailable until a comparable post-edit trace exists. Canonical JSON exports render these unavailable savings as `null`; the compact `bha project`/LSP impact view may still display `0`, which must not be read as a measured zero-saving result.

## Apply and rollback

`bha suggest` **does not modify files**. There is no standalone `bha apply` command. With LSP enabled, use the project command for CLI application or the VS Code client for editor application:

```bash
bha project record --project-root . --build-dir build --trace-dir build/traces
bha project analyze --project-root . --build-dir build --trace-dir build/traces
bha project apply --project-root . --build-dir build --trace-dir build/traces \
  --suggestion-id ana-2
bha project apply --project-root . --build-dir build --trace-dir build/traces \
  --all --safe-only
bha project revert --project-root . --backup-id <backup-id>
```

Suggestion IDs are analysis-specific; run `project analyze` and use an ID it actually returned, not the illustrative `ana-2`. `project suggest` is an alias for `project analyze`. The CLI and LSP construct manager-owned apply requests with the same workspace/build/trace context, analysis identity, source fingerprints, ordered suggestion IDs, and validation policy. Stale or drifted requests are refused before mutation.

The guarded apply path is:

1. Resolve saved analysis and check candidate policy, source state, project ownership, and affected files.
2. Create a **durable disk backup** under the workspace's `.lsp-optimization-backup/` before writing; new files are recorded so rollback can remove only files the transaction created. BHA does **not** shell out to `git checkout` to restore user files.
3. Apply exact text/byte replacements. Eligible semantic edits are syntax-checked with compile-command-backed affected TUs; a missing required command blocks application.
4. Run configured rebuild validation when requested. On a failed syntax or build gate, restore the backup and return diagnostics. A compiler pass is not a full semantic or behavioral proof: run project tests and inspect the diff.
5. For apply-all, select the highest-ranked eligible candidate, re-analyze after a successful edit when the default rerank policy has a project context, then consider remaining candidates against fresh evidence. A final rebuild validates the batch. If it fails, fault isolation can probe direct-edit subsets and retain only a freshly validated remainder; interactions or ambiguous failures revert the batch.

Apply-all is a single transactional request from the client perspective; it is not an unconditional loop over lexicographically sorted `ana-N` strings and it does not perform a combinatorial search. Candidates are compared by priority, then numeric estimated savings, confidence, and numeric ID as a deterministic tie-breaker. All current built-in suggesters leave numeric savings at zero pending measurement, so priority/confidence/tie-breakers usually control order. `--safe-only` excludes advisory and blocked candidates. A returned `backupId` is the handle for an explicit revert when a backup remains available. Check returned `buildValidation`, `rollback`, `faultIsolation`, and diagnostics rather than assuming success from the absence of an editor error.

The LSP also exposes `bha.applyDirectEdits` for a client-owned raw edit bundle. It receives transactional backup and validation, but it is **not** an evidence-backed BHA suggestion; VS Code's suggestion UI uses analyzed IDs instead. Keep workspace files saved before applying. VS Code rejects affected dirty documents and refreshes only clean open buffers after a successful server write.

Cancellation is cooperative. Background jobs have IDs and can be cancelled; an external shell command may only observe cancellation after it exits. Disk backups improve recovery, but process termination, disk failure, or a failing restore can still leave a partially changed tree. Inspect backup and working tree before repeating an interrupted operation.

## Reports and data contract

```bash
bha export traces --format html -o report.html
bha export traces --format json -o report.json --include-suggestions
bha export traces --format csv -o report-csv/ --include-suggestions
bha export traces --format md -o report.md
```

| Format | Use | Structure |
| --- | --- | --- |
| `html` | Interactive human review | Self-contained, dark-only Build Context dashboard with evidence and optional suggestion cards. |
| `json` | Automation and complete interchange | Canonical `bha-analysis` document, schema version `0.1.0`, explicit nullable metrics and capability provenance. Schema fixture: [`tests/fixtures/analysis-v0.1.0.schema.json`](tests/fixtures/analysis-v0.1.0.schema.json). |
| `csv` | SQL/spreadsheet import | A `.csv` filename is one rectangular file table. A directory path yields normalized tables (metadata, summary, files, headers, dependency edges, targets, modules, suggestions, edits, and more). Suggestions require the bundle form. |
| `md` | Lightweight PR or CI summary | Human-readable tables and evidence limitations; not a graph serialization format. |

Suggestion generation is **opt-in** on every exporter via `--include-suggestions` because it can require compilation database discovery, Clang AST work, and external `clang-tidy`. JSON is the canonical structured output; HTML embeds that analysis for rendering, while CSV represents relationships as separate edge tables rather than trying to put a graph into one cell. Format-specific `--no-*` switches suppress optional detail collections; aggregate metric provenance remains available.

Top-level JSON domains include `summary`, `performance`, `files`, `dependencies`, `templates`, `symbols`, `build_session`, `linker`, `targets`, `modules`, `process_resources`, and `cache_distribution`; `suggestions` appears only when requested. Use `schema_version` and capabilities when writing a consumer rather than assuming every domain is populated on every platform.

## Snapshots and regression gates

Snapshots store named analyses under `.bha/snapshots/` by default. `snapshot save` takes one trace file, not an arbitrary directory; choose a representative artifact and keep capture conditions with your benchmark record.

```bash
# Replace trace.json with an actual captured compiler trace file.
bha snapshot save before build/traces/trace.json -d "before change"
bha snapshot baseline set before
# Rebuild and capture a comparable artifact, then:
bha snapshot save after build/traces/trace.json -d "after change"
bha compare before after --threshold 5 --gate-tu 5 --gate-header 8 --gate-template 10
bha compare --baseline after --json
bha compare --repeat run-1 run-2 run-3 --json
```

`compare` reports changes in total and matched TU/header/template categories; a positive regression delta means slower, while the benchmark harness's `baseline - post` savings delta uses the opposite sign. Category gates return a nonzero exit status when their configured percentage limit is exceeded. Repeated-run mode reports minimum, rounded mean, nearest-rank median/P90/P99, maximum, and sample standard deviation when possible. These are descriptive observations, not a causal or statistical-significance test. Compare like-for-like compiler flags, hardware, caches, parallelism, and source revisions.

## LSP and IDE workflow

### Server contract

Build with `BHA_ENABLE_LSP=ON` and start `bha-lsp` over standard byte-counted LSP stdio. The server expects `initialize`, then `initialized`; runtime configuration arrives via `workspace/didChangeConfiguration` under `settings.optimization`. On Windows, stdio is explicitly binary so `Content-Length` is not corrupted by CRT newline translation.

| `workspace/executeCommand` command | Purpose |
| --- | --- |
| `bha.recordBuildTraces`, `bha.analyze` | Capture and analyze a workspace. |
| `bha.getSuggestionDetails`, `bha.listSuggesters`, `bha.runSuggester`, `bha.explainSuggestion`, `bha.showMetrics` | Inspect candidates, evidence, and current analysis identity. |
| `bha.applySuggestion`, `bha.applyAllSuggestions`, `bha.applyDirectEdits` | Guarded transaction paths; raw direct edits are an explicit escape hatch. |
| `bha.revertChanges`, `bha.listBackups` | Inspect and restore persisted backups. |
| `bha.getJobStatus`, `bha.cancelJob` | Poll/cancel long-running background operations. |

Long operations return an immediate `{accepted, async, jobId, status, progressToken}` acknowledgment when requested asynchronously. Follow `$/progress`, `bha/jobStarted`, and `bha/jobCompleted`; use the job ID for `bha.getJobStatus`/`bha.cancelJob`. Do not hold an LSP request open for a multi-minute rebuild. Apply requests should carry the `analysisId` from the current analysis, an operation ID for correlation, and the chosen suggestion ID(s); `bha.getSuggestionDetails` provides affected files and edit preview data. The server exposes code actions, but a generic LSP connection is not a complete BHA IDE integration.

Common `settings.optimization` controls include `rebuildAfterApply`, `rollbackOnBuildFailure`, `buildCommand`, `buildTimeout` (seconds), `backupDirectory`, `keepBackups`, `persistTrustLoop`, `allowMissingCompileCommands`, `includeUnsafeSuggestions`, and `minConfidence`. Configuration cannot disable manager-owned semantic/syntax preconditions for analyzed suggestions. A build command that cannot validate the changed project is not a meaningful safety gate.

A client can send a configuration notification after initialization (the individual optimization auto-apply switches default to `false`):

```json
{
  "jsonrpc": "2.0",
  "method": "workspace/didChangeConfiguration",
  "params": {
    "settings": {
      "optimization": {
        "rebuildAfterApply": true,
        "rollbackOnBuildFailure": true,
        "buildTimeout": 300,
        "backupDirectory": ".lsp-optimization-backup",
        "includeUnsafeSuggestions": false,
        "minConfidence": 0.5,
        "forwardDeclaration": { "autoApply": false },
        "includeReduction": { "autoApply": false }
      }
    }
  }
}
```

Leaving `allowMissingCompileCommands` enabled permits analysis where some commands are absent; it does not make an AST-dependent edit eligible without the exact commands needed for that edit. The client should show preview and consent for broad changes even when the server classifies a candidate as directly applicable.

When a baseline and validation rebuild are available, the apply result can include predicted-vs-actual timing (`trustLoop`). If enabled, records are appended to `<backupDirectory>/trust_loop.jsonl`. Inspect `baselineSource`: `recorded-build` is adapter wall time, while `trace-aggregate` is summed compiler work and is not directly comparable to a rebuild wall time. A zero `predictedSavingsMs` can mean the suggester made **no estimate**, not that zero saving was predicted from evidence.

### VS Code

The shipped client lives at `lsp/ide-integrations/vscode` and requires a separately built `bha-lsp` on `PATH` or an explicit `buildHotspotAnalyzer.serverPath`. The extension is a thin UI: it does not reimplement suggestion eligibility or file application. It offers a persistent analysis view, evidence/affected-file details, native diff previews, record/analyze/apply/revert commands, progress and cancellation, and a `Build Hotspot Analyzer` output channel. The last successful traced build profile is retained per workspace for apply-time validation when its paths are still valid.

```bash
cd lsp/ide-integrations/vscode
npm ci
npm run typecheck
npm run bundle
npm run package
code --install-extension build-hotspot-analyzer-0.1.0.vsix
```

From WSL, run `code` against the intended Windows VS Code installation and confirm `bha-lsp` is reachable from the extension host's environment; Linux and Windows path namespaces are not interchangeable. Extension settings are `buildHotspotAnalyzer.serverPath`, `buildHotspotAnalyzer.autoAnalyze`, and `buildHotspotAnalyzer.trace.server` (`off`, `messages`, `verbose`). The advanced record command accepts build-system, compiler, build type, jobs, output directory, and additional build arguments. Inspect `BHA: Show Activity Log` if the server fails to launch or a build profile is invalid.

The extension's own README remains in its package because VSIX tooling includes it as extension metadata; this root README is the source of project-wide technical documentation. There is **no shipped native CLion plugin**. CLion is a planned client, not an equivalent supported interface merely because a generic LSP connector can start `bha-lsp`.

## Architecture and extension points

| Layer | Main code | Contract |
| --- | --- | --- |
| Domain types and provenance | `headers/bha/types.hpp` | Compiler/build types, TU traces, sessions, capabilities, suggestion and edit records. |
| Parser registry | `headers/bha/parsers/`, `sources/bha/parsers/` | Detect valid artifacts, normalize without inventing unavailable fields. |
| Build/session inputs | `sources/bha/build_systems/`, `sources/bha/build_sessions/` | Drive core adapters, persist session sidecars, parse CMake producer models. |
| Analyzers | `headers/bha/analyzers/`, `sources/bha/analyzers/` | Independent domain analyses merged into one `AnalysisResult`. |
| Project/semantic index | `sources/bha/project_index.cpp`, semantic index sources | Resolve project-owned files and exact compile commands; Clang AST for edit eligibility. |
| Suggesters | `sources/bha/suggestions/` | Emit evidence, diagnostics, application modes, and optional exact edits. |
| Shared apply engine | `lsp/sources/bha/lsp/suggestion_manager*.{cpp,inc}` | Analysis-bound requests, ordering, backup, validation, reranking, rollback. |
| CLI and LSP | `cli/`, `lsp/` | User commands, LSP transport, background jobs, editor-facing results. |
| Export and storage | `sources/bha/exporters/`, `sources/bha/storage/` | Canonical JSON, rendered formats, snapshots and comparisons. |
| Presentation assets | `resources/`, `assets/` | Embedded HTML templates/vendor assets and repository branding. |

Registration happens at process startup in `cli/main.cpp` and `lsp/cli/lsp_server/main.cpp`. To add a parser, implement `ITraceParser` and register it; to add an analyzer, implement `IAnalyzer` and verify merge semantics; to add a suggester, implement `ISuggester`, describe its prerequisites, and test fail-closed cases; to add a build adapter, state its support tier and verify real trace production. Adding a field to an exporter also requires updating the canonical document/schema and the cross-format fixture. Avoid creating a second interpretation of suggestion safety in the VS Code client.

## Data and trust boundaries

Build artifacts can contain source paths, compiler flags, target names, and machine details. Treat trace bundles and exported reports as project data, not public telemetry. CMake static host information is retained without hostname, but can still include OS version, CPU counts, memory, processor name, and vendor when the producer supplies them. CMake captured command output is not copied into aggregate reports or snapshots; output-byte observations may be included.

The canonical JSON schema is versioned, but this is a pre-release `0.1.0` contract. Consumers should validate `schema_version`, respect nullable fields and `metric_capabilities`, and avoid joining records by display name when an exact producer ID is available. A source path or symbol mentioned in a trace is not semantic proof that a refactor is safe. BHA's AST checks depend on the compilation database being complete and representative of the configurations that will ship. CI builds on several operating systems provide useful coverage, but they cannot validate unbuilt feature flags, alternate compilers, or runtime behavior automatically.

## Validation and development

```bash
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=Debug \
  -DBHA_BUILD_TESTS=ON -DBHA_ENABLE_LSP=ON \
  -DBHA_REQUIRE_CLANG_TOOLING=ON
cmake --build build-dev --parallel 2
ctest --test-dir build-dev --output-on-failure --parallel 2
```

The suite covers parser rejection, analyzer provenance, source/AST eligibility, export schema/format parity, file backups, rollback/fault injection, CLI/LSP apply parity, and byte-exact LSP framing. `ExportCrossFormatFixture` requires Python 3; full JSON Schema validation additionally uses `jsonschema` when installed. `ApplyCliLspParity` is registered only when CLI, LSP, and Clang tooling are enabled. The CI matrix and VS Code extension-host workflow are in `.github/workflows/`; CI artifacts retain configure/build/test logs and trace fixture outputs.

For nontrivial repository validation, `tests/clone_repos.sh`, `tests/build_repos.sh`, `tests/run_repo_apply_benchmark.py`, and targeted `tests/run_*_subproject.py` harnesses exercise real and synthetic projects. The benchmark runner accepts one or more `--project` names from `tests/cli/repos`, creates a detached Git worktree for each, captures compiler traces, applies one eligible analyzed suggestion ID through the CLI or `--apply-mode vscode`, and compares median **untraced** `cmake --build --clean-first` wall time before and after the edit. CMake configuration time is excluded; each state is reconfigured outside the timed build. It keeps worktrees, traces, logs, JSON, and a Markdown summary under `tests/cli/benchmarks/` for review; it never resets the original clone. Run `python3 tests/run_repo_apply_benchmark.py --help` for workload and test-gate controls. Use `--require-tests` to fail if the project has no CTest coverage; `build_only` means the edit and rebuild succeeded but no CTest tests existed. `--require-applied` rejects missing or failed edits, but does not mislabel a build-only result as project-test validated. Timing comparisons should retain raw traces, project revision, compiler/version, build command, cache state, and machine context; a single successful fixture does not establish general optimization quality.

The manual [repository suggestion benchmark](.github/workflows/repo-apply-benchmark.yml) pins all 15 projects from `tests/clone_repos.sh`. On supported cells it records real compiler traces, requires a compilation database and a direct suggestion, runs the actual VS Code Extension Development Host against `bha-lsp`, applies the CLI-selected suggestion ID through the extension command, checks the changed worktree and LSP build validation, then measures clean post-edit builds and runs available CTest tests. The separate [extension-host contract matrix](.github/workflows/vscode-extension.yml) still uses a fake LSP for dirty-document behavior; it is not a substitute for these real-project jobs. The manual `all` choice schedules up to 39 supported project/runner jobs at two concurrent legs; this is deliberately expensive. Six unsupported cells are listed in the dispatch summary instead of counted as passes. The CI profile disables some optional upstream tests or features to keep jobs bounded; such results are `build_only`, not test-validated. No suggestion, trace, or post-edit build is reported as a successful edit. One clean build per state is a pipeline smoke measurement, not statistically reliable savings evidence.

The pinned build paths and dependencies were checked against upstream [CMake and platform guidance](https://cmake.org/cmake/help/latest/manual/cmake-generators.7.html) and each project's build instructions. These are **planned native paths**, not claims that every project/OS pair has passed CI:

| Project | Language | Linux Clang / macOS AppleClang | Windows MSVC 2022 | Upstream build evidence |
| --- | --- | --- | --- | --- |
| benchmark | C++ | CMake, Ninja; benchmark tests | Same CMake path | [Build instructions](https://github.com/google/benchmark/blob/main/README.md) |
| mimalloc | C | CMake, Ninja; test executables | Same CMake path | [Build instructions](https://github.com/microsoft/mimalloc/blob/main/readme.md) |
| RocksDB | C++ | CMake, optional components disabled | CMake/VS 2022; optional dependencies disabled | [CMake Windows prerequisites](https://github.com/facebook/rocksdb/blob/main/CMakeLists.txt) |
| zstd | C | CMake from `build/cmake` | Same CMake subdirectory | [CMake build instructions](https://github.com/facebook/zstd/blob/dev/build/cmake/README.md) |
| Abseil | C++ | CMake; GoogleTest-dependent tests disabled | Same CMake path | [CMake build instructions](https://github.com/abseil/abseil-cpp/blob/master/CMake/README.md) |
| Catch2 | C++ | CMake; self-tests disabled for cost | Same CMake path | [CMake integration](https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md) |
| LevelDB | C++ | CMake; tests enabled, GoogleTest submodule | Same CMake path | [Build instructions](https://github.com/google/leveldb/blob/main/README.md) |
| yaml-cpp | C++ | CMake; tests enabled | Same CMake path | [Build instructions](https://github.com/jbeder/yaml-cpp/blob/master/README.md) |
| libjpeg-turbo | C | CMake; SIMD disabled to avoid NASM dependency | Same CMake path | [Build instructions](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/BUILDING.md) |
| GLFW | C | CMake; Linux X11 development headers; interactive tests disabled | CMake with Win32 backend; interactive tests disabled | [Build options](https://github.com/glfw/glfw/blob/master/CMakeLists.txt) |
| Redis | C | Native Make; BHA's shipped CLI does not register its experimental Make adapter | No upstream native MSVC build | [Source build](https://redis.io/docs/latest/operate/oss_and_stack/install/install-redis/install-redis-from-source/) |
| curl | C | CMake; optional TLS/PSL/SSH2 features disabled | CMake/MSVC path | [CMake installation guide](https://github.com/curl/curl/blob/master/docs/INSTALL-CMAKE.md) |
| zlib | C | CMake | Same CMake path | [CMake build file](https://github.com/madler/zlib/blob/master/CMakeLists.txt) |
| libpng | C | CMake; zlib development library | CMake; vcpkg zlib | [Project/build files](https://github.com/pnggroup/libpng) |
| Weston | C | Meson on Linux; BHA benchmark is still CMake-only | No verified native MSVC or macOS build path | [Upstream Meson test guidance](https://wayland.pages.freedesktop.org/weston/toc/test-suite.html) |

The C/C++ split above matters: the matrix is not a C++-only exercise. Redis and Weston remain explicit capability gaps until their native Make/Meson adapters are enabled and verified with real trace and compilation-database evidence through the same editor apply gate. The experimental Make adapter has a Bear-based compilation-database fallback; it is not yet a verified CLI path. The workflow never treats a missing or unsafe suggestion as an optimization success.

Documentation checks are run by `.github/workflows/docs.yml` on this root README. Optional local commands are `npx markdownlint-cli2 README.md`, `vale README.md`, and `lychee --config .lychee.toml README.md` when those tools are installed.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| CMake cannot find `nlohmann_json` or GTest | Install the package or allow FetchContent/network access; inspect CMake's configure output. |
| Configure says LibTooling is absent | Supply the LLVM/Clang CMake prefix or `BHA_CLANG_TOOLING_ROOT`; set `BHA_REQUIRE_CLANG_TOOLING=ON` to fail fast. AppleClang alone is not the LibTooling package. |
| No traces after `bha build` | Confirm an actual compilation occurred, compiler timing flags/launcher were applied, and output directory contains valid artifacts. Try a clean disposable build tree. |
| `bha build` cannot detect the project | CLI registers CMake and MSBuild by default. Use project root or collect compiler traces manually for another build system. |
| Many metrics are zero or unavailable | Inspect `metric_capabilities`; add the exact CMake instrumentation index, File API reply, sccache/P1689/resource sidecar, or `.su` producer input required by the metric. |
| No include/template/PCH suggestion | Verify compilation database, Clang tooling, `clang-tidy` where applicable, and required per-specialization/include evidence. `--explain` can expose diagnostics, not waive gates. |
| Apply refuses an apparently valid suggestion | Save dirty buffers, re-run analysis, inspect `auto_apply_blocked_reason` and compile-command coverage, then inspect build-validation diagnostics. |
| LSP client stalls or disconnects | Use async jobs and `bha.getJobStatus`; check output channel, server path, `Content-Length` framing, and build timeout. |
| Regression gate is noisy | Compare repeated clean runs with the same toolchain, flags, cache state, parallelism, and machine. Use `--repeat` for descriptive spread before setting thresholds. |

## Known limits

- BHA is not a C/C++ compiler, source formatter, generic include graph resolver, or proof of semantic equivalence. A successful syntax check and full build cannot replace project tests, ABI review, or runtime validation.
- Unsupported or partial producer data remain unavailable. No NVCC `--time` or Intel Classic optimization-report timing is accepted; the CUDA CI probe does not change that.
- The shipped CLI supports only core CMake/MSBuild build adapters. Other adapter implementations are experimental library opt-ins, not a tested cross-platform CLI guarantee.
- LLD time-trace parsing exists in the library but is not currently exposed as a CLI attachment. There is no native CLion plugin yet.
- A new suggestion's expected saving is usually unestimated. Apply-all may rerun expensive analysis/build steps between edits; measure the actual post-edit trace before claiming improvement.
- Recovery is best-effort under abrupt termination or filesystem failure. Preserve the working tree and inspect durable backups if a transaction is interrupted.

Repository: [gregorian-09/build-hotspot-analyzer](https://github.com/gregorian-09/build-hotspot-analyzer). Report bugs and attach minimal trace/build reproductions in [Issues](https://github.com/gregorian-09/build-hotspot-analyzer/issues).
