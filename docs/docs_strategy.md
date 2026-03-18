# Documentation Strategy (Production-Oriented)

Last reviewed: 2026-03-15

## Goals

This project documentation should:
- help new users reach first value quickly
- support production operators and CI owners
- provide complete, stable references for CLI/LSP integration
- remain maintainable as features evolve

## Information Architecture

This repo uses a Diataxis-aligned split:
- Tutorials: skill-building flow (`quickstart.md`)
- How-to guides: operational tasks (`installation.md`, `export_ci.md`, `troubleshooting.md`)
- Reference: exact command/API contracts (`cli_reference.md`, `lsp_reference.md`, `suggestions_reference.md`)
- Explanation: internal model and tradeoffs (`architecture.md`, `repo_validation.md`)

This separation prevents mixed-purpose pages and improves discoverability.

## Editorial Principles

1. Task-first writing
- Start pages with user intent and expected outcome.
- Keep setup and command examples close to the action.

2. Accuracy over volume
- Prefer concise exact behavior to broad generic guidance.
- Keep docs synchronized with real command flags and response fields.

3. Docs-as-code
- Keep documentation in repo and review in the same workflow as code.
- Treat doc updates as part of definition-of-done for behavior changes.

4. CI-oriented outputs
- Document SARIF and PR annotation paths clearly.
- Document non-zero gate behaviors and operational consequences.

5. Change-safe guidance
- Include rollback/validation expectations for auto-apply features.
- Distinguish advisory, direct-edit, and external-refactor modes.

## Maintenance Policy

When behavior changes, update:
- command help and command docs
- LSP payload docs for request/response deltas
- suggester safety/eligibility semantics
- exporter output behavior notes

## Recommended Future Enhancements

1. Expand docs lint severity policy
- current CI includes `markdownlint`, `vale`, and `lychee` link checks
- next step is staged severity (`warning` on PR, `error` on protected branch)

2. Add versioned docs snapshots per release
- tag docs with release branch/version

3. Add docs ownership map
- assign module-level doc owners (CLI/LSP/suggesters/export)

4. Add runnable snippet checks
- periodically execute key command snippets in CI smoke jobs

## External Guidance Used

- Diataxis framework:
  - https://diataxis.fr/
  - https://diataxis.fr/how-to-use-diataxis/
- Google developer documentation style guide:
  - https://developers.google.com/style
  - https://developers.google.com/style/headings
- Microsoft developer/reference writing guidance:
  - https://learn.microsoft.com/en-us/style-guide/developer-content/
  - https://learn.microsoft.com/en-us/style-guide/developer-content/reference-documentation
- Write the Docs principles:
  - https://www.writethedocs.org/guide/writing/docs-principles.html
- GitLab docs-as-code workflow/style examples:
  - https://docs.gitlab.com/development/documentation/styleguide/
  - https://docs.gitlab.com/development/documentation/testing/
