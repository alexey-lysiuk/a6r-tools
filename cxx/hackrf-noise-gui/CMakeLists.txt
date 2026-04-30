cmake_minimum_required(VERSION 3.15)
project(hackrf-noise-gui VERSION 1.0 LANGUAGES CXX)

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
pkg_check_modules(SDL2 REQUIRED sdl2)

find_package(OpenGL REQUIRED)

add_library(imgui_sdl2_opengl2 STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
)
target_include_directories(imgui_sdl2_opengl2 PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)
target_compile_options(imgui_sdl2_opengl2 PRIVATE ${SDL2_CFLAGS_OTHER})
target_link_libraries(imgui_sdl2_opengl2 PUBLIC ${SDL2_LIBRARIES} OpenGL::GL)

add_executable(hackrf-noise-gui main.cpp)
target_compile_options(hackrf-noise-gui PRIVATE ${HACKRF_CFLAGS_OTHER})
target_include_directories(hackrf-noise-gui PRIVATE ${HACKRF_INCLUDE_DIRS})
target_link_libraries(hackrf-noise-gui PRIVATE imgui_sdl2_opengl2 ${HACKRF_LINK_LIBRARIES})
