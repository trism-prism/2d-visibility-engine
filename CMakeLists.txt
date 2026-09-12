cmake_minimum_required(VERSION 3.25)
project(VisibilityEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VIS_BUILD_APP "Build the GLFW + Dear ImGui application (fetches GLFW and ImGui)" ON)
option(VIS_BUILD_TESTS "Build the doctest suite (fetches doctest)" ON)
option(VIS_SANITIZE "Build with MSVC AddressSanitizer" OFF)

# Warnings apply to our own targets only; third-party targets never link this.
add_library(vis_warnings INTERFACE)
if(MSVC)
  target_compile_options(vis_warnings INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
  target_compile_definitions(vis_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(vis_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(VIS_SANITIZE)
  if(NOT MSVC)
    message(FATAL_ERROR "VIS_SANITIZE is implemented for MSVC only")
  endif()
  add_compile_options(/fsanitize=address /Zi)
  add_link_options(/INCREMENTAL:NO /DEBUG)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

add_subdirectory(core)

if(VIS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(VIS_BUILD_APP)
  add_subdirectory(app)
endif()
