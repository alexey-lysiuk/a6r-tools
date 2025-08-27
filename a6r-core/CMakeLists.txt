cmake_minimum_required(VERSION 3.10)

project(a6r-core LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

add_library(a6r-core
	tinysa4-device.cpp
	tinysa4-device.h
)

target_include_directories(a6r-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(a6r-core PRIVATE serialport)
