cmake_minimum_required(VERSION 3.20)
project(suggester_pimpl_external_inheritance_nontrivial LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(suggester_pimpl_external_inheritance_nontrivial STATIC
    src/pimpl_widget_external_inheritance_nontrivial.cpp
)

target_include_directories(suggester_pimpl_external_inheritance_nontrivial PUBLIC include)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 2)
endif()
