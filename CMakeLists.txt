cmake_minimum_required(VERSION 3.31)
project(BuildtimeHotspotAnalyzer)

set(CMAKE_CXX_STANDARD 20)

add_executable(BuildtimeHotspotAnalyzer main.cpp
        headers/bha/utils/string_utils.h)
