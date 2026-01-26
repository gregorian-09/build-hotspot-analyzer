#!/bin/bash

# Build Hotspot Analyzer - Repository Building Script
# Builds cloned repositories using the bha build command
#
# Dependencies:
#   - For curl (make): none (builds with --without-ssl --without-libpsl)
#   - For libpng (make): zlib
#   - For weston (meson): wayland-protocols >= 1.46 (Ubuntu 24.04 has 1.38, may fail)
#
# Note: Build artifacts go to build/, trace files (.json, .txt) go to traces/

set -o pipefail

#=============================================================================
# Configuration
#=============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_ROOT="${TEST_ROOT:-${SCRIPT_DIR}/cli}"
BHA_BINARY="${BHA_BINARY:-${SCRIPT_DIR}/../cmake-build-debug/bha}"
REPO_CACHE="${REPO_CACHE:-${TEST_ROOT}/repos}"

# Build parameters
COMPILERS=("clang" "gcc")
# shellcheck disable=SC2034
BUILD_SYSTEMS=("cmake" "make" "meson")

# Logging
LOG_FILE="${TEST_ROOT}/build_results.log"
ERROR_LOG="${TEST_ROOT}/build_errors.log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
NC='\033[0m'

#=============================================================================
# Project Metadata
#=============================================================================

# Map project names to their build systems
declare -A PROJECT_BUILD_SYSTEM=(
    ["benchmark"]="cmake"
    ["cli11"]="cmake"
    ["simdjson"]="cmake"
    ["lz4"]="cmake"
    ["args"]="cmake"
    ["mimalloc"]="cmake"
    ["googletest"]="cmake"
    ["rocksdb"]="cmake"
    ["snappy"]="cmake"
    ["fmt"]="cmake"
    ["cxxopts"]="cmake"
    ["tinyxml2"]="cmake"
    ["zstd"]="cmake"
    ["abseil"]="cmake"
    ["spdlog"]="cmake"
    ["taskflow"]="cmake"
    ["catch2"]="cmake"
    ["leveldb"]="cmake"
    ["yaml-cpp"]="cmake"
    ["libjpeg-turbo"]="cmake"
    ["glfw"]="cmake"
    ["redis"]="make"
    ["curl"]="make"
    ["zlib"]="make"
    ["libpng"]="make"
    ["weston"]="meson"
)

# Project-specific CMake subdirectories
declare -A PROJECT_CMAKE_SUBDIR=(
    ["lz4"]="build/cmake"
    ["zstd"]="build/cmake"
)

# Additional arguments passed to cmake
declare -A PROJECT_EXTRA_CMAKE_ARGS=(
    ["rocksdb"]="-DWITH_GFLAGS=OFF -DWITH_TESTS=OFF -DWITH_BENCHMARK_TOOLS=OFF"
    ["benchmark"]="-DBENCHMARK_ENABLE_TESTING=OFF -DBENCHMARK_ENABLE_GTEST_TESTS=OFF"
    ["leveldb"]="-DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF"
    ["snappy"]="-DSNAPPY_BUILD_TESTS=OFF -DSNAPPY_BUILD_BENCHMARKS=OFF"
    ["fmt"]="-DFMT_TEST=OFF -DFMT_DOC=OFF"
    ["cxxopts"]="-DCXXOPTS_BUILD_EXAMPLES=OFF -DCXXOPTS_BUILD_TESTS=OFF"
    ["tinyxml2"]="-DBUILD_TESTING=OFF"
    ["zstd"]="-DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_CONTRIB=OFF -DZSTD_BUILD_TESTS=OFF"
    ["spdlog"]="-DSPDLOG_BUILD_TESTS=OFF -DSPDLOG_BUILD_EXAMPLE=OFF"
    ["taskflow"]="-DTF_BUILD_TESTS=OFF -DTF_BUILD_EXAMPLES=OFF"
    ["catch2"]="-DCATCH_BUILD_TESTING=OFF -DCATCH_INSTALL_DOCS=OFF"
    ["yaml-cpp"]="-DYAML_BUILD_SHARED_LIBS=OFF -DYAML_CPP_BUILD_TESTS=OFF"
    ["libjpeg-turbo"]="-DENABLE_SHARED=OFF -DWITH_TURBOJPEG=OFF"
    ["glfw"]="-DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF"
    ["leveldb"]="-DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF -DHAVE_SNAPPY=OFF -DCMAKE_CXX_STANDARD=17"
)

#=============================================================================
# Utility Functions
#=============================================================================

log() {
    local level="$1"
    shift
    local message="$*"
    # shellcheck disable=SC2155
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "[$level] [$timestamp] $message" | tee -a "$LOG_FILE"
}

log_error() {
    local message="$*"
    log "ERROR" "$message"
    echo "$message" >> "$ERROR_LOG"
}

#=============================================================================
# Build Functions
#=============================================================================

build_project_with_bha() {
    local project_name="$1"
    local build_system="$2"
    local compiler="$3"
    local repo_dir="$4"

    local project_output_dir="${TEST_ROOT}/${build_system}/${compiler}/${project_name}"
    local build_dir="${project_output_dir}/build"
    local trace_dir="${project_output_dir}/traces"

    if [ -d "$trace_dir" ] && [ -n "$(find "$trace_dir" -type f \( -name "*.json" -o -name "*.bha.txt" -o -name "*.su" \) 2>/dev/null)" ]; then
        # shellcheck disable=SC2155
        local trace_count=$(find "$trace_dir" -type f \( -name "*.json" -o -name "*.bha.txt" -o -name "*.su" \) 2>/dev/null | wc -l)
        log "INFO" "Skipping $project_name with $compiler ($build_system) - already built ($trace_count files)"
        return 0
    fi

    mkdir -p "$build_dir"
    mkdir -p "$trace_dir"

    log "INFO" "Building $project_name with $compiler ($build_system)"
    log "INFO" "  Repo:   $repo_dir"
    log "INFO" "  Build:  $build_dir"
    log "INFO" "  Traces: $trace_dir"

    local source_dir="$repo_dir"
    if [ -n "${PROJECT_CMAKE_SUBDIR[$project_name]}" ]; then
        source_dir="$repo_dir/${PROJECT_CMAKE_SUBDIR[$project_name]}"
    fi

    cd "$source_dir" || {
        log_error "Failed to cd to $source_dir"
        return 1
    }

    local bha_cmd="\"$BHA_BINARY\" build"
    bha_cmd="$bha_cmd --build-system $build_system"
    bha_cmd="$bha_cmd --compiler $compiler"
    bha_cmd="$bha_cmd --build-dir \"$build_dir\""
    bha_cmd="$bha_cmd --output \"$trace_dir\""
    bha_cmd="$bha_cmd --memory"
    bha_cmd="$bha_cmd --clean"

    local extra_args=""
    if [ -n "${PROJECT_EXTRA_CMAKE_ARGS[$project_name]}" ]; then
        extra_args="${PROJECT_EXTRA_CMAKE_ARGS[$project_name]}"
    fi

    if [ "$build_system" = "make" ] && [ "$project_name" = "curl" ]; then
        extra_args="--without-ssl --without-libpsl"
    elif [ "$build_system" = "make" ] && [ "$project_name" = "libpng" ]; then
        extra_args="--disable-shared"
    elif [ "$build_system" = "meson" ] && [ "$project_name" = "weston" ]; then
        extra_args="-Drenderer-vulkan=false -Dbackend-pipewire=false -Dbackend-rdp=false -Dshell-lua=false -Dpipewire=false -Dremoting=false -Dbackend-default=headless -Dtests=false -Ddemo-clients=false"
    fi

    if [ -n "$extra_args" ]; then
        extra_args="${extra_args// /;}"
        bha_cmd="$bha_cmd --cmake-args \"$extra_args\""
    fi

    log "INFO" "  Working dir: $(pwd)"
    log "INFO" "  Command: $bha_cmd"

    local build_log="${project_output_dir}/bha_build.log"
    if eval "$bha_cmd" > "$build_log" 2>&1; then
        # shellcheck disable=SC2155
        local json_count=$(find "$trace_dir" -type f -name "*.json" 2>/dev/null | wc -l)
        # shellcheck disable=SC2155
        local txt_count=$(find "$trace_dir" -type f -name "*.bha.txt" 2>/dev/null | wc -l)
        # shellcheck disable=SC2155
        local su_count=$(find "$trace_dir" -type f -name "*.su" 2>/dev/null | wc -l)
        log "INFO" "  ${GREEN}✓ Build successful${NC} - Generated: $json_count .json, $txt_count .bha.txt, $su_count .su files"
        cd - > /dev/null || true
        return 0
    else
        log_error "Build failed for $project_name ($compiler)"
        log_error "  Check log: $build_log"
        tail -20 "$build_log" | while IFS= read -r line; do
            log "ERROR" "    $line"
        done
        cd - > /dev/null || true
        return 1
    fi
}

#=============================================================================
# Main
#=============================================================================

main() {
    # shellcheck disable=SC2188
    > "$LOG_FILE"
    # shellcheck disable=SC2188
    > "$ERROR_LOG"

    echo -e "${CYAN}================================================================${NC}"
    echo -e "${CYAN}BHA Repository Building Script${NC}"
    echo -e "${CYAN}================================================================${NC}"
    echo ""

    # Check if BHA binary exists
    if [ ! -f "$BHA_BINARY" ]; then
        echo -e "${RED}Error: BHA binary not found at $BHA_BINARY${NC}"
        echo "Please build BHA first or set BHA_BINARY environment variable."
        exit 1
    fi

    echo -e "BHA Binary:    ${GREEN}$BHA_BINARY${NC}"
    echo -e "Repository dir: ${CYAN}$REPO_CACHE${NC}"
    echo -e "Output dir:     ${CYAN}$TEST_ROOT${NC}"
    echo -e "Compilers:      ${YELLOW}${COMPILERS[*]}${NC}"
    echo ""

    # Check if repositories exist
    if [ ! -d "$REPO_CACHE" ] || [ -z "$(ls -A "$REPO_CACHE" 2>/dev/null)" ]; then
        echo -e "${RED}Error: No repositories found in $REPO_CACHE${NC}"
        echo "Please run ./clone_repos.sh first to clone the repositories."
        exit 1
    fi

    echo -e "${CYAN}================================================================${NC}"
    echo ""

    local total_builds=0
    local successful_builds=0
    local failed_builds=0

    # Iterate through all projects in the repo cache
    for repo_dir in "$REPO_CACHE"/*; do
        [ ! -d "$repo_dir" ] && continue

        # shellcheck disable=SC2155
        local project_name=$(basename "$repo_dir")
        local build_system="${PROJECT_BUILD_SYSTEM[$project_name]}"

        if [ -z "$build_system" ]; then
            log "WARN" "Unknown build system for $project_name - skipping"
            continue
        fi

        if [ "$project_name" = "weston" ]; then
            export PKG_CONFIG_PATH="/home/gregorian-rayne/.local/share/pkgconfig:$PKG_CONFIG_PATH"
        fi

        for compiler in "${COMPILERS[@]}"; do
            echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
            echo -e "${YELLOW}Project: ${GREEN}$project_name${NC} | Compiler: ${CYAN}$compiler${NC} | System: ${BLUE}$build_system${NC}"
            echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
            echo ""

            total_builds=$((total_builds + 1))

            if build_project_with_bha "$project_name" "$build_system" "$compiler" "$repo_dir"; then
                successful_builds=$((successful_builds + 1))
                echo -e "${GREEN}✓ Build completed successfully${NC}"
            else
                failed_builds=$((failed_builds + 1))
                echo -e "${RED}✗ Build failed${NC}"
            fi
            echo ""
        done
    done

    echo -e "${CYAN}================================================================${NC}"
    echo -e "${CYAN}Build Summary${NC}"
    echo -e "${CYAN}================================================================${NC}"
    echo "Total builds:      $total_builds"
    echo -e "Successful:        ${GREEN}$successful_builds${NC}"
    echo -e "Failed:            ${RED}$failed_builds${NC}"
    echo ""
    echo "Trace files location: ${CYAN}${TEST_ROOT}/<build-system>/<compiler>/<project>/traces/${NC}"
    echo "Log file:             ${CYAN}$LOG_FILE${NC}"

    if [ $failed_builds -gt 0 ]; then
        echo "Error log:            ${RED}$ERROR_LOG${NC}"
        echo ""
        echo -e "${YELLOW}Some builds failed. Check the error log for details.${NC}"
    fi

    echo ""
    echo "Next step: Run ./run_bha_cmds.sh to analyze the build traces"
    echo -e "${CYAN}================================================================${NC}"

    return 0
}

main "$@"
