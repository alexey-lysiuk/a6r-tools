cmake_minimum_required(VERSION 3.15)
project(hackrf-noise VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HACKRF REQUIRED libhackrf)

add_executable(hackrf-noise main.c)
target_include_directories(hackrf-noise PRIVATE ${HACKRF_INCLUDE_DIRS})
target_link_libraries(hackrf-noise PRIVATE ${HACKRF_LIBRARIES})
target_compile_options(hackrf-noise PRIVATE ${HACKRF_CFLAGS_OTHER})
