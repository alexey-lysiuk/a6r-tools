cmake_minimum_required(VERSION 3.15)
project(spectrum-analyzer VERSION 1.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SA_ENABLE_HACKRF "Build HackRF support (requires libhackrf)" ON)
option(SA_ENABLE_PLUTO "Build PlutoSDR/LibreSDR support (requires libiio and libad9361)" ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.93.1
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

set(SA_SOURCES
    analyzer.cpp
    device.cpp
    device_tinysa.cpp
    serial.cpp
    trace.cpp
    ${PLATFORM_MAIN}
)

if(SA_ENABLE_HACKRF)
    pkg_check_modules(HACKRF libhackrf)
    if(NOT HACKRF_FOUND)
        message(WARNING "libhackrf not found, building without HackRF support")
    endif()
endif()

if(SA_ENABLE_PLUTO)
    pkg_check_modules(IIO libiio)
    pkg_check_modules(AD9361 libad9361)
    if(NOT IIO_FOUND OR NOT AD9361_FOUND)
        message(WARNING "libiio or libad9361 not found, building without PlutoSDR/LibreSDR support")
    endif()
endif()

if(WIN32)
    add_executable(spectrum-analyzer WIN32 ${SA_SOURCES})
else()
    add_executable(spectrum-analyzer ${SA_SOURCES})
endif()

target_link_libraries(spectrum-analyzer PRIVATE imgui_backend Threads::Threads)

if(HACKRF_FOUND)
    target_sources(spectrum-analyzer PRIVATE device_hackrf.cpp)
    target_compile_definitions(spectrum-analyzer PRIVATE SA_HAVE_HACKRF)
    target_compile_options(spectrum-analyzer PRIVATE ${HACKRF_CFLAGS_OTHER})
    target_include_directories(spectrum-analyzer PRIVATE ${HACKRF_INCLUDE_DIRS})
    target_link_libraries(spectrum-analyzer PRIVATE ${HACKRF_LINK_LIBRARIES})
endif()

if(IIO_FOUND AND AD9361_FOUND)
    target_sources(spectrum-analyzer PRIVATE device_pluto.cpp)
    target_compile_definitions(spectrum-analyzer PRIVATE SA_HAVE_PLUTO)
    target_compile_options(spectrum-analyzer PRIVATE ${IIO_CFLAGS_OTHER} ${AD9361_CFLAGS_OTHER})
    target_include_directories(spectrum-analyzer PRIVATE ${IIO_INCLUDE_DIRS} ${AD9361_INCLUDE_DIRS})
    target_link_libraries(spectrum-analyzer PRIVATE ${IIO_LINK_LIBRARIES} ${AD9361_LINK_LIBRARIES})
endif()
