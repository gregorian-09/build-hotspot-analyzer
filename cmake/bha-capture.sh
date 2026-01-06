#!/bin/bash
# BHA Compiler Launcher - Captures per-file timing output
# Used as CMAKE_CXX_COMPILER_LAUNCHER for automatic trace capture
#
# Usage:
#   As CMAKE_*_COMPILER_LAUNCHER: Called automatically by CMake
#   For Make projects: bha-capture.sh <compiler> [args...]
#   Environment: BHA_COMPILER=gcc/g++/clang/clang++ and call with just args
#
# Environment variables:
#   BHA_TRACE_DIR - Directory to store traces (required)
#   BHA_VERBOSE   - Set to 1 for debug output

set -o pipefail

BHA_TRACE_DIR="${BHA_TRACE_DIR:-./bha_traces}"
BHA_VERBOSE="${BHA_VERBOSE:-0}"

# Debug logging
debug_log() {
    if [ "$BHA_VERBOSE" = "1" ]; then
        echo "[bha-capture] $*" >&2
    fi
}

# Determine the actual compiler to use
ACTUAL_COMPILER=""
COMPILER_ARGS=()

# Check if first arg looks like a compiler path or name
if [[ "$1" =~ (gcc|g\+\+|clang|clang\+\+)$ ]] || [[ -x "$1" && "$1" == *"/bin/"* ]]; then
    ACTUAL_COMPILER="$1"
    shift
    COMPILER_ARGS=("$@")
    debug_log "Detected compiler from args: $ACTUAL_COMPILER"
elif [ -n "$BHA_COMPILER" ]; then
    ACTUAL_COMPILER="$BHA_COMPILER"
    COMPILER_ARGS=("$@")
    debug_log "Using compiler from BHA_COMPILER: $ACTUAL_COMPILER"
else
    # Called as compiler launcher - first arg is the actual compiler
    ACTUAL_COMPILER="$1"
    shift
    COMPILER_ARGS=("$@")
    debug_log "Compiler launcher mode: $ACTUAL_COMPILER"
fi

# Create trace directory
mkdir -p "$BHA_TRACE_DIR" || {
    debug_log "ERROR: Cannot create trace directory: $BHA_TRACE_DIR"
    exec "$ACTUAL_COMPILER" "${COMPILER_ARGS[@]}"
}

# Find the source file and output file from arguments
SOURCE_FILE=""
OUTPUT_FILE=""
NEXT_IS_OUTPUT=0

for arg in "${COMPILER_ARGS[@]}"; do
    # Check if previous arg was output marker
    if [ "$NEXT_IS_OUTPUT" = "1" ]; then
        OUTPUT_FILE="$arg"
        NEXT_IS_OUTPUT=0
    fi

    # Check for output file marker
    if [ "$arg" = "-o" ]; then
        NEXT_IS_OUTPUT=1
    fi

    # Check for source file extensions
    case "$arg" in
        *.cpp|*.cc|*.cxx|*.c|*.cu)
            SOURCE_FILE="$arg"
            ;;
    esac
done

# If no source file found, run compiler normally
if [ -z "$SOURCE_FILE" ]; then
    debug_log "No source file detected, running normally"
    exec "$ACTUAL_COMPILER" "${COMPILER_ARGS[@]}"
fi

debug_log "Capturing trace for: $SOURCE_FILE"

# Create unique trace filename using MD5 hash to avoid collisions
SOURCE_HASH=$(echo "$SOURCE_FILE" | md5sum 2>/dev/null | cut -d' ' -f1 | cut -c1-8)
if [ -z "$SOURCE_HASH" ]; then
    # Fallback for systems without md5sum (macOS, BSD)
    SOURCE_HASH=$(echo "$SOURCE_FILE" | cksum | cut -d' ' -f1)
fi

BASENAME=$(basename "$SOURCE_FILE")
BASENAME_NO_EXT="${BASENAME%.*}"
TRACE_FILE="$BHA_TRACE_DIR/${BASENAME_NO_EXT}_${SOURCE_HASH}.bha.txt"

debug_log "Trace file: $TRACE_FILE"

# Create temporary file for stderr capture
TEMP_STDERR=$(mktemp)
trap 'rm -f "$TEMP_STDERR"' EXIT

# Run compiler and capture stderr separately
"$ACTUAL_COMPILER" "${COMPILER_ARGS[@]}" 2>"$TEMP_STDERR"
EXIT_CODE=$?

# Check if stderr contains timing information
if [ -s "$TEMP_STDERR" ]; then
    # Check for timing markers (GCC/Clang)
    if grep -q "Execution times\|TOTAL\|time in" "$TEMP_STDERR"; then
        # Create trace file with metadata and timing data
        {
            echo "# BHA Trace"
            echo "# Source: $SOURCE_FILE"
            echo "# Output: $OUTPUT_FILE"
            echo "# Compiler: $ACTUAL_COMPILER"
            echo "# Command: $ACTUAL_COMPILER ${COMPILER_ARGS[*]}"
            echo "# Timestamp: $(date -Iseconds 2>/dev/null || date)"
            echo "# Exit code: $EXIT_CODE"
            echo "# ---"
            echo ""
            cat "$TEMP_STDERR"
        } > "$TRACE_FILE"

        if [ "$BHA_VERBOSE" = "1" ]; then
            TRACE_SIZE=$(du -h "$TRACE_FILE" 2>/dev/null | cut -f1)
            debug_log "Trace saved: $TRACE_SIZE"
        fi
    else
        debug_log "No timing data found in stderr"
    fi

    # Always output stderr to preserve error messages
    cat "$TEMP_STDERR" >&2
fi

exit "$EXIT_CODE"