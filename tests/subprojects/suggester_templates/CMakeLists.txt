cmake_minimum_required(VERSION 3.20)
project(suggester_templates LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(suggester_templates STATIC
    src/use_templates_a.cpp
    src/use_templates_b.cpp
    src/use_templates_c.cpp
)

target_include_directories(suggester_templates PUBLIC include)
