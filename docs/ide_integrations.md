# IDE Integrations

This guide documents how to use and distribute BHA IDE integrations.

Supported clients in-tree:
- VS Code: `lsp/ide-integrations/vscode`
- Neovim: `lsp/ide-integrations/neovim`
- Emacs: `lsp/ide-integrations/emacs`

All clients use the same server executable: `bha-lsp`.

## Prerequisites

1. Build BHA with LSP enabled:

```bash
cmake -S . -B build -DBHA_ENABLE_LSP=ON
cmake --build build -j
```

2. Ensure `bha-lsp` is available:
- add `build/lsp` (or `build/`) to `PATH`, or
- configure explicit server path in your editor client.

## VS Code: Local Usage

From `lsp/ide-integrations/vscode`:

```bash
npm ci
npm run package
code --install-extension build-hotspot-analyzer-0.1.0.vsix
```

Runtime settings are contributed under:
- `buildHotspotAnalyzer.serverPath`
- `buildHotspotAnalyzer.autoAnalyze`
- `buildHotspotAnalyzer.trace.server`

Main commands:
- `BHA: Analyze Build Performance`
- `BHA: Show Suggestions`
- `BHA: Apply Suggestion`
- `BHA: Apply All Suggestions`
- `BHA: Revert Changes`
- `BHA: Restart Language Server`

## VS Code: Distribution

### Required accounts and IDs

1. Visual Studio Marketplace publisher ID.
2. Open VSX namespace.
3. `package.json` `publisher` must match both.

### Required secrets for CI

- `VSCE_PAT`: Azure DevOps PAT with Marketplace manage scope.
- `OVSX_PAT`: Open VSX access token.

### Publish commands

From `lsp/ide-integrations/vscode`:

```bash
npx vsce publish -p "$VSCE_PAT"
npx ovsx publish -p "$OVSX_PAT"
```

Optional:

```bash
npx vsce package
npx ovsx publish build-hotspot-analyzer-0.1.0.vsix -p "$OVSX_PAT"
```

## Neovim Usage

Client file: `lsp/ide-integrations/neovim/lua/bha/init.lua`

Requirements:
- `nvim-lspconfig`
- `bha-lsp` in `PATH` (or pass custom `cmd`)

The module registers commands:
- `:BHAAnalyze`
- `:BHAShowSuggestions`
- `:BHAApplySuggestion`
- `:BHAApplyAll`
- `:BHARevert`

## Emacs Usage

Client file: `lsp/ide-integrations/emacs/bha-lsp.el`

Requirements:
- `lsp-mode`
- `bha-lsp` available via configurable path

Commands are provided for analyze/show/apply/apply-all/revert.

## Other IDEs

For JetBrains or other LSP-capable IDEs, integration should remain thin:
1. start `bha-lsp`
2. map `workspace/executeCommand` for BHA commands
3. surface preview/apply/revert UX in editor-native actions

Keep all optimization logic in server/CLI to preserve behavior consistency.
