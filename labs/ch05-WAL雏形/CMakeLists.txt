cmake_minimum_required(VERSION 3.16)
project(lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # 给 clangd / 编辑器用

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_executable(lab
    src/main.cpp
    src/crc32.h
    src/wal.h
    src/wal.cpp
    src/fd_guard.h
    src/thread_pool.h
)
