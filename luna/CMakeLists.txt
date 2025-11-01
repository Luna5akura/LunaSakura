# ./CMakelistst.txt

cmake_minimum_required(VERSION 3.29)
project(Luna C)

set(CMAKE_C_STANDARD 23)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_subdirectory(src)

add_executable(LunaMain main.c)

target_link_libraries(LunaMain PRIVATE Luna)
