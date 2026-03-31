cmake_minimum_required(VERSION 3.20)

project(
    Health-GPS
    VERSION 1.0.0
    LANGUAGES CXX
)

option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(GENERATE_COVERAGE "Generate code coverage information" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED CATCH_EXCEPTIONS AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CATCH_EXCEPTIONS TRUE)
endif()

if(CATCH_EXCEPTIONS)
    add_compile_definitions(CATCH_EXCEPTIONS)
endif()

include(cmake/OutOfSource.cmake)

message(STATUS "Compiler ID: ${CMAKE_CXX_COMPILER_ID}")

if(MSVC)
    add_compile_options(/W4 /permissive- /bigobj)

    if(WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()

    if(GENERATE_COVERAGE)
        message(WARNING "Code coverage cannot be generated for MSVC, ignoring")
    endif()

    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)

    if(WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()

    if(GENERATE_COVERAGE)
        add_compile_options(-O0 -g --coverage)
        add_link_options(--coverage)
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fdiagnostics-color=always)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcolor-diagnostics)
    endif()
endif()

add_subdirectory(src)