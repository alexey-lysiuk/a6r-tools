cmake_minimum_required(VERSION 3.10)

project(a6r-tools LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(thirdparty/serialport)
add_subdirectory(a6r-core)

add_executable(a6r-console
	thirdparty/argparse/argparse.hpp
	thirdparty/stb/stb_image.h
	thirdparty/stb/stb_image_write.h
	a6r-console.cpp
)

target_include_directories(a6r-console PRIVATE
	thirdparty/argparse
	thirdparty/stb
)
target_link_libraries(a6r-console PRIVATE a6r-core)
