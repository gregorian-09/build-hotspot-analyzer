#!/bin/bash

# Build Hotspot Analyzer - Repository Cloning Script
# Clones test repositories for BHA validation

set -o pipefail

#=============================================================================
# Configuration
#=============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_ROOT="${TEST_ROOT:-${SCRIPT_DIR}/cli}"
REPO_CACHE="${REPO_CACHE:-${TEST_ROOT}/repos}"

# Logging
LOG_FILE="${TEST_ROOT}/clone_results.log"
ERROR_LOG="${TEST_ROOT}/clone_errors.log"

#=============================================================================
# Project Definitions
#=============================================================================

# Format: "project_name|build_system|repo_url|branch"
declare -a PROJECTS=(
    # CMake projects
    "benchmark|cmake|https://github.com/google/benchmark.git|main"
    "cli11|cmake|https://github.com/CLIUtils/CLI11.git|main"
    "simdjson|cmake|https://github.com/simdjson/simdjson.git|master"
    "lz4|cmake|https://github.com/lz4/lz4.git|dev"
    "args|cmake|https://github.com/Taywee/args.git|master"
    "mimalloc|cmake|https://github.com/microsoft/mimalloc.git|main"
    "googletest|cmake|https://github.com/google/googletest.git|main"
    "rocksdb|cmake|https://github.com/facebook/rocksdb.git|main"
    "snappy|cmake|https://github.com/google/snappy.git|main"
    "fmt|cmake|https://github.com/fmtlib/fmt.git|master"
    "cxxopts|cmake|https://github.com/jarro2783/cxxopts.git|master"
    "tinyxml2|cmake|https://github.com/leethomason/tinyxml2.git|master"
    "zstd|cmake|https://github.com/facebook/zstd.git|dev"
    "abseil|cmake|https://github.com/abseil/abseil-cpp.git|master"
    "spdlog|cmake|https://github.com/gabime/spdlog.git|v1.x"
    "taskflow|cmake|https://github.com/taskflow/taskflow.git|master"
    "catch2|cmake|https://github.com/catchorg/Catch2.git|devel"
    "leveldb|cmake|https://github.com/google/leveldb.git|main"
    "yaml-cpp|cmake|https://github.com/jbeder/yaml-cpp.git|master"
    "libjpeg-turbo|cmake|https://github.com/libjpeg-turbo/libjpeg-turbo.git|main"
    "glfw|cmake|https://github.com/glfw/glfw.git|master"

    # Make projects
    "redis|make|https://github.com/redis/redis.git|unstable"
    "curl|make|https://github.com/curl/curl.git|master"
    "zlib|make|https://github.com/madler/zlib.git|master"
    "libpng|make|https://github.com/glennrp/libpng.git|libpng16"

    # Meson projects
    "weston|meson|https://gitlab.freedesktop.org/wayland/weston.git|main"
)

#=============================================================================
# Utility Functions
#=============================================================================

log() {
    local level="$1"
    shift
    local message="$*"
    local timestamp
    timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$level] [$timestamp] $message" | tee -a "$LOG_FILE"
}

log_error() {
    local message="$*"
    log "ERROR" "$message"
    echo "$message" >> "$ERROR_LOG"
}

#=============================================================================
# Repository Cloning
#=============================================================================

clone_or_update_repo() {
    local project_name="$1"
    local repo_url="$2"
    local branch="$3"

    local cache_dir="${REPO_CACHE}/${project_name}"

    if [ -d "$cache_dir/.git" ]; then
        log "INFO" "Repository '$project_name' already exists - skipping"
        return 2   # skipped
    fi

    mkdir -p "$REPO_CACHE"
    log "INFO" "Cloning $project_name from $repo_url (branch: $branch)..."

    if git clone --depth 1 --branch "$branch" "$repo_url" "$cache_dir" >>"$LOG_FILE" 2>&1; then
        log "INFO" "  ✓ Clone successful: $project_name"
        return 0   # cloned
    else
        log_error "Failed to clone $project_name"
        return 1   # failed
    fi
}

#=============================================================================
# Main
#=============================================================================

main() {
    : >"$LOG_FILE"
    : >"$ERROR_LOG"

    echo "================================================================"
    echo "BHA Repository Cloning Script"
    echo "================================================================"
    echo
    echo "This script will clone test repositories to: $REPO_CACHE"
    echo
    echo "If you already have repositories cloned, they will be skipped."
    echo "To re-clone a repository, delete its directory first."
    echo
    echo "================================================================"
    echo

    local total=0
    local cloned=0
    local skipped=0
    local failed=0

    for project_entry in "${PROJECTS[@]}"; do
        IFS='|' read -r project_name build_system repo_url branch <<<"$project_entry"

        total=$((total + 1))
        echo "[$total/${#PROJECTS[@]}] Processing: $project_name ($build_system)"

        clone_or_update_repo "$project_name" "$repo_url" "$branch"
        case $? in
            0) cloned=$((cloned + 1)) ;;
            2) skipped=$((skipped + 1)) ;;
            *) failed=$((failed + 1)) ;;
        esac

        echo
    done

    echo "================================================================"
    echo "Cloning Summary"
    echo "================================================================"
    echo "Total projects:    $total"
    echo "Newly cloned:      $cloned"
    echo "Already existing:  $skipped"
    echo "Failed:            $failed"
    echo
    echo "Repository location: $REPO_CACHE"
    echo "Log file:            $LOG_FILE"

    if [ "$failed" -gt 0 ]; then
        echo "Error log:           $ERROR_LOG"
        echo
        echo "Some repositories failed to clone. Check the error log."
        return 1
    fi

    echo
    echo "Next step: Run ./build_repos.sh to build the projects"
    echo "================================================================"
}

main "$@"
