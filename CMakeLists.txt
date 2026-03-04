cmake_minimum_required(VERSION 3.20)
project(cadmia-explorer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_executable(cadvis
    src/main.cpp
    src/reader.cpp
    src/layout.cpp
    src/svg.cpp
)

target_include_directories(cadvis PRIVATE src)
target_compile_options(cadvis PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CONFIG:Release>:-O2>
)
