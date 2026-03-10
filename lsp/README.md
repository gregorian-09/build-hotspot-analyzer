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
