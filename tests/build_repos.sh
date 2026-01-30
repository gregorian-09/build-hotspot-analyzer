#!/bin/bash

###############################################################################
# BHA Repository Building Script
#
# Builds cloned repositories using the BHA build command with proper compiler
# flags for time tracing and memory profiling.
#
# Requirements:
#   - BHA binary (default: ../build/bha)
#   - Cloned repositories in REPO_CACHE directory
#   - Build system tools (cmake, make, meson, ninja)
#   - Compilers (gcc, g++, clang, clang++)
#
# Output:
#   - Build artifacts: <TEST_ROOT>/<build-system>/<compiler>/<project>/build/
#   - Trace files: <TEST_ROOT>/<build-system>/<compiler>/<project>/traces/
#     * .json files for Clang -ftime-trace
#     * .bha.txt files for GCC -ftime-report
#     * .su files for memory profiling
#
###############################################################################

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_ROOT="${TEST_ROOT:-${SCRIPT_DIR}/temp}"
BHA_BINARY="${BHA_BINARY:-${SCRIPT_DIR}/../build/bha}"
REPO_CACHE="${REPO_CACHE:-${TEST_ROOT}/repos}"

COMPILERS=("clang" "gcc")

LOG_FILE="${TEST_ROOT}/build_results.log"
ERROR_LOG="${TEST_ROOT}/build_errors.log"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
NC='\033[0m'

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

declare -A PROJECT_CMAKE_SUBDIR=(
    ["lz4"]="build/cmake"
    ["zstd"]="build/cmake"
)

declare -A PROJECT_CMAKE_FLAGS=(
    ["benchmark"]="-DBENCHMARK_ENABLE_TESTING=OFF -DBENCHMARK_ENABLE_GTEST_TESTS=OFF"
    ["cli11"]="-DCLI11_BUILD_TESTS=OFF -DCLI11_BUILD_EXAMPLES=OFF"
    ["lz4"]="-DLZ4_BUILD_CLI=OFF -DLZ4_BUILD_LEGACY_LZ4C=OFF"
    ["args"]="-DARGS_BUILD_EXAMPLE=OFF -DARGS_BUILD_UNITTESTS=OFF"
    ["mimalloc"]="-DMI_BUILD_TESTS=OFF"
    ["rocksdb"]="-DWITH_TESTS=OFF -DWITH_TOOLS=OFF -DWITH_BENCHMARK_TOOLS=OFF -DFAIL_ON_WARNINGS=OFF"
    ["snappy"]="-DSNAPPY_BUILD_TESTS=OFF -DSNAPPY_BUILD_BENCHMARKS=OFF"
    ["fmt"]="-DFMT_TEST=OFF -DFMT_DOC=OFF"
    ["cxxopts"]="-DCXXOPTS_BUILD_EXAMPLES=OFF -DCXXOPTS_BUILD_TESTS=OFF"
    ["tinyxml2"]="-DBUILD_TESTING=OFF"
    ["zstd"]="-DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_CONTRIB=OFF -DZSTD_BUILD_TESTS=OFF"
    ["abseil"]="-DABSL_BUILD_TESTING=OFF -DABSL_USE_GOOGLETEST_HEAD=OFF -DABSL_BUILD_TEST_HELPERS=OFF"
    ["spdlog"]="-DSPDLOG_BUILD_TESTS=OFF -DSPDLOG_BUILD_EXAMPLE=OFF"
    ["taskflow"]="-DTF_BUILD_TESTS=OFF -DTF_BUILD_EXAMPLES=OFF"
    ["catch2"]="-DCATCH_BUILD_TESTING=OFF -DCATCH_INSTALL_DOCS=OFF"
    ["leveldb"]="-DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF -DHAVE_SNAPPY=OFF -DCMAKE_CXX_STANDARD=17"
    ["yaml-cpp"]="-DYAML_BUILD_SHARED_LIBS=OFF -DYAML_CPP_BUILD_TESTS=OFF"
    ["libjpeg-turbo"]="-DENABLE_SHARED=OFF -DWITH_TURBOJPEG=OFF"
    ["glfw"]="-DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF"
    ["googletest"]="-DBUILD_GMOCK=OFF -DINSTALL_GTEST=OFF"
)

declare -A PROJECT_MAKE_FLAGS=(
    ["redis"]="BUILD_TLS=no"
)

declare -A PROJECT_MAKE_EXTRA_ARGS=(
    ["curl"]="--disable-dependency-tracking --without-ssl --without-libssh2 --without-libpsl --disable-shared"
    ["libpng"]="--disable-dependency-tracking --disable-shared"
)

declare -A PROJECT_MESON_FLAGS=(
    ["weston"]="-Dbackend-drm=true -Dbackend-wayland=false -Dbackend-x11=false -Dbackend-headless=true -Drenderer-gl=false -Dxwayland=false -Dpipewire=false -Dremoting=false -Dshell-lua=false -Dbackend-rdp=false -Drenderer-vulkan=false -Dshell-desktop=false -Dshell-ivi=false -Dbackend-pipewire=false -Dshell-kiosk=true -Ddemo-clients=false -Dsimple-clients=[] -Dtests=false"
)

###############################################################################
# Logging Functions
###############################################################################

##
# Logs a message with timestamp and level to console and log file.
#
# Arguments:
#   $1 - Log level (INFO, WARN, ERROR)
#   $@ - Message to log
##
log() {
    local level="$1"
    shift
    local message="$*"
    # shellcheck disable=SC2155
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "[$level] [$timestamp] $message" | tee -a "$LOG_FILE"
}

##
# Logs an error message to both main log and error log.
#
# Arguments:
#   $@ - Error message to log
##
log_error() {
    local message="$*"
    log "ERROR" "$message"
    echo "$message" >> "$ERROR_LOG"
}

###############################################################################
# Build Functions
###############################################################################

##
# Builds a project using the BHA build command.
#
# Handles:
#   - Build system detection and configuration
#   - Compiler-specific flags (CMAKE, Make, Meson)
#   - Trace directory setup
#   - Skip if already built successfully
#
# Arguments:
#   $1 - Project name
#   $2 - Build system (cmake, make, meson)
#   $3 - Compiler (gcc, clang)
#   $4 - Repository directory path
#
# Returns:
#   0 on success, 1 on failure
##
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

    case "$build_system" in
        cmake)
            if [ -n "${PROJECT_CMAKE_FLAGS[$project_name]}" ]; then
                extra_args="${PROJECT_CMAKE_FLAGS[$project_name]}"
            fi
            ;;
        make)
            if [ -n "${PROJECT_MAKE_FLAGS[$project_name]}" ]; then
                extra_args="${PROJECT_MAKE_FLAGS[$project_name]}"
            fi
            if [ -n "${PROJECT_MAKE_EXTRA_ARGS[$project_name]}" ]; then
                if [ -n "$extra_args" ]; then
                    extra_args="$extra_args ${PROJECT_MAKE_EXTRA_ARGS[$project_name]}"
                else
                    extra_args="${PROJECT_MAKE_EXTRA_ARGS[$project_name]}"
                fi
            fi
            ;;
        meson)
            if [ -n "${PROJECT_MESON_FLAGS[$project_name]}" ]; then
                extra_args="${PROJECT_MESON_FLAGS[$project_name]}"
            fi
            ;;
    esac

    if [ -n "$extra_args" ]; then
        extra_args="${extra_args// /;}"
        if [ "$build_system" = "cmake" ]; then
            bha_cmd="$bha_cmd --cmake-args \"$extra_args\""
        else
            bha_cmd="$bha_cmd --configure-args \"$extra_args\""
        fi
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

###############################################################################
# Main Entry Point
###############################################################################

##
# Main function - orchestrates the build process for all projects.
#
# Workflow:
#   1. Validates BHA binary exists
#   2. Validates repository cache exists
#   3. Iterates through all projects in REPO_CACHE
#   4. Builds each project with each compiler
#   5. Generates build summary
#
# Returns:
#   0 on completion (regardless of individual build failures)
##
main() {
    # shellcheck disable=SC2188
    > "$LOG_FILE"
    # shellcheck disable=SC2188
    > "$ERROR_LOG"

    echo -e "${CYAN}================================================================${NC}"
    echo -e "${CYAN}BHA Repository Building Script${NC}"
    echo -e "${CYAN}================================================================${NC}"
    echo ""

    if [ ! -f "$BHA_BINARY" ]; then
        echo -e "${RED}Error: BHA binary not found at $BHA_BINARY${NC}"
        echo "Please build BHA first or set BHA_BINARY environment variable."
        exit 1
    fi

    echo -e "BHA Binary:     ${GREEN}$BHA_BINARY${NC}"
    echo -e "Repository dir: ${CYAN}$REPO_CACHE${NC}"
    echo -e "Output dir:     ${CYAN}$TEST_ROOT${NC}"
    echo -e "Compilers:      ${YELLOW}${COMPILERS[*]}${NC}"
    echo ""

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
            export PKG_CONFIG_PATH="$HOME/.local/share/pkgconfig:$PKG_CONFIG_PATH"
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