# Documentation Coverage Matrix

Last updated: 2026-03-18

Coverage scope:
- user-facing workflows (install, analyze, suggest, apply, export, compare)
- contributor-facing extension points and validation workflow
- IDE integration usage and distribution

Excluded from this matrix:
- temporary test scaffolding under `tests/temp`
- one-off local debug notes and ad-hoc traces

## Coverage Summary

- Total tracked areas: `18`
- Fully documented: `18`
- Partial: `0`
- Missing: `0`
- Coverage: `100%` (within the scope above)

## Matrix

| Area | Primary docs | Status | Notes |
| --- | --- | --- | --- |
| Project overview and feature scope | `README.md`, `docs/readme.md` | Complete | Includes command surface and architecture map |
| Installation and build profiles | `docs/installation.md` | Complete | CLI/LSP build options and environment notes |
| End-to-end onboarding workflow | `docs/quickstart.md` | Complete | Analyze, suggest, apply, export, compare flow |
| CLI command reference | `docs/cli_reference.md` | Complete | `analyze`, `suggest`, `apply`, `build`, `record`, `export`, `snapshot`, `compare` |
| LSP protocol reference | `docs/lsp_reference.md` | Complete | Runtime config, command contracts, payloads |
| IDE usage and distribution | `docs/ide_integrations.md` | Complete | VS Code, Neovim, Emacs, publish prerequisites |
| Suggestion families and safety | `docs/suggestions_reference.md` | Complete | All suggesters, trust loop, safety gates |
| Export formats and CI wiring | `docs/export_ci.md` | Complete | JSON/HTML/CSV/SARIF/annotations + CI usage |
| Regression gates and snapshot model | `docs/cli_reference.md`, `docs/export_ci.md` | Complete | Thresholds and category gate behavior |
| Architecture and component boundaries | `docs/architecture.md` | Complete | Data flow, modules, extension points |
| Development workflow | `docs/development.md` | Complete | Local dev loop, tests, release readiness |
| Troubleshooting playbook | `docs/troubleshooting.md` | Complete | Common failures and recovery steps |
| Repo-wide validation methodology | `docs/repo_validation.md` | Complete | Baseline/post-apply methodology and artifacts |
| Documentation policy and quality gates | `docs/docs_strategy.md` | Complete | Diataxis, linting, docs CI |
| LSP apply semantics and rollback | `docs/lsp_reference.md`, `docs/suggestions_reference.md` | Complete | Apply modes, backup, rollback, trust loop |
| Cache/distribution and explainability outputs | `docs/architecture.md`, `docs/export_ci.md`, `docs/suggestions_reference.md` | Complete | Output payload semantics documented |
| PR-native outputs (SARIF/annotations) | `docs/export_ci.md` | Complete | GitHub/GitLab-ready outputs and commands |
| Branding and visual assets | `docs/assets/*` | Complete | Primary, monochrome, and icon logo variants |

## Maintenance Rule

When adding a new public command, suggester, analyzer, exporter, adapter, or IDE client:
1. update the relevant reference doc
2. update this matrix row (or add a new row)
3. keep `README.md` and `docs/readme.md` links in sync
