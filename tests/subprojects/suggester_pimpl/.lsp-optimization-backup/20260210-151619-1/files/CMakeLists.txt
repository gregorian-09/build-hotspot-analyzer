cmake_minimum_required(VERSION 3.20)
project(suggester_pimpl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(suggester_pimpl STATIC
    src/pimpl_widget.cpp
    src/driver.cpp
)

target_include_directories(suggester_pimpl PUBLIC include)
