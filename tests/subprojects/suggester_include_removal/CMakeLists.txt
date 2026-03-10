cmake_minimum_required(VERSION 3.20)
project(suggester_include_removal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(suggester_include_removal STATIC ${SOURCES})

target_include_directories(suggester_include_removal PUBLIC include)
