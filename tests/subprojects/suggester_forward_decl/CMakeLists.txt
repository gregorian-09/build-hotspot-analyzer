cmake_minimum_required(VERSION 3.20)
project(suggester_forward_decl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(suggester_forward_decl STATIC
    src/alpha.cpp
    src/beta.cpp
)

target_include_directories(suggester_forward_decl PUBLIC include)
