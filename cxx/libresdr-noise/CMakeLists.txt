cmake_minimum_required(VERSION 3.15)
project(libresdr-noise VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRFNM REQUIRED librfnm)

add_executable(libresdr-noise main.cpp)
target_compile_options(libresdr-noise PRIVATE ${LIBRFNM_CFLAGS_OTHER})
target_include_directories(libresdr-noise PRIVATE ${LIBRFNM_INCLUDE_DIRS})
target_link_libraries(libresdr-noise PRIVATE ${LIBRFNM_LINK_LIBRARIES})
