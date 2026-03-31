add_compile_definitions(TBB_USE_ASSERT)

add_subdirectory(hgps_core)
add_subdirectory(hgps_input)
add_subdirectory(hgps)
add_subdirectory(hgps_console)

target_include_directories(hgps_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(hgps_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(hgps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(hgps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT hgps_console)
