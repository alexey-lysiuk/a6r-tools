cmake_minimum_required(VERSION 3.15)
project(hackrf-noise-gui VERSION 1.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HACKRF REQUIRED libhackrf)

set(IMGUI_CORE_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

if(WIN32)

    # ── Windows: DirectX 11 + Win32 API ────────────────────────────────────
    add_library(imgui_backend STATIC
        ${IMGUI_CORE_SOURCES}
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui_backend PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui_backend PUBLIC d3d11 dxgi d3dcompiler dwmapi imm32)

    set(PLATFORM_MAIN main.cpp)

elseif(APPLE)

    # ── macOS: Metal + OSX (AppKit / MTKView) ──────────────────────────────
    add_library(imgui_backend STATIC
        ${IMGUI_CORE_SOURCES}
        ${imgui_SOURCE_DIR}/backends/imgui_impl_osx.mm
        ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
    )
    target_include_directories(imgui_backend PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui_backend PUBLIC
        "-framework Cocoa"
        "-framework GameController"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
    )

    set(PLATFORM_MAIN main_macos.mm)

else()

    # ── Other platforms: SDL2 + OpenGL2 ────────────────────────────────────
    pkg_check_modules(SDL2 REQUIRED sdl2)
    find_package(OpenGL REQUIRED)

    add_library(imgui_backend STATIC
        ${IMGUI_CORE_SOURCES}
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
    )
    target_include_directories(imgui_backend PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${SDL2_INCLUDE_DIRS}
    )
    target_compile_options(imgui_backend PRIVATE ${SDL2_CFLAGS_OTHER})
    target_link_libraries(imgui_backend PUBLIC ${SDL2_LIBRARIES} OpenGL::GL)

    set(PLATFORM_MAIN main.cpp)

endif()

if(WIN32)
    add_executable(hackrf-noise-gui WIN32
        hackrf_noise_gui.cpp
        ${PLATFORM_MAIN}
    )
else()
    add_executable(hackrf-noise-gui
        hackrf_noise_gui.cpp
        ${PLATFORM_MAIN}
    )
endif()
target_compile_options(hackrf-noise-gui PRIVATE ${HACKRF_CFLAGS_OTHER})
target_include_directories(hackrf-noise-gui PRIVATE ${HACKRF_INCLUDE_DIRS})
target_link_libraries(hackrf-noise-gui PRIVATE imgui_backend ${HACKRF_LINK_LIBRARIES})
