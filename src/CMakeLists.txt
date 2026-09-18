# Three targets, and the boundary between them is the point.
#
#   hgps::engine   the simulation, as a library. Its public API is include/hgps/ and nothing else.
#   hgps::internal an interface target that also exposes src/, for the tests and the tools, which
#                  test the parts rather than the whole.
#   healthgps      the command-line host. Links hgps::engine only, so an include of an internal
#                  header from the CLI is a compile error rather than a review comment.
#
# docs/decisions/0032-library-and-a-thin-cli.md says why the CLI gets no privileged access, and
# docs/decisions/0005-module-layout.md why the library is one target rather than ten.
#
# Sources are globbed with CONFIGURE_DEPENDS: the module directories contain nothing but this
# project's own sources, and re-globbing on build keeps a newly added unit from being silently
# left out of the library.
file(GLOB_RECURSE HGPS_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/random/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/io/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/config/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/data/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/model/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/sim/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/output/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cpp")

add_library(hgps_engine STATIC ${HGPS_SOURCES})
add_library(hgps::engine ALIAS hgps_engine)
hgps_target_options(hgps_engine)

# The public API, and the generated build stamp the manifest records. Both are PUBLIC, because
# include/hgps/engine.h includes include/hgps/version.h which includes the generated header.
hgps_build_info_header(HGPS_GENERATED_INCLUDE_DIR)
target_include_directories(hgps_engine PUBLIC
                           "${CMAKE_SOURCE_DIR}/include"
                           "${HGPS_GENERATED_INCLUDE_DIR}")

# The internals are PRIVATE to the library: src/ is not on the include path of anything that links
# hgps::engine.
target_include_directories(hgps_engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(hgps_engine PUBLIC fmt::fmt nlohmann_json::nlohmann_json)
find_package(Threads REQUIRED)
target_link_libraries(hgps_engine PUBLIC Threads::Threads)

# What the tests and the tools link: the library plus its internal include path. Keeping this
# separate from hgps::engine is what makes the CLI's boundary real — the tests opt in to the
# internals by name, and nothing opts in by accident.
add_library(hgps_internal INTERFACE)
add_library(hgps::internal ALIAS hgps_internal)
target_link_libraries(hgps_internal INTERFACE hgps_engine)
target_include_directories(hgps_internal INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

# The CLI's own code: argument parsing and printing, and nothing else. A library rather than two
# files compiled straight into the executable, so that tests/app/ can test the parsing — which is
# the one thing the CLI still owns, and was untested while it was buried in main.cpp.
add_library(hgps_cli STATIC
            "${CMAKE_CURRENT_SOURCE_DIR}/app/options.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/app/reporter.cpp")
add_library(hgps::cli ALIAS hgps_cli)
hgps_target_options(hgps_cli)
target_include_directories(hgps_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/app")
target_link_libraries(hgps_cli PUBLIC hgps::engine)

add_executable(healthgps "${CMAKE_CURRENT_SOURCE_DIR}/app/main.cpp")
hgps_target_options(healthgps)
target_link_libraries(healthgps PRIVATE hgps::cli)
