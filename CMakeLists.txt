# 全プロジェクトの共通設定

# CMakePresets.jsonのversion 6とFetchContentのSYSTEMに3.25が必要
cmake_minimum_required(VERSION 3.25)

# SDL3のためにCも有効にする
project(TellerProject LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Dependencies.cmake)

# 自前のターゲットだけにリンクするコンパイラ設定
add_library(teller_project_options INTERFACE)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_compile_options(teller_project_options INTERFACE
    /utf-8
    /W4
    /permissive-
    /Zc:__cplusplus
  )
else()
  target_compile_options(teller_project_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
  )
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # Linuxで問題になるインクルードの大文字小文字の誤りを検出する
  target_compile_options(teller_project_options INTERFACE
    -Wnonportable-include-path
  )
endif()
