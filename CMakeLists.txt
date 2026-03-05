cmake_minimum_required(VERSION 3.20)
project(cadmia-explorer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BUILD_GUI "Build the interactive GUI (requires imgui/GLFW/OpenGL)" ON)

# ---------------------------------------------------------------------------
# C++ unit tests — always built (only requires Catch2)
# ---------------------------------------------------------------------------
find_package(Catch2 CONFIG REQUIRED)
enable_testing()

add_executable(cadvis-test
    tests/test_logic.cpp
    src/reader.cpp
    src/layout.cpp
)
target_include_directories(cadvis-test PRIVATE src)
target_link_libraries(cadvis-test PRIVATE Catch2::Catch2WithMain)
target_compile_options(cadvis-test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
add_test(NAME cadvis-test COMMAND cadvis-test)

# ---------------------------------------------------------------------------
# Main executable
# With BUILD_GUI=ON  (default, CI): full ImGui viewer + headless SVG mode
# With BUILD_GUI=OFF (no X11/display): headless SVG-only, same CLI interface
# ---------------------------------------------------------------------------
if(BUILD_GUI)
    find_package(imgui CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)

    add_executable(cadvis
        src/main.cpp
        src/reader.cpp
        src/layout.cpp
        src/svg.cpp
        src/app.cpp
        src/renderer.cpp
    )
    target_link_libraries(cadvis PRIVATE imgui::imgui OpenGL::GL)
    target_compile_definitions(cadvis PRIVATE CADVIS_GUI)
else()
    # Headless-only build: no ImGui/GLFW/OpenGL needed
    add_executable(cadvis
        src/main.cpp
        src/reader.cpp
        src/layout.cpp
        src/svg.cpp
    )
endif()

target_include_directories(cadvis PRIVATE src)
target_compile_options(cadvis PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CONFIG:Release>:-O2>
)
