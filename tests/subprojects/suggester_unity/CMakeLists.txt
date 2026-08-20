cmake_minimum_required(VERSION 3.20)
project(suggester_unity LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(corelib
  src/a.cpp
  src/b.cpp
  src/c.cpp
)
target_include_directories(corelib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
