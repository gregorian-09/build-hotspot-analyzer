# BHAHelpers.cmake
# Helper functions for integrating Build Hotspot Analyzer with CMake projects.
#
# Usage:
#   include(BHAHelpers)
#   bha_enable_tracing(my_target)       # Enable for specific target
#   bha_enable_tracing_all()            # Enable for all targets
#
# How it works:
#   - Clang/ICX: Uses -ftime-trace, outputs JSON automatically
#   - GCC/MSVC/Intel/NVCC: Uses compiler launcher to capture timing output
#
# After building, traces are stored in:
#   - Clang/ICX: <build>/*.json (next to object files)
#   - Others: <build>/bha_traces/<source>.bha.txt
#
# Then run: bha analyze <build>/bha_traces
#
# Functions:
#   bha_enable_tracing(target) - Enable timing traces for a target
#   bha_enable_tracing_all() - Enable for all targets in directory
#   bha_setup_tracing_directory() - Create trace output directory
#   bha_detect_compiler() - Detect compiler type and set tracing flags
#

include_guard(GLOBAL)

# Get the directory where this CMake file is located
get_filename_component(BHA_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)

# Detect compiler type and appropriate timing flags
function(bha_detect_compiler OUT_COMPILER OUT_FLAGS)
    # Check for Intel compiler first (can also match "Clang" for ICX)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
        if(CMAKE_CXX_COMPILER MATCHES "icx")
            # Intel oneAPI (ICX) - Clang-based, uses -ftime-trace
            set(${OUT_COMPILER} "icx" PARENT_SCOPE)
            set(${OUT_FLAGS} "-ftime-trace" PARENT_SCOPE)
        else()
            # Intel Classic (ICC)
            set(${OUT_COMPILER} "intel" PARENT_SCOPE)
            set(${OUT_FLAGS} "-qopt-report=5" PARENT_SCOPE)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(${OUT_COMPILER} "clang" PARENT_SCOPE)
        set(${OUT_FLAGS} "-ftime-trace" PARENT_SCOPE)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set(${OUT_COMPILER} "gcc" PARENT_SCOPE)
        set(${OUT_FLAGS} "-ftime-report" PARENT_SCOPE)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        set(${OUT_COMPILER} "msvc" PARENT_SCOPE)
        set(${OUT_FLAGS} "/Bt+" PARENT_SCOPE)
    elseif(CMAKE_CUDA_COMPILER)
        set(${OUT_COMPILER} "nvcc" PARENT_SCOPE)
        set(${OUT_FLAGS} "--time" PARENT_SCOPE)
    else()
        set(${OUT_COMPILER} "unknown" PARENT_SCOPE)
        set(${OUT_FLAGS} "" PARENT_SCOPE)
        message(WARNING "BHA: Unsupported compiler ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()

# Setup trace output directory and configure compiler launcher
function(bha_setup_tracing_directory)
    set(options "")
    set(oneValueArgs DIRECTORY)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(ARG_DIRECTORY)
        set(BHA_TRACE_DIR "${ARG_DIRECTORY}" CACHE PATH "Directory for BHA trace files")
    else()
        set(BHA_TRACE_DIR "${CMAKE_BINARY_DIR}/bha_traces" CACHE PATH "Directory for BHA trace files")
    endif()

    file(MAKE_DIRECTORY "${BHA_TRACE_DIR}")

    # Detect compiler
    bha_detect_compiler(BHA_COMPILER BHA_FLAGS)
    set(BHA_COMPILER "${BHA_COMPILER}" CACHE STRING "Detected compiler for BHA tracing")
    set(BHA_TRACE_FLAGS "${BHA_FLAGS}" CACHE STRING "Compiler flags for BHA tracing")

    # For compilers that output to console (GCC, MSVC, Intel Classic, NVCC),
    # set up the compiler launcher for automatic per-file capture
    if(BHA_COMPILER STREQUAL "gcc" OR
            BHA_COMPILER STREQUAL "msvc" OR
            BHA_COMPILER STREQUAL "intel" OR
            BHA_COMPILER STREQUAL "nvcc")

        # Set environment variable for trace directory
        set(ENV{BHA_TRACE_DIR} "${BHA_TRACE_DIR}")

        # Set compiler launcher
        if(WIN32)
            set(BHA_CAPTURE_SCRIPT "${BHA_CMAKE_DIR}/bha-capture.bat" CACHE FILEPATH "BHA capture script")
        else()
            set(BHA_CAPTURE_SCRIPT "${BHA_CMAKE_DIR}/bha-capture.sh" CACHE FILEPATH "BHA capture script")
            # Make executable
            execute_process(COMMAND chmod +x "${BHA_CAPTURE_SCRIPT}" ERROR_QUIET)
        endif()

        if(EXISTS "${BHA_CAPTURE_SCRIPT}")
            set(CMAKE_CXX_COMPILER_LAUNCHER
                    "${CMAKE_COMMAND}" "-E" "env" "BHA_TRACE_DIR=${BHA_TRACE_DIR}" "${BHA_CAPTURE_SCRIPT}"
                    CACHE STRING "BHA compiler launcher" FORCE)
            set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_CXX_COMPILER_LAUNCHER}" CACHE STRING "" FORCE)
            message(STATUS "BHA: Compiler launcher enabled - traces auto-saved to ${BHA_TRACE_DIR}")
        endif()
    endif()

    message(STATUS "BHA: Tracing enabled for ${BHA_COMPILER}")
    message(STATUS "BHA: Trace directory: ${BHA_TRACE_DIR}")
    message(STATUS "BHA: Compiler flags: ${BHA_TRACE_FLAGS}")
endfunction()

# Enable tracing for a specific target
function(bha_enable_tracing target)
    if(NOT DEFINED BHA_TRACE_FLAGS)
        bha_setup_tracing_directory()
    endif()

    if(BHA_TRACE_FLAGS)
        target_compile_options(${target} PRIVATE ${BHA_TRACE_FLAGS})

        # For Clang, traces are automatic JSON files
        # For GCC/MSVC, we need to capture stderr
        if(BHA_COMPILER STREQUAL "clang")
            message(STATUS "BHA: ${target} will output traces to build directory")
        else()
            message(STATUS "BHA: ${target} will output timing to stderr (use 'bha record' to capture)")
        endif()
    endif()
endfunction()

# Enable tracing for all targets in the current directory
function(bha_enable_tracing_all)
    if(NOT DEFINED BHA_TRACE_FLAGS)
        bha_setup_tracing_directory()
    endif()

    if(BHA_TRACE_FLAGS)
        add_compile_options(${BHA_TRACE_FLAGS})
        message(STATUS "BHA: Tracing enabled globally with ${BHA_TRACE_FLAGS}")
    endif()
endfunction()

# Add a custom target to run analysis
function(bha_add_analysis_target)
    set(options "")
    set(oneValueArgs NAME OUTPUT)
    set(multiValueArgs DEPENDS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_NAME)
        set(ARG_NAME "bha-analyze")
    endif()

    if(NOT ARG_OUTPUT)
        set(ARG_OUTPUT "${CMAKE_BINARY_DIR}/bha_report.html")
    endif()

    find_program(BHA_EXECUTABLE bha HINTS "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/build")

    if(BHA_EXECUTABLE)
        if(BHA_COMPILER STREQUAL "clang")
            # For Clang, directly analyze the JSON traces
            add_custom_target(${ARG_NAME}
                    COMMAND ${BHA_EXECUTABLE} export
                    --format html
                    --output "${ARG_OUTPUT}"
                    --include-suggestions
                    "${CMAKE_BINARY_DIR}"
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    COMMENT "Running BHA analysis..."
                    DEPENDS ${ARG_DEPENDS}
            )
        else()
            # For GCC/MSVC, traces should be in the trace directory
            add_custom_target(${ARG_NAME}
                    COMMAND ${BHA_EXECUTABLE} export
                    --format html
                    --output "${ARG_OUTPUT}"
                    --include-suggestions
                    "${BHA_TRACE_DIR}"
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    COMMENT "Running BHA analysis..."
                    DEPENDS ${ARG_DEPENDS}
            )
        endif()

        message(STATUS "BHA: Added analysis target '${ARG_NAME}'")
        message(STATUS "BHA: Run 'cmake --build . --target ${ARG_NAME}' after building")
    else()
        message(WARNING "BHA: 'bha' executable not found, analysis target not created")
    endif()
endfunction()

# Create a wrapper script for capturing GCC/MSVC output
function(bha_create_capture_script)
    set(options "")
    set(oneValueArgs OUTPUT_DIR SCRIPT_NAME)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${BHA_TRACE_DIR}")
    endif()

    if(NOT ARG_SCRIPT_NAME)
        set(ARG_SCRIPT_NAME "bha_capture")
    endif()

    if(WIN32)
        # Windows batch script
        set(SCRIPT_CONTENT "@echo off
setlocal
set OUTPUT_DIR=${ARG_OUTPUT_DIR}
set TIMESTAMP=%date:~-4%%date:~4,2%%date:~7,2%_%time:~0,2%%time:~3,2%%time:~6,2%
set TRACE_FILE=%OUTPUT_DIR%\\trace_%TIMESTAMP%.txt
if not exist \"%OUTPUT_DIR%\" mkdir \"%OUTPUT_DIR%\"
%* 2>&1 | tee \"%TRACE_FILE%\"
")
        file(WRITE "${CMAKE_BINARY_DIR}/${ARG_SCRIPT_NAME}.bat" "${SCRIPT_CONTENT}")
    else()
        # Unix shell script
        set(SCRIPT_CONTENT "#!/bin/bash
OUTPUT_DIR=\"${ARG_OUTPUT_DIR}\"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
TRACE_FILE=\"$OUTPUT_DIR/trace_$TIMESTAMP.txt\"
mkdir -p \"$OUTPUT_DIR\"
\"$@\" 2>&1 | tee \"$TRACE_FILE\"
")
        file(WRITE "${CMAKE_BINARY_DIR}/${ARG_SCRIPT_NAME}.sh" "${SCRIPT_CONTENT}")
        execute_process(COMMAND chmod +x "${CMAKE_BINARY_DIR}/${ARG_SCRIPT_NAME}.sh")
    endif()

    message(STATUS "BHA: Created capture script: ${CMAKE_BINARY_DIR}/${ARG_SCRIPT_NAME}")
endfunction()

# Print BHA integration status
function(bha_status)
    message(STATUS "")
    message(STATUS "=== Build Hotspot Analyzer (BHA) Status ===")
    message(STATUS "Compiler: ${BHA_COMPILER}")
    message(STATUS "Trace flags: ${BHA_TRACE_FLAGS}")
    message(STATUS "Trace directory: ${BHA_TRACE_DIR}")
    message(STATUS "")

    if(BHA_COMPILER STREQUAL "clang" OR BHA_COMPILER STREQUAL "icx")
        message(STATUS "${BHA_COMPILER} detected: JSON traces created automatically")
        message(STATUS "After building, run:")
        message(STATUS "  bha analyze ${CMAKE_BINARY_DIR}")
    elseif(BHA_COMPILER STREQUAL "gcc")
        message(STATUS "GCC detected: Traces auto-captured via compiler launcher")
        message(STATUS "After building, run:")
        message(STATUS "  bha analyze ${BHA_TRACE_DIR}")
    elseif(BHA_COMPILER STREQUAL "msvc")
        message(STATUS "MSVC detected: Traces auto-captured via compiler launcher")
        message(STATUS "After building, run:")
        message(STATUS "  bha analyze ${BHA_TRACE_DIR}")
    elseif(BHA_COMPILER STREQUAL "intel")
        message(STATUS "Intel ICC detected: Traces auto-captured via compiler launcher")
        message(STATUS "After building, run:")
        message(STATUS "  bha analyze ${BHA_TRACE_DIR}")
    elseif(BHA_COMPILER STREQUAL "nvcc")
        message(STATUS "NVCC detected: Traces auto-captured via compiler launcher")
        message(STATUS "After building, run:")
        message(STATUS "  bha analyze ${BHA_TRACE_DIR}")
    endif()
    message(STATUS "============================================")
    message(STATUS "")
endfunction()