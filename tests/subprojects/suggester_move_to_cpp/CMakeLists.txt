cmake_minimum_required(VERSION 3.20)
project(suggester_move_to_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(suggester_move_to_cpp STATIC
    src/widget.cpp
    src/worker.cpp
)

target_include_directories(suggester_move_to_cpp PUBLIC include)
