# LSP Reference

Optional executable: `bha-lsp`

Enable build:

```bash
cmake -S . -B build -DBHA_ENABLE_LSP=ON
cmake --build build -j
```

Binary path is typically:
- `build/lsp/bha-lsp` or
- `build/bha-lsp`

## Protocol Surface

Standard LSP:
- `initialize`
- `initialized`
- `shutdown`
- `exit`
- `workspace/executeCommand`
- `textDocument/codeAction`
- `workspace/didChangeConfiguration`

Custom execute commands:
- `bha.analyze`
- `bha.applySuggestion`
- `bha.applyDirectEdits`
- `bha.applyAllSuggestions`
- `bha.getSuggestionDetails`
- `bha.revertChanges`
- `bha.showMetrics`
- `bha.listSuggesters`
- `bha.runSuggester`
- `bha.explainSuggestion`

## Initialization

Minimal initialize payload:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {
    "rootUri": "file:///path/to/project",
    "capabilities": {}
  }
}
```

After initialize, send:
- `initialized` notification
- optionally `workspace/didChangeConfiguration`

## Runtime Configuration Schema

Configuration is read from `settings.optimization`.

```json
{
  "settings": {
    "optimization": {
      "autoApplyAll": false,
      "showPreviewBeforeApply": true,
      "rebuildAfterApply": true,
      "rollbackOnBuildFailure": true,
      "buildCommand": "bha build",
      "buildTimeout": 300,
      "keepBackups": false,
      "backupDirectory": ".lsp-optimization-backup",
      "persistTrustLoop": true,
      "allowMissingCompileCommands": true,
      "includeUnsafeSuggestions": false,
      "minConfidence": 0.5,
      "pch": { "autoApply": false },
      "headerSplitting": { "autoApply": false },
      "unityBuild": { "autoApply": false },
      "templateOptimization": { "autoApply": false },
      "includeReduction": { "autoApply": false },
      "forwardDeclaration": { "autoApply": false },
      "pimpl": { "autoApply": false },
    }
  }
}
```

## Command Contracts

### `bha.analyze`

Input (`arguments[0]`):
- `projectRoot` (optional, defaults to workspace root)
- `buildDir` (optional)
- `rebuild` (bool)
- `enabledTypes` (array)
- `includeUnsafe` (bool)
- `minConfidence` (number)
- `disableConsolidation` (bool)
- `explain` (bool; includes suggestions normally filtered by output policy,
  without bypassing semantic or producer-evidence validation)

Output:
- `analysisId`
- `suggestions`
- `baselineMetrics`
- `filesAnalyzed`
- `durationMs`
- `unrealEnvironmentChecks` (when Unreal markers exist)

### `bha.applySuggestion`

Input:
- `suggestionId` (required)
- `skipRebuild` (optional)
- `skipConsent` (optional)

Suggestion validation cannot be disabled by the client. The manager always
enforces the configured source-state, semantic/syntax, and post-apply build
gates; `skipRebuild` only controls whether the configured rebuild is requested.

Output includes:
- `success`
- `changedFiles`
- `errors`
- `backupId`
- `buildValidation`
- `rollback`
- `trustLoop`

### `bha.applyDirectEdits`

This is an explicit raw-edit escape hatch for clients that already own an edit
bundle. It is not an evidence-backed suggestion and is not used by the VS Code
suggestion UI. Analyzed suggestions must use `bha.applySuggestion` or
`bha.applyAllSuggestions` so the manager can enforce analysis-state preconditions,
semantic validation, deterministic ordering, and transactional rollback.

Input supports:
- `edits`
- `textEdits`
- `text_edits`
- nested `suggestions[*]` edit arrays
- optional `projectRoot`

Output mirrors apply workflow:
- `success`
- `changedFiles`
- `errors`
- `backupId`
- `buildValidation`
- `rollback`
- `trustLoop` (manual edit path returns unavailable reason)

### `bha.applyAllSuggestions`

Input:
- `minPriority` (string or numeric)
- `safeOnly` (bool)
- `skipRebuild` (bool)
- `skipConsent` (bool)

Apply-all is always one transactional operation. The server ignores no
caller-selected atomicity mode because partial retention is not part of the
apply contract.

The evidence-backed apply commands operate on the saved workspace files. A
client with unsaved editor buffers must either save or discard changes and
re-run analysis before applying; the VS Code client refuses to apply when any
affected file is dirty rather than silently overwriting the buffer.

Output:
- `success`
- `appliedCount`
- `skippedCount`
- `failedCount`
- `appliedSuggestionIds`
- `changedFiles`
- `errors`
- `backupId`
- `buildValidation`
- `rollback`
- `trustLoop`

### `bha.getSuggestionDetails`

Input:
- `suggestionId`

Output includes:
- suggestion fields
- `filesToCreate`
- `filesToModify`
- `dependencies`
- `applicationSummary`
- `applicationGuidance`
- `autoApplyBlockedReason`
- `textEdits` and `text_edits`

### `bha.revertChanges`

Input:
- `backupId`

Output:
- `success`
- `restoredFiles`
- `errors`

### `bha.listSuggesters`

Output:
- `suggesters[]` with:
  - `id`
  - `className`
  - `description`
  - `supportedTypes`
  - `inputRequirements`
  - `potentiallyAutoApplicable`
  - `supportsExplainMode`

### `bha.runSuggester`

Input:
- `suggester` id/class token
- plus optional `bha.analyze` fields

Behavior:
- resolves descriptor
- injects `enabledTypes` for that suggester
- calls analysis and returns suggester-scoped results

### `bha.explainSuggestion`

Two modes:
- with `suggestionId`: returns details for that suggestion
- with `suggester`: returns descriptor + usage contract

## Safety and Rollback

LSP apply paths can:
- create backup snapshots under configured backup directory
- run rebuild validation
- rollback automatically when validation fails
- emit diagnostics for rollback success/failure

## Trust Loop Metrics

When validation runs and data is available, trust-loop payload includes:
- `predictedSavingsMs`
- `actualSavingsMs`
- `predictionDeltaMs`
- `baselineBuildMs`
- `rebuildBuildMs`
- `actualSavingsPercent`
- `predictionErrorPercent`
- `status` (`met-or-exceeded`, `below-prediction`, `no-change`, `regression`)

When enabled (`persistTrustLoop=true`), records are appended to:
- `<backupDirectory>/trust_loop.jsonl`

## Unreal Notes

For Unreal projects, `bha.analyze` can return readiness diagnostics in:
- `unrealEnvironmentChecks`

Checks cover:
- project marker detection
- build tooling discovery
- module/target rules presence
- optional IDE metadata hints
