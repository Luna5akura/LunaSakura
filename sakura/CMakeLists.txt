# ./CMakeLists.txt
cmake_minimum_required(VERSION 3.29)
project(Sakura C)

set(CMAKE_C_STANDARD 23)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_subdirectory(src)

add_executable(SakuraMain main.c)

target_link_libraries(SakuraMain PRIVATE Sakura)

target_link_libraries(Sakura PRIVATE Msimg32)
