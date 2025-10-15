cmake_minimum_required(VERSION 3.31)
project(BuildtimeHotspotAnalyzer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_SYSTEM_OPENSSL "Use the system-installed OpenSSL" ON)

include(FetchContent)

FetchContent_Declare(
        simdjson
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG v4.0.7
)

FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
)

FetchContent_MakeAvailable(simdjson)
FetchContent_MakeAvailable(tomlplusplus)

if(USE_SYSTEM_OPENSSL)
    if(DEFINED ENV{OPENSSL_ROOT_DIR})
        set(OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT_DIR})
    endif()
    find_package(OpenSSL REQUIRED)
else()
    find_package(OpenSSL REQUIRED)
endif()

if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR "OpenSSL not found. Please install or specify OPENSSL_ROOT_DIR.")
endif()

add_executable(BuildtimeHotspotAnalyzer main.cpp
        headers/bha/utils/string_utils.h
        sources/bha/utils/string_utils.cpp
        headers/bha/utils/path_utils.h
        sources/bha/utils/path_utils.cpp
        headers/bha/utils/file_utils.h
        sources/bha/utils/file_utils.cpp
        headers/bha/utils/hash_utils.h
        sources/bha/utils/hash_utils.cpp
        headers/bha/utils/json_utils.h
        sources/bha/utils/json_utils.cpp
        headers/bha/core/error.h
        sources/bha/core/error.cpp
        headers/bha/core/result.h
        headers/bha/core/types.h
        sources/bha/core/types.cpp
        headers/bha/core/config.h
        sources/bha/core/config.cpp
)

include_directories(${PROJECT_SOURCE_DIR}/headers)

target_link_libraries(BuildtimeHotspotAnalyzer
        PRIVATE OpenSSL::SSL
        PRIVATE simdjson::simdjson
        PRIVATE tomlplusplus::tomlplusplus
)