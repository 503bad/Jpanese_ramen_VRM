# UE4SS C++ MOD のビルド。core のビルド(リポジトリ直下の CMakeLists.txt)とは別に構成する。
#   cmake -S mod -B build_mod -G "Visual Studio 18 2026" -A x64
#   cmake --build build_mod --config Game__Shipping__Win64 --target RamenVRM
# 前提: external/RE-UE4SS に、同梱する UE4SS.dll と同じコミットのソース(サブモジュール込み)があること(docs/BUILD.md)。
cmake_minimum_required(VERSION 3.22)
project(RamenVRMMod LANGUAGES C CXX)

set(RVRM_MOD_BUILD ON)
set(RVRM_UE4SS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/RE-UE4SS" CACHE PATH "RE-UE4SS のソース")

add_subdirectory(${RVRM_UE4SS_DIR} RE-UE4SS)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core rvrm_core)
add_subdirectory(RamenVRM)
