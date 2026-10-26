cmake_minimum_required(VERSION 3.20)
project(ramen_vrm_mod LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RVRM_BUILD_TESTS "core の単体テストをビルドする" ON)
option(RVRM_BUILD_TOOLS "vrm_inspect をビルドする" ON)

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(core)

if(RVRM_BUILD_TOOLS)
  add_subdirectory(tools/vrm_inspect)
endif()
