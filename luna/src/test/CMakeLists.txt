# ./src/test/CMakeLists.txt

cmake_minimum_required(VERSION 3.29)
project(LunaTest C)

set(CMAKE_C_STANDARD 23)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(LunaTestLexer test_lexer.c)
target_link_libraries(LunaTestLexer
    PRIVATE
    Luna
)

add_executable(LunaTestParser test_parser.c)
target_link_libraries(LunaTestParser
        PRIVATE
        Luna
)

add_executable(LunaTestStdio ../io/stdio.c)
target_link_libraries(LunaTestStdio
        PRIVATE
        Luna
)

