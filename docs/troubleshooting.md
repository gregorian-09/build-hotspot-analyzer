# Troubleshooting

## Build/Configuration Issues

### `nlohmann_json not found`

Symptom:
- configure stage cannot find system package

Resolution:
- allow FetchContent fallback (default behavior)
- or install system package and reconfigure

### `No trace files found`

Symptom:
- `bha analyze`/`bha export` fails immediately

Resolution:
1. confirm trace generation command succeeded
2. verify output directory and extensions
3. run `bha build --output traces`
4. pass explicit directory/file paths

### `Could not detect build system`

Symptom:
- `bha build` cannot auto-select adapter

Resolution:
- provide `--build-system <name>` explicitly
- run command from project root containing build markers

## Suggestion/Apply Issues

### Include removal suggestions are missing

Symptom:
- `include-removal` not emitted when expected

Resolution:
1. ensure `compile_commands.json` exists for target project
2. ensure `clang-tidy` is installed and discoverable
3. rerun with `--verbose` and inspect diagnostics

### Suggestions exist but apply reports no changes

Possible causes:
- advisory/external-refactor mode suggestion
- blocked auto-apply (`auto_apply_blocked_reason`)
- filtered by priority/confidence/safety flags

Resolution:
- inspect details:
  - CLI JSON output
  - LSP `bha.getSuggestionDetails`
- run without over-restrictive filters for diagnosis

### Build fails after apply

Expected behavior when rollback enabled:
- changes are reverted from backup automatically

Action:
1. inspect apply errors and rollback diagnostics
2. inspect backup directory for affected file list
3. apply suggestions incrementally instead of batch mode

## LSP Issues

### Server not initialized

Symptom:
- LSP command returns `Server not initialized`

Resolution:
1. send `initialize`
2. send `initialized`
3. then execute `workspace/executeCommand`

### Config changes do not take effect

Symptom:
- runtime settings appear unchanged

Resolution:
- send `workspace/didChangeConfiguration` with `settings.optimization` payload after initialize

### Missing Unreal build validation tooling

Symptom:
- Unreal readiness checks warn about missing build tooling

Resolution:
- define one of:
  - `BHA_UE_BUILD_SCRIPT`
  - `UE_ENGINE_ROOT`
  - `UNREAL_ENGINE_ROOT`
- or put `UnrealBuildTool` on `PATH`

## Comparison/Gates Issues

### Compare fails due to missing baseline

Resolution:

```bash
bha snapshot baseline set <snapshot_name>
```

### Gate keeps failing with noisy delta

Resolution:
- ensure comparable build environments and flags
- reduce external variability (cache state, machine load, parallelism)
- tune gate thresholds to realistic team policy

## Debugging Checklist

1. Run with `--verbose`.
2. Save JSON outputs (`analyze`, `suggest`, `export`).
3. Validate trace input quality.
4. Verify `compile_commands.json` correctness.
5. Reproduce with a minimal project slice.
6. Use snapshot compare to separate regression from baseline noise.

