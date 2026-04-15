find_package(fmt CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_path(RAPIDCSV_INCLUDE_DIRS "rapidcsv.h")
find_package(Threads REQUIRED)

add_executable(hgps_console "program.cpp")
add_library(
    hgps_console_lib STATIC
    "command_options.cpp"
    "event_monitor.cpp"
    "individual_id_tracking_writer.cpp"
    "program.cpp"
    "result_file_writer.cpp"
)

# Under Windows, we also include a resource file to the build
if(WIN32)
    # Make sure that the resource file is seen as an RC file to be compiled with a resource
    # compiler, not a C++ compiler
    set_source_files_properties(versioninfo.rc LANGUAGE RC)
    target_sources(hgps_console PRIVATE versioninfo.rc)
endif()

target_include_directories(hgps_console_lib PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(
    hgps_console
    PRIVATE hgps_core
            hgps_input
            hgps_console_lib
            hgps
            fmt::fmt
            Threads::Threads
            cxxopts::cxxopts
            TBB::tbb)

target_link_libraries(
    hgps_console_lib
    PRIVATE hgps_core
            hgps_input
            hgps
            fmt::fmt
            Threads::Threads
            cxxopts::cxxopts
            TBB::tbb)

file(GLOB_RECURSE HGPS_SCHEMA_FILES
    "${PROJECT_SOURCE_DIR}/schemas/*.json"
)

set(HGPS_SCHEMA_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/schemas"
)

add_custom_target(copy_schemas ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${HGPS_SCHEMA_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HGPS_SCHEMA_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/schemas"
            "${HGPS_SCHEMA_OUTPUT_DIR}"
    DEPENDS ${HGPS_SCHEMA_FILES}
    COMMENT "Copying schema files to build tree"
)

add_dependencies(hgps_console copy_schemas)

install(TARGETS hgps_console
        RUNTIME DESTINATION bin)

if(WIN32)
    install(
        TARGETS hgps_console
        COMPONENT hgps_console
        RUNTIME_DEPENDENCIES
        PRE_EXCLUDE_REGEXES
        "api-ms-"
        "ext-ms-"
        POST_EXCLUDE_REGEXES
        ".*system32/.*\\.dll"
        DIRECTORIES
        $<TARGET_FILE_DIR:hgps_console>)
endif()

# Install schemas next to the installed executable
install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/schemas
    DESTINATION bin
)