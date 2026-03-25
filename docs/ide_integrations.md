# IDE Integrations

This guide documents how to use, package, and distribute the in-tree BHA IDE integrations as a user.

Supported clients:
- VS Code: `lsp/ide-integrations/vscode`
- Neovim: `lsp/ide-integrations/neovim`
- Emacs: `lsp/ide-integrations/emacs`

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
3. direct GitHub install for Neovim
4. direct file/package-manager install for Emacs
5. Marketplace and Open VSX later, only if needed

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

- `BHA: Analyze Build Performance`
- `BHA: Show Suggestions`
- `BHA: Apply Suggestion`
- `BHA: Apply All Suggestions`
- `BHA: Revert Changes`
- `BHA: Restart Language Server`

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

## Neovim

Client file:
- `lsp/ide-integrations/neovim/lua/bha/init.lua`

Requirements:
- `nvim-lspconfig`
- `bha-lsp` on `PATH`, or configured explicitly

The module exposes:
- `:BHAAnalyze`
- `:BHAShowSuggestions`
- `:BHAApplySuggestion`
- `:BHAApplyAll`
- `:BHARevert`

Recommended distribution:
- ship from the GitHub repository
- document install snippets for the user’s preferred plugin manager

No external marketplace account is required for basic Neovim distribution.

## Emacs

Client file:
- `lsp/ide-integrations/emacs/bha-lsp.el`

Requirements:
- `lsp-mode`
- `bha-lsp` on `PATH`, or configured explicitly

Recommended distribution:
- direct GitHub install first
- MELPA only after the package API is stable

No external publishing token is required for direct usage.

## Other IDEs

For JetBrains or other LSP-capable IDEs, keep the integration thin:
1. start `bha-lsp`
2. wire `workspace/executeCommand`
3. expose analyze, preview, apply, and revert through editor-native UI

All optimization logic should remain in the CLI and server layers so behavior stays consistent across editors.
