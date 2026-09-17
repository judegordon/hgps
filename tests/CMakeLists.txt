# The test suite. Ports of the baseline's 471 tests plus the tests the baseline lacks
# (docs/decisions/0006-validation-strategy.md).
#
# tests/equivalence/ is a script-driven harness against the baseline binary, not a CTest target:
# it needs the upstream data and a built baseline. scripts/check.sh runs it.

find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE HGPS_TEST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/random/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/io/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/config/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/data/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/model/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/sim/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/output/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp")

add_executable(hgps_tests ${HGPS_TEST_SOURCES})
hgps_target_options(hgps_tests)
target_include_directories(hgps_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hgps_tests PRIVATE hgps GTest::gtest GTest::gtest_main)

# Paths the tests need, as compile definitions rather than __FILE__ arithmetic. The baseline
# derives its FINCH fixture path from __FILE__ and silently skips 35 tests when it does not
# exist (audit B-11); here the path is supplied by the build and a missing fixture fails.
target_compile_definitions(hgps_tests PRIVATE
    HGPS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    HGPS_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
    HGPS_UPSTREAM_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/../hgps_main_examples"
    HGPS_SCHEMAS_DIR="${CMAKE_SOURCE_DIR}/schemas"
    HGPS_BINARY_DIR="${CMAKE_BINARY_DIR}")

include(GoogleTest)
gtest_discover_tests(hgps_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 300)
