# BHA LSP (Optional Module)

This directory contains the optional Language Server Protocol (LSP) and IDE
integrations for Build Hotspot Analyzer.

Core guidance:
- The main repository focuses on analysis + reporting.
- LSP/IDE integration is optional and should be built only when needed.

To build `bha-lsp` from this repo:
```
cmake -DBHA_ENABLE_LSP=ON -B build
cmake --build build
```

## Unreal Workflow Notes

The LSP now emits Unreal readiness diagnostics during `bha.analyze` when Unreal
project markers are detected (`.uproject`, `*.Build.cs`, `*.Target.cs`).

Response field:
- `unrealEnvironmentChecks`: array of structured checks with `id`, `status`,
  `severity`, `message`, and optional `recommendedAction`.

Current checks include:
- project detection
- Unreal build tooling discovery (`BHA_UE_BUILD_SCRIPT`, `UE_ENGINE_ROOT`,
  `UNREAL_ENGINE_ROOT`, or `UnrealBuildTool` on `PATH`)
- ModuleRules/TargetRules presence
- optional IDE workflow hint (Rider/Visual Studio metadata detection)

These are warnings/info only; they do not block analysis.

## Rebuild Validation (Unreal)

For Unreal auto-apply workflows, configure one of:
- `BHA_UE_BUILD_SCRIPT` to point to a project-specific Unreal build wrapper.
- `UE_ENGINE_ROOT` or `UNREAL_ENGINE_ROOT` for engine script discovery.
- `UnrealBuildTool` on `PATH`.

If none are available, suggestions can still be generated, but rebuild
validation will report missing Unreal tooling in readiness checks.
