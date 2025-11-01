# ./src/CMakeLists.txt

file(GLOB_RECURSE SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/textedit/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/util/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/wwindows/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/std/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/decoder/*.c"
)

add_executable(NALUTest "${CMAKE_CURRENT_SOURCE_DIR}/decoder/module/h264/nalu.c")
target_link_libraries(NALUTest PRIVATE Sakura Msimg32)

add_executable(FileIOTest "${CMAKE_CURRENT_SOURCE_DIR}/std/fileio.c")
target_link_libraries(FileIOTest PRIVATE Sakura Msimg32)

add_executable(RBSPTest "${CMAKE_CURRENT_SOURCE_DIR}/decoder/module/h264/rbsp.c")
target_link_libraries(RBSPTest PRIVATE Sakura Msimg32)

add_executable(SPSTest "${CMAKE_CURRENT_SOURCE_DIR}/decoder/module/h264/sps.c")
target_link_libraries(SPSTest PRIVATE Sakura Msimg32)

add_executable(SEITest "${CMAKE_CURRENT_SOURCE_DIR}/decoder/module/h264/sei.c")
target_link_libraries(SEITest PRIVATE Sakura Msimg32)

add_library(Sakura STATIC ${SRC_FILES})

