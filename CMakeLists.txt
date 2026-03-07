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
# C++ unit tests — native builds only (Catch2 not available for Emscripten)
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
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
endif()

# ---------------------------------------------------------------------------
# Main executable
# With BUILD_GUI=ON + EMSCRIPTEN : WASM viewer (cadvis.html)
# With BUILD_GUI=ON + native      : desktop viewer + headless SVG mode
# With BUILD_GUI=OFF              : headless SVG-only cadvis
# ---------------------------------------------------------------------------
if(BUILD_GUI)
    if(EMSCRIPTEN)
        # --- WASM build: use FetchContent for imgui; GLFW/GL from Emscripten ---
        include(FetchContent)
        FetchContent_Declare(imgui_src
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG        v1.91.9)   # matches vcpkg baseline 45179168
        FetchContent_MakeAvailable(imgui_src)

        add_executable(cadvis_web
            src/main.cpp
            src/reader.cpp
            src/layout.cpp
            src/svg.cpp
            src/app.cpp
            src/renderer.cpp
            ${imgui_src_SOURCE_DIR}/imgui.cpp
            ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_src_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_src_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        )
        target_include_directories(cadvis_web PRIVATE
            src
            ${imgui_src_SOURCE_DIR}
            ${imgui_src_SOURCE_DIR}/backends
        )
        target_compile_definitions(cadvis_web PRIVATE CADVIS_GUI)
        target_compile_options(cadvis_web PRIVATE -Wall -Wextra)
        set_target_properties(cadvis_web PROPERTIES
            OUTPUT_NAME "cadvis"
            SUFFIX      ".html"
        )
        target_link_options(cadvis_web PRIVATE
            -sUSE_GLFW=3
            -sUSE_WEBGL2=1
            -sMIN_WEBGL_VERSION=2
            -sMAX_WEBGL_VERSION=2
            -sFORCE_FILESYSTEM=1
            -sALLOW_MEMORY_GROWTH=1
            "-sEXPORTED_RUNTIME_METHODS=['ccall','FS']"
            "-sEXPORTED_FUNCTIONS=['_main','_reload_file']"
            --shell-file ${CMAKE_SOURCE_DIR}/shell.html
        )
    else()
        # --- Desktop build (unchanged from Phase 2) ---
        find_package(imgui CONFIG REQUIRED)
        find_package(glfw3 CONFIG REQUIRED)
        find_package(OpenGL REQUIRED)

        add_executable(cadvis
            src/main.cpp
            src/reader.cpp
            src/layout.cpp
            src/svg.cpp
            src/app.cpp
            src/renderer.cpp
        )
        target_include_directories(cadvis PRIVATE src)
        target_link_libraries(cadvis PRIVATE imgui::imgui glfw OpenGL::GL)
        target_compile_definitions(cadvis PRIVATE CADVIS_GUI)
        target_compile_options(cadvis PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
            $<$<CONFIG:Release>:-O2>
        )
    endif()
else()
    # --- Headless-only build: no ImGui/GLFW/OpenGL needed ---
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
endif()
