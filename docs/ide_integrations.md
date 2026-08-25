# IDE Integrations

This guide documents how to use, package, and distribute the in-tree BHA IDE integrations as a user.

Supported clients:
- VS Code: `lsp/ide-integrations/vscode`

All clients talk to the same language server:
- `bha-lsp`

## Build And Runtime Prerequisites

### Build the server

```bash
cmake -S . -B build -DBHA_ENABLE_LSP=ON
cmake --build build -j
```

### Make `bha-lsp` discoverable

Use one of these approaches:
- add the build output directory to `PATH`
- point the client directly at the `bha-lsp` executable

## Recommended Distribution Strategy

For this project, the lowest-friction path is:
1. GitHub-first distribution
2. manual `.vsix` install for VS Code
3. native CLion plugin distribution after the plugin is implemented
4. Marketplace and Open VSX later, only if needed

This matters because Microsoft Marketplace publication may require Azure DevOps billing setup and can block otherwise-usable editor integrations.

## VS Code

### Local packaging

From `lsp/ide-integrations/vscode`:

```bash
npm ci
npm run package
```

This produces:
- `build-hotspot-analyzer-0.1.0.vsix`

### Local installation

```bash
code --install-extension build-hotspot-analyzer-0.1.0.vsix
```

### Runtime settings

- `buildHotspotAnalyzer.serverPath`
- `buildHotspotAnalyzer.autoAnalyze`
- `buildHotspotAnalyzer.trace.server`

### Commands

- `BHA: Record Build Traces`
- `BHA: Record Build Traces (Advanced)`
- `BHA: Analyze Build Performance`
- `BHA: Show Suggestions`
- `BHA: Show Activity Log`
- `BHA: Apply Suggestion`
- `BHA: Apply All Suggestions`
- `BHA: Revert Changes`
- `BHA: Restart Language Server`

Long-running commands surface progress notifications in VS Code so recording, analysis, apply, and revert operations are visible while they run.
Record, analyze, and apply operations expose a cancel button in the progress notification. Cancellation requests are forwarded to
the language server, and build output streams into the activity log during trace recording and rebuild validation.

VS Code also writes command activity, build output summaries, and server/runtime diagnostics to the
`Build Hotspot Analyzer` output channel. Open it with `BHA: Show Activity Log` or through the Output panel.

If trace recording uses a custom trace output directory, the VS Code client reuses that directory for follow-up analysis in the same workspace.
The last successful traced build profile is also persisted per workspace and reused for apply-time rebuild validation across window reloads,
as long as the cached build directory, trace directory, and explicit compiler path still validate.

Recommended workflow:
1. Record traces
2. Analyze traces
3. Review suggestions

The advanced record command keeps auto-detection as the default path, but lets users override:
- build system
- compiler
- build type
- parallel jobs
- extra build arguments
- trace output directory
- clean/verbose mode

### Metadata and branding

Current extension metadata is defined in:
- `lsp/ide-integrations/vscode/package.json`

Current publisher identity:
- publisher ID: `build-hotspot-analyzer`
- display name: `Build Hotspot Analyzer`

Current icon asset:
- `lsp/ide-integrations/vscode/media/icon.png`

### GitHub-first release flow

If Marketplace publication is blocked, ship the `.vsix` through GitHub Releases:
1. run `npm run package`
2. attach `build-hotspot-analyzer-0.1.0.vsix` to a GitHub release
3. document local install with `code --install-extension`

That is enough for users to install the extension without Marketplace publication.

## CLion

No native CLion plugin is included yet. CLion is the next integration target.
It requires an IntelliJ plugin rather than only a generic LSP launch so the BHA
workflow can expose a tool window and native actions for:
- analyze and evidence review
- suggestion details and previews
- apply and rollback
- progress and cancellation
- operation history and activity

The planned plugin should keep optimization logic in the CLI and server layers
so behavior remains consistent with the VS Code client. JetBrains documents its
generic [Language Server Protocol integration](https://plugins.jetbrains.com/docs/intellij/language-server-protocol.html),
but that connection alone is not treated as complete BHA support.

Until the native plugin exists, CLion is a planned target rather than a shipped
in-tree client.

## Generic LSP Boundary

For any temporary editor-side experiment, keep the integration thin:
1. start `bha-lsp`
2. wire `workspace/executeCommand`
3. expose analyze, preview, apply, and revert through editor-native UI

This boundary is not a supported product integration. All optimization logic
must remain in the CLI and server layers so future editor clients cannot diverge
in safety or evidence semantics.
