# Installation

## Supported Platforms

BHA is cross-platform C++20 and targets Linux/macOS/Windows toolchains.

Trace ingestion supports outputs from:
- Clang
- GCC
- MSVC
- Intel classic/oneAPI
- NVCC

## Prerequisites

Required:
- CMake `>= 3.28`
- C++20 compiler
- `nlohmann_json` (system package or auto-fetched through CMake FetchContent)

Optional but recommended:
- `clang-tidy` for include-removal verification workflows
- Git (for snapshot metadata and benchmark harnesses)
- `compile_commands.json` in analyzed project builds

## Build Profiles

### Default (CLI + library + tests)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Enable LSP module

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBHA_ENABLE_LSP=ON
cmake --build build -j
```

### Enable refactor tool (`bha-refactor`)

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBHA_BUILD_REFACTOR_TOOLS=ON
cmake --build build -j
```

### Developer profile (sanitizers + tests)

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBHA_ENABLE_SANITIZERS=ON \
  -DBHA_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Build Options

Top-level CMake options:
- `BHA_BUILD_TESTS` (default `ON`)
- `BHA_BUILD_CLI` (default `ON`)
- `BHA_BUILD_REFACTOR_TOOLS` (default `OFF`)
- `BHA_ENABLE_LSP` (default `OFF`)
- `BHA_BUILD_DOCS` (default `OFF`)
- `BHA_ENABLE_COVERAGE` (default `OFF`)
- `BHA_ENABLE_SANITIZERS` (default `OFF`)
- `BHA_BUILD_SHARED` (default `OFF`)

## Verify Binaries

After build:

```bash
./build/bha version
./build/bha --help
```

Optional binaries (if enabled):
- `./build/bha-lsp`
- `./build/bha-refactor`

## First Functional Check

From project root:

```bash
./build/bha build --clean --output traces
./build/bha analyze traces --top 10
./build/bha suggest traces --limit 5
./build/bha export traces --format md -o bha_report.md
```

## Notes for LSP Builds

Enable module:

```bash
cmake -S . -B build -DBHA_ENABLE_LSP=ON
cmake --build build -j
```

Expected executable:
- `build/lsp/bha-lsp` or `build/bha-lsp`

See `lsp_reference.md` for command payloads and configuration schema.
