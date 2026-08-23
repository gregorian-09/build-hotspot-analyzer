# Suggestions Reference

This document explains each suggester, expected inputs, and application behavior.

## Suggestion Families

### 1. `pch` (PCH Optimization)

Purpose:
- identify repeatedly parsed expensive headers
- report exact repeated inclusion and compatible compile-command evidence
- leave build-system-specific PCH design and configuration to the project

Inputs:
- trace files
- compilation database covering the observed includers

Typical outputs:
- advisory evidence for a candidate header
- zero estimated savings until a configured build is measured again

Application mode:
- `advisory`; no PCH source or build-system edit is emitted

Guardrails:
- every observed includer must have an exact compilation command
- the includers must share one compiler, language mode, working directory, and
  preprocessor environment after output-only arguments are removed
- PCH configuration remains compiler/build-system-specific and requires a
  fresh validation trace

### 2. `forward-decl` (Forward Declaration)

Purpose:
- replace unnecessary heavyweight includes in headers with forward declarations
- move required includes to corresponding source file where safe

Inputs:
- trace files

Application mode:
- often `direct-edits`

Safety constraints:
- avoids by-value/inheritance layouts requiring complete type
- avoids circular dependency and invalid declaration contexts

### 3. `include-removal`

Purpose:
- remove semantically unused includes

Inputs:
- trace files
- `compile_commands.json`
- `clang-tidy` for semantic verification (`misc-include-cleaner`)

Application mode:
- `direct-edits` when explicit semantic evidence is present

Guardrails:
- suggestion quality is evidence-driven, not based on include frequency alone


Purpose:
- move includes from header to source when only implementation needs the full type

Inputs:
- trace files

Application mode:
- `direct-edits` for safe cases

### 5. `template-instantiation`

Purpose:
- reduce repeated template codegen cost via explicit instantiation strategy

Inputs:
- trace files
- build-system files for integrating generated instantiation translation units

Typical outputs:
- create/update `template_instantiations.cpp`
- add `extern template` declarations in headers
- update build target sources

Application mode:
- often `direct-edits`

### 6. `unity-build`

Purpose:
- group many small translation units to reduce repeated include parsing overhead

Inputs:
- trace files
- build-system files

Application mode:
- `direct-edits` when the exact target and merged translation unit pass validation

Guardrails:
- requires exact CMake target ownership, compile-database evidence for every
  source, and Clang syntax validation of the merged translation unit

### 7. `header-split`

Purpose:
- split large high-fanout headers to reduce transitive parse cost

Inputs:
- trace files

Application mode:
- generally advisory in conservative mode (higher semantic risk)

Guardrails:
- generated `_fwd` declarations preserve class-key consistency (`class`/`struct`) for the same canonical type name to avoid `-Wmismatched-tags` ABI portability warnings
- when `_fwd` is wired into the original header, redundant in-header forward declarations are removed to prevent declaration drift
- generated `_fwd` headers copy leading source-header comment preambles (license/copyright banners) for policy consistency

### 8. `pimpl`

Purpose:
- reduce rebuild surface by isolating private-heavy implementation details

Inputs:
- trace files
- `compile_commands.json` for external refactor apply path

Application mode:
- usually `external-refactor` or advisory
- auto-apply only in strict eligible subsets

Guardrails:
- blocks risky class shapes (inheritance/virtual/copy semantics/template complexity as applicable)
- reports `auto_apply_blocked_reason` when automation is intentionally denied

## Suggester Discovery and Introspection

List all suggesters:

```bash
bha suggest --list-suggesters
```

Describe one:

```bash
bha suggest --describe-suggester pch
```

LSP equivalents:
- `bha.listSuggesters`
- `bha.explainSuggestion`
- `bha.runSuggester`

## Suggestion Object Semantics

Key fields:
- `type`, `priority`, `confidence`
- `estimated_savings` and `%`
- `implementation_steps`, `caveats`, `verification`
- `hotspot_origins` for producer/AST-backed explainability when exact evidence is available
- application metadata:
  - `application_mode`
  - `is_safe`
  - `application_summary`
  - `application_guidance`
  - `auto_apply_blocked_reason`

Edit payload:
- `edits` (`TextEdit[]`) for direct-apply paths

## How to Operate Safely

Recommended policy:
1. Start with `safeOnly` application paths.
2. Always run rebuild + tests after apply.
3. Use rollback-enabled workflows.
4. For advisory/external-refactor suggestions, apply in scoped PRs with explicit review.
5. Re-measure and compare against baseline snapshots.

## Evidence Controls

BHA does not use filename, age, fixed-percentage, or threshold heuristics as
semantic evidence. `--explain` can expose suggestions normally filtered by
confidence or safety policy, but it cannot bypass AST, compilation-database,
producer-schema, or post-edit validation requirements.
