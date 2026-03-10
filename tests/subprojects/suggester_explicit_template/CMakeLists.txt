cmake_minimum_required(VERSION 3.20)
project(suggester_explicit_template LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(suggester_explicit_template STATIC
    src/use_box_a.cpp
    src/use_box_b.cpp
    src/use_box_c.cpp
)

target_include_directories(suggester_explicit_template PUBLIC include)
