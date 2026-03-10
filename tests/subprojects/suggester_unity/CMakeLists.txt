cmake_minimum_required(VERSION 3.20)
project(suggester_unity LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CMakeParseArguments)

function(my_cc_library)
  set(options)
  set(oneValueArgs NAME)
  set(multiValueArgs SRCS HDRS)
  cmake_parse_arguments(MY "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  add_library(${MY_NAME} ${MY_SRCS} ${MY_HDRS})
  target_include_directories(${MY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
endfunction()

my_cc_library(
  NAME corelib
  SRCS
    src/a.cpp
    src/b.cpp
    src/c.cpp
  HDRS
    include/common.hpp
)
