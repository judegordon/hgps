# tools/convert-config : upstream config v1 -> config v2 (docs/decisions/0010-config-v2-and-a-converter.md)
# tools/gen-fixtures   : the synthetic data pack (docs/decisions/0011-data-fetched-not-vendored.md)

file(GLOB CONVERT_CONFIG_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/convert-config/*.cpp")
add_executable(convert-config ${CONVERT_CONFIG_SOURCES})
hgps_target_options(convert-config)
target_link_libraries(convert-config PRIVATE hgps)

file(GLOB GEN_FIXTURES_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gen-fixtures/*.cpp")
add_executable(gen-fixtures ${GEN_FIXTURES_SOURCES})
hgps_target_options(gen-fixtures)
target_link_libraries(gen-fixtures PRIVATE hgps)
