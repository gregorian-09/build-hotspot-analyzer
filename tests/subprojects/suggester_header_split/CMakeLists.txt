cmake_minimum_required(VERSION 3.20)
project(suggester_header_split LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(suggester_header_split STATIC
    src/worker1.cpp
    src/worker2.cpp
    src/worker3.cpp
    src/worker4.cpp
    src/worker5.cpp
    src/worker6.cpp
    src/worker7.cpp
    src/worker8.cpp
)

target_include_directories(suggester_header_split PUBLIC include)
