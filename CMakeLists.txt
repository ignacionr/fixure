cmake_minimum_required(VERSION 3.20)
project(fixure VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -O3)
endif()

# Header-only or library? Let's make it a library
add_library(fixure
    src/parser.cpp
    src/dictionary.cpp
    src/session.cpp
    src/scenario.cpp
    src/runner.cpp
    src/replay.cpp
)

target_include_directories(fixure PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# CLI application
add_executable(fixure_cli
    app/main.cpp
)
target_link_libraries(fixure_cli PRIVATE fixure)
set_target_properties(fixure_cli PROPERTIES OUTPUT_NAME fixure)

# Unit tests
enable_testing()
add_executable(fixure_tests
    tests/test_main.cpp
    tests/test_parser.cpp
    tests/test_dictionary.cpp
    tests/test_session.cpp
    tests/test_scenario.cpp
)
target_link_libraries(fixure_tests PRIVATE fixure)
add_test(NAME fixure_unit_tests COMMAND fixure_tests)
