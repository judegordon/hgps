# Workaround for vcpkg issue: https://github.com/microsoft/vcpkg/issues/31617
add_compile_definitions(TBB_USE_ASSERT)

# Include sub-projects.
add_subdirectory(hgps_core)
add_subdirectory(hgps_input)
add_subdirectory(hgps)
add_subdirectory(hgps_console)

if(BUILD_TESTING)
    enable_testing()
    message(STATUS "Building Tests ...")
    add_subdirectory(hgps_tests)
endif()

# PUBLIC needed to make heathers and library available to other project
target_include_directories(hgps_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(hgps_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(hgps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# External include directory
target_include_directories(hgps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external)

# Set the start-up project for the "play" button in MSVC
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT hgps_console)
