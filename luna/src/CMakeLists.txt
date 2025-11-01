# ./src/CMakeLists.txt

file(GLOB SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/builtin/*.c"
)

add_library(Luna STATIC ${SRC_FILES})

add_subdirectory(test)
