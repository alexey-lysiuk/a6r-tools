cmake_minimum_required(VERSION 3.10)

project(a6r-tools LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(a6r-core)

add_executable(a6r-console a6r-console.cpp)
target_include_directories(a6r-console PRIVATE a6r-core)
target_link_libraries(a6r-console PRIVATE a6r-core)
