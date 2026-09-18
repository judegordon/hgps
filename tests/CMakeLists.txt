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
     "${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/server/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp")

add_executable(hgps_tests ${HGPS_TEST_SOURCES})
hgps_target_options(hgps_tests)
target_include_directories(hgps_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hgps_tests PRIVATE hgps::internal hgps::cli hgps::server GTest::gtest
                      GTest::gtest_main)

# The synthetic data pack is generated into the build tree at build time rather than committed,
# so a test never reads a stale copy and the repository holds no generated data
# (docs/decisions/0011-data-fetched-not-vendored.md).
set(HGPS_FIXTURE_PACK "${CMAKE_CURRENT_BINARY_DIR}/fixtures/pack")
add_custom_command(
    OUTPUT "${HGPS_FIXTURE_PACK}/data/index.json"
    COMMAND gen-fixtures --output "${HGPS_FIXTURE_PACK}"
    DEPENDS gen-fixtures
    COMMENT "Generating the synthetic data pack"
    VERBATIM)
add_custom_target(hgps_fixtures DEPENDS "${HGPS_FIXTURE_PACK}/data/index.json")
add_dependencies(hgps_tests hgps_fixtures healthgps)

# Paths the tests need, as compile definitions rather than __FILE__ arithmetic. The baseline
# derives its FINCH fixture path from __FILE__ and silently skips 35 tests when it does not
# exist (audit B-11); here the path is supplied by the build and a missing fixture fails.
target_compile_definitions(hgps_tests PRIVATE
    # The CLI as a program rather than as a library: tests/app/cli_run_test.cpp runs it in a working
    # directory of its own, which is the only way to test that a configuration's own output.folder
    # is honoured.
    HGPS_CLI_EXECUTABLE="$<TARGET_FILE:healthgps>"
    HGPS_TEST_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}/fixtures"
    HGPS_FIXTURE_PACK_DIR="${HGPS_FIXTURE_PACK}"
    HGPS_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
    HGPS_UPSTREAM_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/../hgps_main_examples"
    HGPS_SCHEMAS_DIR="${CMAKE_SOURCE_DIR}/schemas"
    HGPS_BINARY_DIR="${CMAKE_BINARY_DIR}"
    HGPS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
gtest_discover_tests(hgps_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 300)

# The equivalence harness is the thing that decides whether this implementation agrees with the
# baseline, so a mistake in it says PASS. Two of its rules have been wrong once each, and both
# times a whole twenty-minute run was what found it. Its own tests run in milliseconds and are
# part of the ordinary suite, so `ctest` covers them and so does scripts/check.sh.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME EquivalenceHarness.Rules
             COMMAND ${Python3_EXECUTABLE} -m unittest discover
                     -s "${CMAKE_CURRENT_SOURCE_DIR}/equivalence" -p "run_test.py" -v)
    set_tests_properties(EquivalenceHarness.Rules PROPERTIES TIMEOUT 120)

    # The two questions unit tests cannot answer about a comparison: does it pass when it should, and
    # does it fail when it should. Both run the real simulation against the synthetic fixture pack —
    # sixty milliseconds a run, so twenty seeds of both sides is seconds — through run.py's own
    # `compare` and `report`. docs/equivalence-method.md §7, ADR 0036.
    add_test(NAME EquivalenceHarness.SelfConsistencyAcrossSeeds
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/equivalence/self_check.py"
                     --mode seeds --example Synthetic --seeds 20
                     --new "$<TARGET_FILE:healthgps>"
                     --workdir "${CMAKE_CURRENT_BINARY_DIR}/self-check/seeds")
    add_test(NAME EquivalenceHarness.DetectsADeliberatePerturbation
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/equivalence/self_check.py"
                     --mode perturbation --example Synthetic --seeds 20
                     --new "$<TARGET_FILE:healthgps>"
                     --workdir "${CMAKE_CURRENT_BINARY_DIR}/self-check/perturbation")
    set_tests_properties(EquivalenceHarness.SelfConsistencyAcrossSeeds
                         EquivalenceHarness.DetectsADeliberatePerturbation
                         PROPERTIES TIMEOUT 600)
else()
    message(WARNING "No Python 3 interpreter: the equivalence harness's own tests will not run.")
endif()
