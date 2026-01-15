# ==============================================================================
# Resource Embedding System for BuildtimeHotspotAnalyzer
# ==============================================================================
#
# This CMake script embeds resource files (HTML templates, CSS, JS, vendor libs)
# as C++ constexpr string_view variables for offline-capable HTML reports.
#
# Purpose: Fix MSVC C2026 errors (string literal > 16KB) and enable offline HTML
# ==============================================================================

cmake_minimum_required(VERSION 3.28)

# Find Python3
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Output directory for generated headers
set(RESOURCE_GEN_DIR "${CMAKE_BINARY_DIR}/generated/resources")
file(MAKE_DIRECTORY ${RESOURCE_GEN_DIR})

# Python script for generating resource headers
set(GENERATOR_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/generate_resource_header.py")

# ==============================================================================
# Function: embed_resource
# Converts a resource file into a C++ header using Python script
# ==============================================================================
function(embed_resource RESOURCE_FILE RESOURCE_VAR_NAME OUTPUT_HEADER)
    # Get the absolute path to the resource file
    get_filename_component(RESOURCE_FILE_ABS "${RESOURCE_FILE}" ABSOLUTE)

    # Generate the header using Python script
    add_custom_command(
        OUTPUT "${OUTPUT_HEADER}"
        COMMAND ${Python3_EXECUTABLE} "${GENERATOR_SCRIPT}"
                "${RESOURCE_FILE_ABS}"
                "${RESOURCE_VAR_NAME}"
                "${OUTPUT_HEADER}"
        DEPENDS "${RESOURCE_FILE}" "${GENERATOR_SCRIPT}"
        COMMENT "Generating resource header: ${OUTPUT_HEADER}"
        VERBATIM
    )
endfunction()

# ==============================================================================
# Define all resources to embed
# ==============================================================================

# Vendor resources (external libraries)
embed_resource(
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/d3.v7.min.js"
    "D3_JS_DATA"
    "${RESOURCE_GEN_DIR}/d3_js.hpp"
)

embed_resource(
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/fontawesome.min.css"
    "FONTAWESOME_CSS_DATA"
    "${RESOURCE_GEN_DIR}/fontawesome_css.hpp"
)

# HTML template resources
embed_resource(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/report_head.html"
    "REPORT_HEAD_HTML"
    "${RESOURCE_GEN_DIR}/report_head.hpp"
)

embed_resource(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/report_body_start.html"
    "REPORT_BODY_START_HTML"
    "${RESOURCE_GEN_DIR}/report_body_start.hpp"
)

embed_resource(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/report_body_sections.html"
    "REPORT_BODY_SECTIONS_HTML"
    "${RESOURCE_GEN_DIR}/report_body_sections.hpp"
)

embed_resource(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/report_body_end.html"
    "REPORT_BODY_END_HTML"
    "${RESOURCE_GEN_DIR}/report_body_end.hpp"
)

# CSS and JavaScript resources
embed_resource(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/styles.css"
    "STYLES_CSS"
    "${RESOURCE_GEN_DIR}/styles_css.hpp"
)

embed_resource(
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/visualization.js"
    "VISUALIZATION_JS"
    "${RESOURCE_GEN_DIR}/visualization_js.hpp"
)

# ==============================================================================
# Create custom target for resource generation
# ==============================================================================
add_custom_target(generate_resources ALL
    DEPENDS
        "${RESOURCE_GEN_DIR}/d3_js.hpp"
        "${RESOURCE_GEN_DIR}/fontawesome_css.hpp"
        "${RESOURCE_GEN_DIR}/report_head.hpp"
        "${RESOURCE_GEN_DIR}/report_body_start.hpp"
        "${RESOURCE_GEN_DIR}/report_body_sections.hpp"
        "${RESOURCE_GEN_DIR}/report_body_end.hpp"
        "${RESOURCE_GEN_DIR}/styles_css.hpp"
        "${RESOURCE_GEN_DIR}/visualization_js.hpp"
)

# ==============================================================================
# Documentation
# ==============================================================================
message(STATUS "")
message(STATUS "==========================================================")
message(STATUS "Resource Embedding System Configuration")
message(STATUS "==========================================================")
message(STATUS "Resource generation directory: ${RESOURCE_GEN_DIR}")
message(STATUS "Python interpreter: ${Python3_EXECUTABLE}")
message(STATUS "")
message(STATUS "Embedded resources:")
message(STATUS "  - D3.js v7 (vendor/d3.v7.min.js)")
message(STATUS "  - Font Awesome CSS (vendor/fontawesome.min.css)")
message(STATUS "  - HTML templates (templates/*.html)")
message(STATUS "  - CSS styles (templates/styles.css)")
message(STATUS "  - JavaScript visualizations (templates/visualization.js)")
message(STATUS "")
message(STATUS "Generated headers will be available at:")
message(STATUS "  ${RESOURCE_GEN_DIR}/*.hpp")
message(STATUS "==========================================================")
message(STATUS "")
