cmake_minimum_required(VERSION 3.10)

project(a6r-core LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

add_library(a6r-core
	bmpfile.cpp
	bmpfile.h
	tinysa4.cpp
	tinysa4.h
)

add_subdirectory(serialport)
target_include_directories(a6r-core PRIVATE serialport)
set_target_properties(a6r-core PROPERTIES INTERFACE_LINK_LIBRARIES serialport)
