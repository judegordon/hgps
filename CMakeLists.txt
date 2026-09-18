cmake_minimum_required(VERSION 3.24)

project(healthgps
        VERSION 0.2.0
        DESCRIPTION "Deterministic reimplementation of the Health-GPS population health microsimulation"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Windows is not a target (docs/decisions/0013-platforms-linux-and-macos.md). Saying so here
# turns a wrong-platform build into one clear message instead of a hundred compile errors.
if(WIN32)
    message(FATAL_ERROR
            "Windows is not a supported platform for this project; see "
            "docs/decisions/0013-platforms-linux-and-macos.md")
endif()

include(cmake/warnings.cmake)
include(cmake/build_info.cmake)
include(CTest)

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(src)
add_subdirectory(tools)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
