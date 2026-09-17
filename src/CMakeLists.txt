# One static library for the simulation, one executable for the CLI
# (docs/decisions/0005-module-layout.md explains why nine modules are not nine targets).
#
# Sources are globbed with CONFIGURE_DEPENDS: the module directories contain nothing but this
# project's own sources, and re-globbing on build keeps a newly added unit from being silently
# left out of the library. docs/backlog.md records switching to explicit lists as a later
# tidy-up if the tree ever acquires files that must not be compiled.
file(GLOB_RECURSE HGPS_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/random/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/io/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/config/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/data/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/model/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/sim/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/output/*.cpp")

add_library(hgps STATIC ${HGPS_SOURCES})
hgps_target_options(hgps)
target_include_directories(hgps PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hgps PUBLIC fmt::fmt nlohmann_json::nlohmann_json)
find_package(Threads REQUIRED)
target_link_libraries(hgps PUBLIC Threads::Threads)

file(GLOB HGPS_APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp")
add_executable(healthgps ${HGPS_APP_SOURCES})
hgps_target_options(healthgps)
target_link_libraries(healthgps PRIVATE hgps)
