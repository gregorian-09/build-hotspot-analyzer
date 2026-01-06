#!/bin/bash

# Build Hotspot Analyzer - Automated Testing Script
# Tests BHA across multiple C++ projects with different build systems and compilers

set -o pipefail

#=============================================================================
# Configuration
#=============================================================================

# Script directory and paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_ROOT="${SCRIPT_ROOT:-${SCRIPT_DIR}/cli}"
BHA_BINARY="${BHA_BINARY:-${SCRIPT_DIR}/../cmake-build-debug/bha}"
BHA_CMAKE_DIR="${SCRIPT_DIR}/../cmake"
REPO_CACHE="${REPO_CACHE:-${TEST_ROOT}/repos}"

# Test parameters
COMPILERS=("clang" "gcc")
SKIP_EXISTING=true
VERBOSE_CAPTURE=0

# Logging
LOG_FILE="${TEST_ROOT}/test_results.log"
ERROR_LOG="${TEST_ROOT}/test_errors.log"
SUMMARY_FILE="${TEST_ROOT}/summary.txt"

#=============================================================================
# Project Definitions
#=============================================================================

# Array of projects to test
# Format: "project_name:build_system:repo_url:branch"
declare -a PROJECTS=(
    # CMake projects
    "benchmark:cmake:https://github.com/google/benchmark.git:main"
    "cli11:cmake:https://github.com/CLIUtils/CLI11.git:main"
    "simdjson:cmake:https://github.com/simdjson/simdjson.git:master"
    "lz4:cmake:https://github.com/lz4/lz4.git:dev"
    "args:cmake:https://github.com/Taywee/args.git:master"
    "mimalloc:cmake:https://github.com/microsoft/mimalloc.git:master"
    "googletest:cmake:https://github.com/google/googletest.git:main"
    "rocksdb:cmake:https://github.com/facebook/rocksdb.git:main"
    "snappy:cmake:https://github.com/google/snappy.git:main"
    "fmt:cmake:https://github.com/fmtlib/fmt.git:master"
    "cxxopts:cmake:https://github.com/jarro2783/cxxopts.git:master"
    "tinyxml2:cmake:https://github.com/leethomason/tinyxml2.git:master"
    "zstd:cmake:https://github.com/facebook/zstd.git:dev"
    "abseil:cmake:https://github.com/abseil/abseil-cpp.git:master"
    "spdlog:cmake:https://github.com/gabime/spdlog.git:v1.x"
    "taskflow:cmake:https://github.com/taskflow/taskflow.git:master"
    "catch2:cmake:https://github.com/catchorg/Catch2.git:devel"
    "leveldb:cmake:https://github.com/google/leveldb.git:main"
    "yaml-cpp:cmake:https://github.com/jbeder/yaml-cpp.git:master"
    "libjpeg-turbo:cmake:https://github.com/libjpeg-turbo/libjpeg-turbo.git:main"
    "glfw:cmake:https://github.com/glfw/glfw.git:master"

    # Make projects
    "redis:make:https://github.com/redis/redis.git:unstable"
    "curl:make:https://github.com/curl/curl.git:master"
    "zlib:make:https://github.com/madler/zlib.git:master"
    "libpng:make:https://github.com/glennrp/libpng.git:libpng16"

    # Meson projects
    "weston:meson:https://gitlab.freedesktop.org/wayland/weston.git:main"
)

# Project-specific CMake flags
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
)

# Project-specific CMake subdirectories
declare -A PROJECT_CMAKE_SUBDIR=(
    ["lz4"]="build/cmake"
    ["zstd"]="build/cmake"
)

# Project-specific dependencies
declare -A PROJECT_DEPENDENCIES=(
    ["weston"]="libwayland-dev libwayland-egl-backend-dev libdrm-dev libgbm-dev \
    libinput-dev libudev-dev libpixman-1-dev libcairo2-dev libpango1.0-dev \
    libxkbcommon-dev libxcb-composite0-dev liblcms2-dev libdbus-1-dev \
    libsystemd-dev libevdev-dev libpam0g-dev libxml2-dev"
    ["curl"]="autoconf automake libtool pkg-config"
    ["libpng"]="zlib1g-dev"
    ["leveldb"]="libsnappy-dev"
)

# Project-specific Make flags
declare -A PROJECT_MAKE_FLAGS=(
    ["redis"]="BUILD_TLS=no"
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
    echo "[$level] [$timestamp] $message" | tee -a "$LOG_FILE"
}

log_detail() {
    local message="$*"
    echo "[DETAIL] $message" | tee -a "$LOG_FILE"
}

log_error() {
    local message="$*"
    log "ERROR" "$message"
    echo "$message" >> "$ERROR_LOG"
}

log_warn() {
    log "WARN" "$*"
}

#=============================================================================
# System Prerequisites
#=============================================================================

check_prerequisites() {
    echo ""
    echo "========================================"
    echo "Checking System Prerequisites"
    echo "========================================"
    echo ""

    # Detect OS and package manager
    local os_type=""
    local pkg_manager=""

    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        os_type="linux"
        if command -v apt-get &> /dev/null; then
            pkg_manager="apt-get"
        elif command -v yum &> /dev/null; then
            pkg_manager="yum"
        elif command -v dnf &> /dev/null; then
            pkg_manager="dnf"
        elif command -v pacman &> /dev/null; then
            pkg_manager="pacman"
        fi
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        os_type="macos"
        pkg_manager="brew"
    fi

    log "INFO" "Detected OS: $os_type with package manager: $pkg_manager"

    # Check required tools
    local required_tools=("git" "cmake" "ninja" "gcc" "g++" "clang" "clang++" "python3" "make" "pkg-config" "autoconf" "automake" "libtool" "meson")
    local missing_tools=()

    for tool in "${required_tools[@]}"; do
        if command -v "$tool" &> /dev/null; then
            # shellcheck disable=SC2155
            # shellcheck disable=SC2155
            local version=$(command "$tool" --version 2>&1 | head -1)
            log "INFO" "$tool found: $(which "$tool") ($version)"
        else
            missing_tools+=("$tool")
        fi
    done

    # Install missing tools if possible
    if [ ${#missing_tools[@]} -gt 0 ]; then
        log "WARN" "Missing tools: ${missing_tools[*]}"

        if [ -n "$pkg_manager" ]; then
            log "INFO" "Attempting to install missing packages..."
            case "$pkg_manager" in
                apt-get)
                    sudo apt-get update
                    sudo apt-get install -y git cmake ninja-build gcc g++ clang python3 make pkg-config autoconf automake libtool python3-pip
                    pip3 install --user meson
                    ;;
                yum|dnf)
                    sudo $pkg_manager install -y git cmake ninja-build gcc gcc-c++ clang python3 make pkgconfig autoconf automake libtool python3-pip
                    pip3 install --user meson
                    ;;
                pacman)
                    sudo pacman -S --needed git cmake ninja gcc clang python make pkgconf autoconf automake libtool meson
                    ;;
                brew)
                    brew install git cmake ninja gcc llvm python3 make pkg-config autoconf automake libtool meson
                    ;;
            esac
        fi

        # Re-check
        for tool in "${missing_tools[@]}"; do
            if ! command -v "$tool" &> /dev/null; then
                log_error "Required tool not found: $tool"
                return 1
            fi
        done
    fi

    log "INFO" "All required packages are already installed"

    # Install common development libraries
    if [ "$pkg_manager" = "apt-get" ]; then
        log "INFO" "Installing common development libraries..."
        sudo apt-get install -y \
            libboost-all-dev libssl-dev libcurl4-openssl-dev zlib1g-dev \
            libpng-dev libjpeg-dev \
            libwayland-dev libwayland-egl-backend-dev wayland-protocols \
            libdrm-dev libgbm-dev libinput-dev libudev-dev libsystemd-dev \
            libdbus-1-dev libglib2.0-dev libcairo2-dev libpango1.0-dev \
            libgtk-3-dev libx11-dev libxcb1-dev libxkbcommon-dev \
            libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev libvulkan-dev \
            libfmt-dev libjemalloc-dev liblz4-dev libzstd-dev libsnappy-dev \
            libtbb-dev libbenchmark-dev
        log "INFO" "Development libraries installation complete"
    fi

    # Check BHA binary
    if [ ! -f "$BHA_BINARY" ]; then
        log_error "BHA binary not found at: $BHA_BINARY"
        return 1
    fi
    log "INFO" "BHA binary found at: $BHA_BINARY"
    log_detail "BHA version: $($BHA_BINARY --version)"

    # Check BHA CMake helpers
    if [ ! -d "$BHA_CMAKE_DIR" ]; then
        log_error "BHA CMake helpers not found at: $BHA_CMAKE_DIR"
        return 1
    fi
    log "INFO" "BHA CMake helpers found at: $BHA_CMAKE_DIR"

    # Make capture script executable
    if [ -f "${BHA_CMAKE_DIR}/bha-capture.sh" ]; then
        chmod +x "${BHA_CMAKE_DIR}/bha-capture.sh"
        log "INFO" "BHA capture script is executable"
    else
        log_error "BHA capture script not found at: ${BHA_CMAKE_DIR}/bha-capture.sh"
        return 1
    fi

    echo ""
    echo "========================================"
    echo "Prerequisites Check Complete"
    echo "========================================"
    echo ""

    return 0
}

#=============================================================================
# Repository Management
#=============================================================================

clone_or_update_repo() {
    local project_name="$1"
    local repo_url="$2"
    local branch="$3"
    local target_dir="$4"

    # Use cached repo if available
    local cache_dir="${REPO_CACHE}/${project_name}"

    if [ -d "$cache_dir/.git" ]; then
        log "INFO" "Repository already exists at target: $project_name ($target_dir)"

        # Ensure parent directory exists before copying
        mkdir -p "$(dirname "$target_dir")"

        # Copy from cache to target if not already present
        if [ ! -d "$target_dir" ]; then
            cp -r "$cache_dir" "$target_dir"
        fi

        log_detail "  Using cached repository copy"
        return 0
    fi

    # Clone to cache first
    mkdir -p "$REPO_CACHE"
    log "INFO" "Cloning $project_name from $repo_url (branch: $branch)..."

    if git clone --depth 1 --branch "$branch" "$repo_url" "$cache_dir" >> "$LOG_FILE" 2>&1; then
        # Ensure parent directory exists before copying
        mkdir -p "$(dirname "$target_dir")"

        # Copy to target
        cp -r "$cache_dir" "$target_dir"
        log_detail "  Clone successful"
        return 0
    else
        log_error "Failed to clone $project_name"
        return 1
    fi
}

#=============================================================================
# Build Functions
#=============================================================================

build_with_cmake() {
    local project_name="$1"
    local project_dir="$2"
    local build_dir="$3"
    local compiler="$4"
    local trace_dir="$5"

    # Add project-specific flags
    local extra_flags="${PROJECT_CMAKE_FLAGS[$project_name]:-}"
    if [ -n "$extra_flags" ]; then
        log_detail "  Adding project-specific flags: $extra_flags"
    fi

    # Check for subdirectory
    local cmake_source_dir="$project_dir"
    if [ -n "${PROJECT_CMAKE_SUBDIR[$project_name]}" ]; then
        cmake_source_dir="$project_dir/${PROJECT_CMAKE_SUBDIR[$project_name]}"
    fi

    # Verify CMakeLists.txt exists
    if [ ! -f "$cmake_source_dir/CMakeLists.txt" ]; then
        log_error "No CMakeLists.txt found in $cmake_source_dir"
        return 1
    fi

    mkdir -p "$build_dir"
    mkdir -p "$trace_dir"

    # Set compiler flags for timing
    local cmake_flags="-GNinja"
    cmake_flags="$cmake_flags -DCMAKE_MODULE_PATH=${BHA_CMAKE_DIR}"

    if [ "$compiler" = "gcc" ]; then
        # GCC with trace capture via launcher
        export CC=gcc
        export CXX=g++
        cmake_flags="$cmake_flags -DCMAKE_C_COMPILER=gcc"
        cmake_flags="$cmake_flags -DCMAKE_CXX_COMPILER=g++"
        cmake_flags="$cmake_flags -DCMAKE_CXX_FLAGS=-ftime-report"
        cmake_flags="$cmake_flags -DCMAKE_C_FLAGS=-ftime-report"
        cmake_flags="$cmake_flags -DCMAKE_CXX_COMPILER_LAUNCHER=${BHA_CMAKE_DIR}/bha-capture.sh"
        cmake_flags="$cmake_flags -DCMAKE_C_COMPILER_LAUNCHER=${BHA_CMAKE_DIR}/bha-capture.sh"

        export BHA_TRACE_DIR="$trace_dir"
        export BHA_VERBOSE="$VERBOSE_CAPTURE"

        log_detail "  GCC trace capture enabled"
        log_detail "  Compiler launcher: ${BHA_CMAKE_DIR}/bha-capture.sh"
        log_detail "  Trace directory: $trace_dir"

        # Verify launcher is executable
        if [ ! -x "${BHA_CMAKE_DIR}/bha-capture.sh" ]; then
            chmod +x "${BHA_CMAKE_DIR}/bha-capture.sh"
        fi
    else
        # Clang with native -ftime-trace
        export CC=clang
        export CXX=clang++
        cmake_flags="$cmake_flags -DCMAKE_C_COMPILER=clang"
        cmake_flags="$cmake_flags -DCMAKE_CXX_COMPILER=clang++"
        cmake_flags="$cmake_flags -DCMAKE_CXX_FLAGS=-ftime-trace"
        cmake_flags="$cmake_flags -DCMAKE_C_FLAGS=-ftime-trace"
    fi

    # Common flags
    cmake_flags="$cmake_flags -DCMAKE_BUILD_TYPE=Release"
    cmake_flags="$cmake_flags -DBUILD_TESTING=OFF"
    cmake_flags="$cmake_flags -DBUILD_SHARED_LIBS=OFF"

    # Add project-specific flags
    if [ -n "$extra_flags" ]; then
        cmake_flags="$cmake_flags $extra_flags"
    fi

    # Configure
    log "INFO" "Configuring with CMake ($compiler)..."
    log_detail "  Source dir: $cmake_source_dir"
    log_detail "  Build dir: $build_dir"
    log_detail "  Trace dir: $trace_dir"
    log_detail "  Command: cmake $cmake_flags $cmake_source_dir"

    if ! cmake "$cmake_flags" "$cmake_source_dir" -B "$build_dir" > "${build_dir}/cmake_configure.log" 2>&1; then
        log_error "Configuration failed - see ${build_dir}/cmake_configure.log"
        tail -20 "${build_dir}/cmake_configure.log" | while IFS= read -r line; do
            log_detail "  $line"
        done
        return 1
    fi

    log_detail "  Configuration successful"
    log_detail "  Config log: ${build_dir}/cmake_configure.log"

    # Build
    log "INFO" "Building project..."
    if ! cmake --build "$build_dir" > "${build_dir}/ninja_build.log" 2>&1; then
        log_error "Build failed - see ${build_dir}/ninja_build.log"
        tail -30 "${build_dir}/ninja_build.log" | while IFS= read -r line; do
            log_detail "  $line"
        done
        return 1
    fi

    log_detail "  Build successful"
    log_detail "  Build log: ${build_dir}/ninja_build.log"

    # Collect traces
    if [ "$compiler" = "clang" ]; then
        # Clang: collect .json files from build directory
        find "$build_dir" -name "*.json" -type f -exec cp {} "$trace_dir/" \; 2>/dev/null
        # shellcheck disable=SC2155
        # shellcheck disable=SC2155
        local json_count=$(find "$trace_dir" -name "*.json" -type f | wc -l)
        log_detail "  Collected $json_count JSON trace files"
    else
        # GCC: traces should already be in trace_dir from launcher
        # shellcheck disable=SC2155
        # shellcheck disable=SC2155
        local txt_count=$(find "$trace_dir" -name "*.bha.txt" -type f 2>/dev/null | wc -l)
        log_detail "  Captured $txt_count GCC trace files (.bha.txt)"

        if [ "$txt_count" -eq 0 ]; then
            log_warn "No GCC traces captured"
            log_detail "  Checking build for compilation activity..."
            # shellcheck disable=SC2155
            # shellcheck disable=SC2155
            local obj_count=$(find "$build_dir" -name "*.o" -type f 2>/dev/null | wc -l)
            log_detail "  Object files created: $obj_count"
            if [ "$obj_count" -eq 0 ]; then
                log_detail "  No object files found - likely a header-only or interface library"
            fi
        fi
    fi

    return 0
}

build_with_make() {
    local project_name="$1"
    local project_dir="$2"
    local build_dir="$3"
    local compiler="$4"
    local trace_dir="$5"

    mkdir -p "$trace_dir"
    mkdir -p "$build_dir"

    # Work in a copy to avoid polluting source
    cp -r "$project_dir"/* "$build_dir/" 2>/dev/null || true
    # shellcheck disable=SC2164
    cd "$build_dir"

    # Set up compiler and flags
    local make_flags="${PROJECT_MAKE_FLAGS[$project_name]:-}"

    if [ "$compiler" = "gcc" ]; then
        # For GCC with Make, wrap the compiler using the launcher
        export BHA_TRACE_DIR="$trace_dir"
        export BHA_VERBOSE="$VERBOSE_CAPTURE"
        export CC="${BHA_CMAKE_DIR}/bha-capture.sh gcc"
        export CXX="${BHA_CMAKE_DIR}/bha-capture.sh g++"
        make_flags="$make_flags CFLAGS=-ftime-report CXXFLAGS=-ftime-report"
        log_detail "  GCC with wrapper: CC=$CC CXX=$CXX"
    elif [ "$compiler" = "clang" ]; then
        export CC=clang
        export CXX=clang++
        make_flags="$make_flags CC=clang CXX=clang++ CFLAGS=-ftime-trace CXXFLAGS=-ftime-trace"
    fi

    log "INFO" "Building with Make ($compiler)..."
    log_detail "  Source dir: $project_dir"
    log_detail "  Build dir: $build_dir"
    log_detail "  Trace dir: $trace_dir"
    log_detail "  Make flags: $make_flags"

    # Check for configure script
    local configure_flags=()
    if [ -f "configure" ]; then
        log_detail "  Running configure script..."

        # Project-specific configure flags
        case "$project_name" in
            curl)
                configure_flags=(
                    "--disable-dependency-tracking"
                    "--without-ssl"
                    "--without-libssh2"
                    "--without-libpsl"
                    "--disable-shared"
                )
                ;;
            libpng)
                configure_flags=(
                    "--disable-dependency-tracking"
                    "--disable-shared"
                )
                ;;
            *)
                configure_flags=(
                    "--disable-dependency-tracking"
                )
                ;;
        esac

        if ! ./configure "${configure_flags[@]}" CC="$CC" CXX="$CXX" > configure.log 2>&1; then
            log_error "Configure failed"
            tail -20 configure.log | while IFS= read -r line; do
                log_detail "  $line"
            done
            return 1
        fi
        log_detail "  Configure successful"
    elif [ -f "autogen.sh" ]; then
        log_detail "  Running autogen.sh..."
        if ./autogen.sh > autogen.log 2>&1; then
            log_detail "  autogen.sh completed"
            if [ -f "configure" ]; then
                log_detail "  Running configure script..."
                if ! ./configure "${configure_flags[@]}" CC="$CC" CXX="$CXX" > configure.log 2>&1; then
                    log_error "Configure failed"
                    return 1
                fi
                log_detail "  Configure successful"
            fi
        else
            log_warn "autogen.sh failed, trying alternatives..."
        fi
    else
        log_detail "  No configure script needed"
    fi

    # Build
    # shellcheck disable=SC2155
    # shellcheck disable=SC2155
    local nproc_val=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    if ! eval make "$make_flags" -j"$nproc_val" > make_build.log 2>&1; then
        log_error "Build failed - see ${build_dir}/make_build.log"
        tail -30 "${build_dir}/make_build.log" | while IFS= read -r line; do
            log_detail "  $line"
        done
        return 1
    fi

    log_detail "  Build successful"

    # Collect traces
    if [ "$compiler" = "clang" ]; then
        find . -name "*.json" -type f -exec cp {} "$trace_dir/" \; 2>/dev/null
        # shellcheck disable=SC2155
        # shellcheck disable=SC2155
        local json_count=$(find "$trace_dir" -name "*.json" -type f | wc -l)
        log_detail "  Collected $json_count JSON trace files"
    else
        # shellcheck disable=SC2155
        local txt_count=$(find "$trace_dir" -name "*.bha.txt" -type f 2>/dev/null | wc -l)
        log_detail "  Captured $txt_count GCC trace files (.bha.txt)"
    fi

    return 0
}

build_with_meson() {
    local project_name="$1"
    local project_dir="$2"
    local build_dir="$3"
    local compiler="$4"
    local trace_dir="$5"

    mkdir -p "$trace_dir"

    # Meson flags
    local meson_flags=()

    # Project-specific meson flags
    case "$project_name" in
        weston)
            meson_flags=(
                "-Dbackend-drm=true"
                "-Dbackend-wayland=false"
                "-Dbackend-x11=false"
                "-Dbackend-headless=true"
                "-Drenderer-gl=false"
                "-Dxwayland=false"
                "-Dpipewire=false"
                "-Dremoting=false"
                "-Dshell-lua=false"
                "-Dbackend-rdp=false"
                "-Drenderer-vulkan=false"
                "-Dshell-desktop=false"
                "-Dshell-ivi=false"
                "-Dbackend-pipewire=false"
                "-Dshell-kiosk=true"
                "-Ddemo-clients=false"
                "-Dsimple-clients=[]"
                "-Dtests=false"
            )
            ;;
    esac

    # Add compiler timing flags
    if [ "$compiler" = "gcc" ]; then
        meson_flags+=("-Dc_args=-ftime-report" "-Dcpp_args=-ftime-report")
    else
        meson_flags+=("-Dc_args=-ftime-trace" "-Dcpp_args=-ftime-trace")
    fi

    log "INFO" "Configuring with Meson ($compiler)..."
    log_detail "  Source dir: $project_dir"
    log_detail "  Build dir: $build_dir"
    log_detail "  Trace dir: $trace_dir"
    log_detail "  Meson flags: ${meson_flags[*]}"

    # Configure - FIX THE COMPILER NAMES
    if [ "$compiler" = "gcc" ]; then
        export CC=gcc
        export CXX=g++
    else
        export CC=clang
        export CXX=clang++
    fi

    if ! meson setup "$build_dir" "$project_dir" "${meson_flags[@]}" > "${build_dir}_meson_config.log" 2>&1; then
        log_error "Configuration failed - see ${build_dir}_meson_config.log"
        tail -20 "${build_dir}_meson_config.log" | while IFS= read -r line; do
            log_detail "  $line"
        done
        return 1
    fi

    log_detail "  Configuration successful"

    # Build
    log "INFO" "Building with Meson..."
    if ! meson compile -C "$build_dir" > "${build_dir}_meson_build.log" 2>&1; then
        log_error "Build failed - see ${build_dir}_meson_build.log"
        tail -30 "${build_dir}_meson_build.log" | while IFS= read -r line; do
            log_detail "  $line"
        done
        return 1
    fi

    log_detail "  Build successful"

    # Collect traces
    if [ "$compiler" = "clang" ]; then
        find "$build_dir" -name "*.json" -type f -exec cp {} "$trace_dir/" \; 2>/dev/null
        # shellcheck disable=SC2155
        local json_count=$(find "$trace_dir" -name "*.json" -type f | wc -l)
        log_detail "  Collected $json_count JSON trace files"
    fi

    return 0
}

#=============================================================================
# BHA Analysis
#=============================================================================

run_bha_analysis() {
    local trace_dir="$1"
    local output_dir="$2"

    # Count trace files
    local trace_count=0
    if [ -d "$trace_dir" ]; then
        trace_count=$(find "$trace_dir" \( -name "*.json" -o -name "*.bha.txt" \) -type f 2>/dev/null | wc -l)
    fi

    if [ "$trace_count" -eq 0 ]; then
        log_warn "No trace files found for analysis"
        return 1
    fi

    log "INFO" "Found $trace_count trace files for analysis"
    log_detail "  Trace directory: $trace_dir"

    # List first 10 trace files with sizes
    log_detail "  Trace files in $trace_dir:"
    # shellcheck disable=SC2155
    find "$trace_dir" \( -name "*.json" -o -name "*.bha.txt" \) -type f -exec ls -lh {} \; 2>/dev/null | head -10 | while read -r line; do
        local size=$(echo "$line" | awk '{print $5}')
        local file=$(basename "$(echo "$line" | awk '{print $9}')")
        log_detail "    - $file ($size)"
    done

    # Create output directories
    mkdir -p "$output_dir"/{analyze,suggest,export}

    # Run analyze commands
    log "INFO" "Running bha analyze..."

    "$BHA_BINARY" analyze --verbose --top 0 --list-files "$trace_dir" \
        > "$output_dir/analyze/analyze_verbose.txt" 2>&1
    log_detail "  Output: $output_dir/analyze/analyze_verbose.txt ($(wc -l < "$output_dir/analyze/analyze_verbose.txt") lines)"

    "$BHA_BINARY" analyze --include-templates --list-templates "$trace_dir" \
        > "$output_dir/analyze/analyze_templates.txt" 2>&1
    log_detail "  Output: $output_dir/analyze/analyze_templates.txt ($(wc -l < "$output_dir/analyze/analyze_templates.txt") lines)"

    "$BHA_BINARY" analyze --include-includes --list-headers "$trace_dir" \
        > "$output_dir/analyze/analyze_headers.txt" 2>&1
    log_detail "  Output: $output_dir/analyze/analyze_headers.txt ($(wc -l < "$output_dir/analyze/analyze_headers.txt") lines)"

    "$BHA_BINARY" analyze --format json "$trace_dir" \
        > "$output_dir/analyze/analyze_full.json" 2>&1
    log_detail "  Output: $output_dir/analyze/analyze_full.json ($(du -h "$output_dir/analyze/analyze_full.json" | cut -f1))"

    # Run suggest commands
    log "INFO" "Running bha suggest..."

    "$BHA_BINARY" suggest --detailed --limit 50 --min-priority low "$trace_dir" \
        > "$output_dir/suggest/suggest_all.txt" 2>&1
    log_detail "  Output: $output_dir/suggest/suggest_all.txt ($(wc -l < "$output_dir/suggest/suggest_all.txt") lines)"

    "$BHA_BINARY" suggest --min-priority high "$trace_dir" \
        > "$output_dir/suggest/suggest_high_priority.txt" 2>&1
    log_detail "  Output: $output_dir/suggest/suggest_high_priority.txt ($(wc -l < "$output_dir/suggest/suggest_high_priority.txt") lines)"

    "$BHA_BINARY" suggest --include-unsafe "$trace_dir" \
        > "$output_dir/suggest/suggest_with_unsafe.txt" 2>&1
    log_detail "  Output: $output_dir/suggest/suggest_with_unsafe.txt ($(wc -l < "$output_dir/suggest/suggest_with_unsafe.txt") lines)"

    "$BHA_BINARY" suggest --format json "$trace_dir" \
        > "$output_dir/suggest/suggest_full.json" 2>&1
    log_detail "  Output: $output_dir/suggest/suggest_full.json ($(du -h "$output_dir/suggest/suggest_full.json" | cut -f1))"

    # Run export commands
    log "INFO" "Running bha export..."

    "$BHA_BINARY" export --format json --pretty --include-suggestions "$trace_dir" \
        > "$output_dir/export/report.json" 2> "$output_dir/export/export_json.log"
    log_detail "  Output: $output_dir/export/report.json ($(du -h "$output_dir/export/report.json" | cut -f1))"

    "$BHA_BINARY" export --format html --include-suggestions "$trace_dir" \
        > "$output_dir/export/report.html" 2> "$output_dir/export/export_html.log"
    log_detail "  Output: $output_dir/export/report.html ($(du -h "$output_dir/export/report.html" | cut -f1))"

    "$BHA_BINARY" export --format html --dark-mode "$trace_dir" \
        > "$output_dir/export/report_dark.html" 2> "$output_dir/export/export_html_dark.log"
    log_detail "  Output: $output_dir/export/report_dark.html ($(du -h "$output_dir/export/report_dark.html" | cut -f1))"

    "$BHA_BINARY" export --format csv "$trace_dir" \
        > "$output_dir/export/data.csv" 2> "$output_dir/export/export_csv.log"
    # shellcheck disable=SC2155
    local csv_rows=$(wc -l < "$output_dir/export/data.csv")
    log_detail "  Output: $output_dir/export/data.csv ($csv_rows rows)"

    "$BHA_BINARY" export --format md --include-suggestions "$trace_dir" \
        > "$output_dir/export/report.md" 2> "$output_dir/export/export_md.log"
    log_detail "  Output: $output_dir/export/report.md ($(wc -l < "$output_dir/export/report.md") lines)"

    # Summary of generated files
    log_detail "  Generated files summary:"
    find "$output_dir" -type f -exec ls -lh {} \; | while read -r line; do
        # shellcheck disable=SC2155
        local size=$(echo "$line" | awk '{print $5}')
        # shellcheck disable=SC2155
        local file=$(basename "$(echo "$line" | awk '{print $9}')")
        log_detail "    $file: $size"
    done

    return 0
}

#=============================================================================
# Project Build Orchestration
#=============================================================================

install_dependencies() {
    local project_name="$1"
    local deps="${PROJECT_DEPENDENCIES[$project_name]}"

    if [ -z "$deps" ]; then
        log_detail "  Dependencies installed for $project_name"
        return 0
    fi

    log "INFO" "Installing dependencies for $project_name..."

    if command -v apt-get &> /dev/null; then
        sudo apt-get install -y "$deps"
        log_detail "  Dependencies installed for $project_name"
    elif command -v brew &> /dev/null; then
        brew install "$deps"
        log_detail "  Dependencies installed for $project_name"
    else
        log_warn "No package manager found, skipping dependency installation"
    fi
}

build_project() {
    local project_name="$1"
    local build_system="$2"
    local repo_url="$3"
    local branch="$4"
    local compiler="$5"

    log "INFO" "Building $project_name with $compiler..."

    # Set up directories
    local project_root="${TEST_ROOT}/${build_system}/${compiler}/${project_name}"
    local repo_dir="${project_root}/repo"
    local build_dir="${project_root}/build"
    local trace_dir="${project_root}/traces"
    local output_dir="${project_root}/output"

    # Clone or update repository
    if ! clone_or_update_repo "$project_name" "$repo_url" "$branch" "$repo_dir"; then
        return 1
    fi

    # Install dependencies
    install_dependencies "$project_name"

    # Build based on build system
    case "$build_system" in
        cmake)
            if ! build_with_cmake "$project_name" "$repo_dir" "$build_dir" "$compiler" "$trace_dir"; then
                return 1
            fi
            ;;
        make)
            if ! build_with_make "$project_name" "$repo_dir" "$build_dir" "$compiler" "$trace_dir"; then
                return 1
            fi
            ;;
        meson)
            if ! build_with_meson "$project_name" "$repo_dir" "$build_dir" "$compiler" "$trace_dir"; then
                return 1
            fi
            ;;
        *)
            log_error "Unknown build system: $build_system"
            return 1
            ;;
    esac

    # Check for trace files
    local trace_count=0
    if [ -d "$trace_dir" ]; then
        trace_count=$(find "$trace_dir" \( -name "*.json" -o -name "*.bha.txt" \) -type f 2>/dev/null | wc -l)
    fi

    if [ "$trace_count" -eq 0 ]; then
        log_warn "Build succeeded but no trace files generated for $project_name with $compiler"
        return 2  # Special return code for no traces
    fi

    # Run BHA analysis
    if ! run_bha_analysis "$trace_dir" "$output_dir"; then
        log_warn "BHA analysis failed or incomplete"
        return 2
    fi

    log "INFO" "Successfully processed $project_name with $compiler"
    return 0
}

check_existing_build() {
    local project_name="$1"
    local build_system="$2"
    local compiler="$3"

    local project_root="${TEST_ROOT}/${build_system}/${compiler}/${project_name}"
    local trace_dir="${project_root}/traces"
    local output_dir="${project_root}/output"

    # Check if build already successful
    if [ -d "$trace_dir" ] && [ -d "$output_dir" ]; then
        # shellcheck disable=SC2155
        local trace_count=$(find "$trace_dir" \( -name "*.json" -o -name "*.bha.txt" \) -type f 2>/dev/null | wc -l)
        # shellcheck disable=SC2155
        local output_count=$(find "$output_dir" -type f 2>/dev/null | wc -l)

        if [ "$trace_count" -gt 0 ] && [ "$output_count" -gt 0 ]; then
            log "INFO" "Build already successful for $project_name with $compiler - SKIPPING"
            log_detail "  Trace files exist: $trace_count"
            log_detail "  Output directory populated"
            return 0
        fi
    fi

    return 1
}

#=============================================================================
# Main Test Loop
#=============================================================================

run_tests() {
    local total_attempts=0
    local successful_builds=0
    local failed_builds=0
    local skipped_builds=0
    local no_trace_builds=0

    declare -A results

    echo ""
    echo "========================================"
    echo "Processing Projects"
    echo "========================================"
    echo ""

    for project_info in "${PROJECTS[@]}"; do
        IFS=':' read -r -a parts <<< "$project_info"
        project_name="${parts[0]}"
        build_system="${parts[1]}"
        repo_url="${parts[2]}:${parts[3]}"
        branch="${parts[4]}"

        echo ""
        echo "========================================"
        echo "Processing: $project_name ($build_system)"
        echo "========================================"
        echo ""

        for compiler in "${COMPILERS[@]}"; do
            total_attempts=$((total_attempts + 1))

            log "INFO" "Checking $project_name with $compiler..."

            # Check if already built
            if [ "$SKIP_EXISTING" = true ]; then
                if check_existing_build "$project_name" "$build_system" "$compiler"; then
                    results["${project_name}_${compiler}"]="SKIP:ALREADY_SUCCESS"
                    skipped_builds=$((skipped_builds + 1))
                    continue
                fi
            fi

            # Build project
            build_project "$project_name" "$build_system" "$repo_url" "$branch" "$compiler"
            local exit_code=$?

            if [ $exit_code -eq 0 ]; then
                results["${project_name}_${compiler}"]="NEW:SUCCESS"
                successful_builds=$((successful_builds + 1))
            elif [ $exit_code -eq 2 ]; then
                results["${project_name}_${compiler}"]="NO_TRACE:NO_TRACES"
                no_trace_builds=$((no_trace_builds + 1))
            else
                results["${project_name}_${compiler}"]="FAIL:BUILD_FAILED"
                failed_builds=$((failed_builds + 1))
                log_error "Build failed for $project_name with $compiler"
            fi
        done

        # Check if all compilers failed
        local all_failed=true
        for compiler in "${COMPILERS[@]}"; do
            if [[ "${results["${project_name}_${compiler}"]}" != FAIL:* ]]; then
                all_failed=false
                break
            fi
        done

        if [ "$all_failed" = true ]; then
            log_error "All builds failed for $project_name"
        fi
    done

    # Generate summary
    generate_summary "$total_attempts" "$successful_builds" "$failed_builds" "$skipped_builds" "$no_trace_builds" results
}

#=============================================================================
# Summary Generation
#=============================================================================

generate_summary() {
    local total_attempts="$1"
    local successful_builds="$2"
    local failed_builds="$3"
    local skipped_builds="$4"
    local no_trace_builds="$5"
    local -n results_ref="$6"

    echo ""
    echo "========================================"
    echo "Generating Summary Report"
    echo "========================================"
    echo ""

    local success_rate=0
    if [ "$total_attempts" -gt 0 ]; then
        success_rate=$(awk "BEGIN {printf \"%.1f\", ($successful_builds + $skipped_builds) * 100 / $total_attempts}")
    fi

    local new_rate=0
    # shellcheck disable=SC2086
    if [ $total_attempts -gt 0 ]; then
        new_rate=$(awk "BEGIN {printf \"%.1f\", $successful_builds * 100 / $total_attempts}")
    fi

    {
        echo ""
        echo "========================================"
        echo "BHA Automated Testing Summary"
        echo "========================================"
        echo ""
        echo "Test Date: $(date)"
        echo "Test Root: $TEST_ROOT"
        echo "BHA Binary: $BHA_BINARY"
        echo ""
        echo "========================================"
        echo "Overall Statistics"
        echo "========================================"
        echo "Total projects attempted: ${#PROJECTS[@]}"
        echo "Successful builds: $successful_builds"
        echo "Failed builds: $failed_builds"
        echo "Skipped builds (already successful): $skipped_builds"
        echo "Success rate: ${success_rate}%"
        echo "New builds: ${new_rate}%"
        echo ""
        echo "========================================"
        echo "Results by Project/Compiler"
        echo "========================================"
        echo ""

        # Group by project
        for project_info in "${PROJECTS[@]}"; do
            IFS=':' read -r project_name _ _ _ <<< "$project_info"
            echo "--- $project_name ---"
            for compiler in "${COMPILERS[@]}"; do
                local result="${results_ref["${project_name}_${compiler}"]}"
                IFS=':' read -r status reason <<< "$result"

                case "$status" in
                    NEW)
                        echo "  $compiler: [$status] $reason"
                        ;;
                    SKIP)
                        echo "  $compiler: [$status] $reason"
                        ;;
                    NO_TRACE)
                        echo "  $compiler: [$status] $reason"
                        ;;
                    FAIL)
                        echo "  $compiler: [$status] $reason"
                        ;;
                esac
            done
            echo ""
        done

        echo "========================================"
        echo "Output Locations"
        echo "========================================"
        echo "Full log: $LOG_FILE"
        echo "Error log: $ERROR_LOG"
        echo "Repository cache: $REPO_CACHE"
        echo ""
        echo "Per-project outputs are in:"
        echo "  \$TEST_ROOT/<build_system>/<compiler>/<project>/output/"
        echo ""
    } | tee "$SUMMARY_FILE"

    log "INFO" "Summary written to: $SUMMARY_FILE"
}

#=============================================================================
# Command-Line Argument Parsing
#=============================================================================

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --help|-h)
                show_help
                exit 0
                ;;
            --compilers)
                shift
                IFS=',' read -r -a COMPILERS <<< "$1"
                ;;
            --projects)
                shift
                # Filter PROJECTS array
                IFS=',' read -r -a selected_projects <<< "$1"
                local filtered_projects=()
                for project_info in "${PROJECTS[@]}"; do
                    IFS=':' read -r project_name _ _ _ <<< "$project_info"
                    for selected in "${selected_projects[@]}"; do
                        if [ "$project_name" = "$selected" ]; then
                            filtered_projects+=("$project_info")
                            break
                        fi
                    done
                done
                PROJECTS=("${filtered_projects[@]}")
                ;;
            --no-skip)
                SKIP_EXISTING=false
                ;;
            --verbose-capture)
                VERBOSE_CAPTURE=1
                ;;
            *)
                echo "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
        shift
    done
}

show_help() {
    cat << EOF
BHA Automated Testing Script

Usage: $0 [OPTIONS]

Options:
  --help, -h              Show this help message
  --compilers COMPILERS   Comma-separated list of compilers (default: clang,gcc)
  --projects PROJECTS     Comma-separated list of projects to test
  --no-skip              Don't skip already successful builds
  --verbose-capture      Enable verbose output from compiler launcher

Examples:
  $0
  $0 --compilers gcc
  $0 --projects leveldb,rocksdb --compilers clang
  $0 --no-skip --verbose-capture

EOF
}

#=============================================================================
# Main Entry Point
#=============================================================================

main() {
    # Parse arguments
    parse_arguments "$@"

    # Initialize
    mkdir -p "$TEST_ROOT"
    mkdir -p "$REPO_CACHE"

    # Clear logs
    # shellcheck disable=SC2188
    > "$LOG_FILE"
    # shellcheck disable=SC2188
    > "$ERROR_LOG"

    # Print header
    echo ""
    echo "========================================"
    echo "Build Hotspot Analyzer - Automated Testing"
    echo "========================================"
    echo ""

    log "INFO" "Start time: $(date)"
    log "INFO" "Test root: $TEST_ROOT"
    log "INFO" "BHA binary: $BHA_BINARY"
    log "INFO" "Repository cache: $REPO_CACHE"
    log "INFO" "Compilers: ${COMPILERS[*]}"

    # Check prerequisites
    if ! check_prerequisites; then
        log_error "Prerequisites check failed"
        exit 1
    fi

    # Run tests
    run_tests

    # Final message
    echo ""
    echo "========================================"
    echo "Testing Complete"
    echo "========================================"
    echo ""

    log "INFO" "End time: $(date)"

    echo ""
    echo "Testing complete!"
    echo "Results saved to: $TEST_ROOT"
    echo "Summary: $SUMMARY_FILE"
    echo ""

    # Count results
    # shellcheck disable=SC2155
    # shellcheck disable=SC2155
    local total_new=$(grep -c "\[NEW\]" "$SUMMARY_FILE" 2>/dev/null || echo 0)
    # shellcheck disable=SC2155
    local total_skip=$(grep -c "\[SKIP\]" "$SUMMARY_FILE" 2>/dev/null || echo 0)
    # shellcheck disable=SC2155
    local total_fail=$(grep -c "\[FAIL\]" "$SUMMARY_FILE" 2>/dev/null || echo 0)
    local total_attempts=$((total_new + total_skip + total_fail))

    echo "Statistics:"
    echo "  - Total attempts: $total_attempts"
    echo "  - New successful builds: $total_new"
    echo "  - Skipped (already successful): $total_skip"
    echo "  - Failed builds: $total_fail"

    exit 0
}

# Run main function
main "$@"