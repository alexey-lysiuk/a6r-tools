cmake_minimum_required(VERSION 3.15)
project(libresdr-noise VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBIIO REQUIRED libiio)
pkg_check_modules(LIBAD9361 REQUIRED libad9361)

add_executable(libresdr-noise main.c)
target_compile_options(libresdr-noise PRIVATE ${LIBIIO_CFLAGS_OTHER} ${LIBAD9361_CFLAGS_OTHER})
target_include_directories(libresdr-noise PRIVATE ${LIBIIO_INCLUDE_DIRS} ${LIBAD9361_INCLUDE_DIRS})
target_link_libraries(libresdr-noise PRIVATE ${LIBIIO_LINK_LIBRARIES} ${LIBAD9361_LINK_LIBRARIES})
