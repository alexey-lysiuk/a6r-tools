cmake_minimum_required(VERSION 3.15)
project(libresdr-noise VERSION 1.0 LANGUAGES C CXX)

option(LIBRESDR_NOISE_BUILD_GUI "Build GUI target" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBIIO REQUIRED libiio)
pkg_check_modules(LIBAD9361 REQUIRED libad9361)

add_executable(libresdr-noise main.c)
target_compile_options(libresdr-noise PRIVATE ${LIBIIO_CFLAGS_OTHER} ${LIBAD9361_CFLAGS_OTHER})
target_include_directories(libresdr-noise PRIVATE ${LIBIIO_INCLUDE_DIRS} ${LIBAD9361_INCLUDE_DIRS})
target_link_libraries(libresdr-noise PRIVATE ${LIBIIO_LINK_LIBRARIES} ${LIBAD9361_LINK_LIBRARIES})

if(LIBRESDR_NOISE_BUILD_GUI)
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

    set(IMGUI_CORE_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    )

    if(WIN32)

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

    find_package(Threads REQUIRED)

    if(WIN32)
        add_executable(libresdr-noise-gui WIN32
            libresdr_noise_gui.cpp
            ${PLATFORM_MAIN}
        )
    else()
        add_executable(libresdr-noise-gui
            libresdr_noise_gui.cpp
            ${PLATFORM_MAIN}
        )
    endif()
    target_compile_options(libresdr-noise-gui PRIVATE ${LIBIIO_CFLAGS_OTHER} ${LIBAD9361_CFLAGS_OTHER})
    target_include_directories(libresdr-noise-gui PRIVATE ${LIBIIO_INCLUDE_DIRS} ${LIBAD9361_INCLUDE_DIRS})
    target_link_libraries(libresdr-noise-gui PRIVATE imgui_backend ${LIBIIO_LINK_LIBRARIES} ${LIBAD9361_LINK_LIBRARIES} Threads::Threads)
endif()
