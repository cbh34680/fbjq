# CMakeLists.txt

cmake_minimum_required(VERSION 3.30)
project(fbjq CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(PkgConfig REQUIRED)

# 現在設定されているフラグを確認・出力する例
message(STATUS "CXX Flags (Debug): ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CXX Flags (Release): ${CMAKE_CXX_FLAGS_RELEASE}")

add_subdirectory(lib)
add_subdirectory(fs)
add_subdirectory(delivery)
add_subdirectory(executor)
