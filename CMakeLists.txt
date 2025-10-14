cmake_minimum_required(VERSION 3.31)
project(BuildtimeHotspotAnalyzer)

set(CMAKE_CXX_STANDARD 20)

add_executable(BuildtimeHotspotAnalyzer main.cpp
        headers/bha/utils/string_utils.h
        sources/bha/utils/string_utils.cpp
        headers/bha/utils/path_utils.h
        sources/bha/utils/path_utils.cpp)

include_directories(${PROJECT_SOURCE_DIR}/headers)