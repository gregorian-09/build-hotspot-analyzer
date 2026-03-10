cmake_minimum_required(VERSION 3.20)
project(suggester_headers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(suggester_headers STATIC
    src/consumer.cpp
    src/file1.cpp
    src/file2.cpp
    src/file3.cpp
    src/file4.cpp
    src/file5.cpp
    src/file6.cpp
    src/file7.cpp
    src/file8.cpp
    src/file9.cpp
    src/file10.cpp
)

target_include_directories(suggester_headers PUBLIC include)
