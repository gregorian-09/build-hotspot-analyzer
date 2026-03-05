# PIMPL Completion Plan

This file tracks the remaining work required to finish PIMPL support with a clear, tested, and safe support boundary.

Status markers:
- `[done]`
- `[in-progress]`
- `[pending]`

## Phase 1: Lock The Current Contract
Status: `[done]`

Goal:
- Freeze the current support boundary so it stays stable while support is broadened.

Scope:
- Make the current application contract explicit in user-facing outputs.
- Add deterministic tests for the current supported and blocked shapes.
- Ensure the suggester and refactor tool do not disagree on already-covered fixtures.

Current progress:
- `[done]` Exporter shows application mode and refactor context.
- `[done]` LSP suggestion details expose application summary/guidance and blocked reasons.
- `[done]` End-to-end fixture coverage exists for:
  - `direct-edits`
  - `external-refactor`
  - `advisory`
- `[done]` Negative regression fixtures now cover:
  - explicit user-defined copy bodies
  - inheritance
  - template classes
  - private inline method bodies
  - macro-generated private declarations
- `[done]` Focused unit tests now assert PIMPL application-mode resolution and string parsing directly in `tests/unit/core/test_types.cpp`.
- `[done]` The consistency matrix runner now compares suggester classification against `bha-refactor` acceptance/rejection for the same fixture set in `tests/run_pimpl_consistency_matrix.py`.

Exit criteria:
- Every covered PIMPL fixture has a deterministic expected mode.
- The current supported contract is explicit in outputs and validated in automated tests.

## Phase 2: Remove Remaining Heuristic Drift
Status: `[done]`

Goal:
- Make analysis-time classification and apply-time acceptance share the same semantic rules as much as possible.

Scope:
- Move blocker decisions onto semantic/compile-context-aware checks first.
- Keep text fallback only where compile context is unavailable.
- Align the suggester with `bha-refactor` so unsupported shapes are downgraded before apply.

Current progress:
- `[done]` Explicit user-defined copy bodies are semantically detected and downgraded to `advisory`.
- `[done]` `bha-refactor` now treats semantic safety rejections as terminal (no confusing fallback on hard blockers).
- `[done]` Inheritance is downgraded to `advisory` at analysis time.
- `[done]` Template classes are downgraded to `advisory` at analysis time.
- `[done]` Private inline method bodies are downgraded to `advisory` at analysis time.
- `[done]` Macro-generated private declarations are downgraded to `advisory` at analysis time.
- `[done]` Header discovery has been hardened so relative and bare trace paths still find the actual header under `include/`, `header/`, or `headers/`.
- `[done]` The shared `PimplEligibilityState` evaluator now drives both analysis-time `external-refactor` classification and `bha-refactor` hard rejection.
- `[done]` Remaining blocker detection now prefers compile-context extraction and class-range source-location checks when semantic context is available; text regex fallbacks are limited to missing semantic extraction paths.
- `[done]` Direct unit tests now validate shared eligibility output in `tests/unit/suggestions/test_pimpl_eligibility.cpp`.

Exit criteria:
- One shared eligibility model drives both analysis-time mode selection and apply-time hard rejection.
- Text-only fallbacks are limited to no-compile-db cases.

## Phase 3: Finish Structural Refactor Generation
Status: `[in-progress]`

Goal:
- Replace remaining line-oriented structural generation with source-range-driven replacements for supported shapes.

Scope:
- Generate header/source structural edits from stable source ranges where possible.
- Preserve original declarations and qualifiers exactly when possible.
- Reduce dependence on reconstructed text blocks.

Planned work:
- Move `struct Impl;` insertion to source-range-aware generation.
- Move `std::unique_ptr<Impl> pimpl_;` insertion to source-range-aware generation.
- Move private field extraction into range-based replacement instead of line slicing.
- Preserve:
  - qualifiers
  - attributes
  - `noexcept`
  - `= default`
  - `= delete`
  from original declarations.

Current progress:
- `[done]` Strict PIMPL private-section replacement now computes the full private section end from class-range source scanning (`find_private_section_end_line`) instead of ending at the last private member line.

Exit criteria:
- Structural edits for supported shapes are source-range-based rather than line-rebuilt.

## Phase 4: Finish Semantic Method Rewriting
Status: `[pending]`

Goal:
- Make body rewriting robust enough to support more class shapes without unsafe text substitutions.

Scope:
- Expand AST-driven member access rewriting coverage.
- Reject unsupported constructs before edits are applied.

Planned work:
- Broaden support for:
  - unqualified field references
  - `this->field`
  - nested access
  - references inside lambdas
  - more complex expressions that still resolve cleanly
- Reject early when unsupported:
  - macro-expanded bodies
  - ambiguous dependent names
  - unsupported captures or shadowing

Exit criteria:
- Method rewrite coverage is explicit, tested, and unsupported bodies fail before edits are applied.

## Phase 5: Broaden Supported Class Shapes Carefully
Status: `[pending]`

Goal:
- Convert selected current `advisory` shapes into supported automation only when the semantic transformer can prove them safe.

Recommended order:
1. Select private inline methods that can be moved out-of-line safely.
2. Narrow macro cases with stable source locations.
3. Narrow template cases with non-dependent behavior.
4. Trivial inheritance cases, if ABI and API preservation remain explicit.

Rule for each new supported shape:
- Add a positive fixture.
- Add a negative sibling fixture.
- Add eligibility logic.
- Add transformation logic.
- Require rebuild validation.

Exit criteria:
- Each newly supported shape has positive and negative regression coverage with no regressions to existing support.

## Phase 6: Rebuild Validation And Rollback Hardening
Status: `[pending]`

Goal:
- Make PIMPL auto-apply operationally safe under failure, not just logically safe when it succeeds.

Scope:
- Standardize:
  - snapshot
  - apply
  - rebuild
  - rollback on failure
- Return richer structured failure reasons from `bha-refactor`.
- Surface rollback results clearly in LSP and exporter.

Exit criteria:
- Failed applies restore original files reliably and explain why.

## Phase 7: UX And Export Completion
Status: `[pending]`

Goal:
- Make the PIMPL support boundary obvious to users without reading code.

Scope:
- Show exact apply mode and blocker reason everywhere.
- Make “why advisory” and “why blocked” explicit.

Planned work:
- Expand exporter detail sections for PIMPL blocker reasons.
- Expand LSP details and IDE integration messages for refactor eligibility.
- Keep wording aligned between suggester and refactor tool.

Exit criteria:
- Users can immediately see what is auto-applicable, what is advisory, and what is blocked.

## Phase 8: Final Completion Gate
Status: `[pending]`

Goal:
- Declare PIMPL complete for the product with an explicit, tested support boundary.

Completion definition:
- All supported shapes are semantic-first.
- No known unsafe auto-apply path remains.
- The suggester and `bha-refactor` agree on covered cases.
- Unsupported shapes fail early with explicit reasons.
- Regression coverage exists for:
  - supported paths
  - advisory paths
  - terminal rejections

This phase does not mean “arbitrary C++ with no exclusions”.
It means the support boundary is explicit, safe, tested, and shippable.
