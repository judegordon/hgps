find_package(fmt CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(Threads REQUIRED)

include(CTest)
include(GoogleTest)

add_executable(hgps_tests "")
target_compile_features(hgps_tests PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

target_sources(
    hgps_tests
    PRIVATE "pch.cpp"
            "pch.h"
            "data_config.h"
            "data_config.cpp"
            "age_gender_table_test.cpp"
            "channel_test.cpp"
            "config_test.cpp"
            "config_schema_expanded_test.cpp"
            "core_array_2d_test.cpp"
            "core_test.cpp"
            "core_univariate_summary_test.cpp"
            "core_math_helper_test.cpp"
            "sha256_test.cpp"
            "datastore_test.cpp"
            "repo_test.cpp"
            "disease_model_test.cpp"
            "event_aggregator_test.cpp"
            "simulation_test.cpp"
            "hierarchical_mapping_test.cpp"
            "json_parser_test.cpp"
            "identifier_test.cpp"
            "life_table_test.cpp"
            "map_2d_test.cpp"
            "population_test.cpp"
            "relative_risk_lookup_test.cpp"
            "runtime_metric_test.cpp"
            "scenario_test.cpp"
            "test_main.cpp"
            "weight_model_test.cpp"
            "country_module.h"
            "risk_factor_data.h"
            "interval_test.cpp"
            "load_nutrient_table.cpp"
            "pif_data_test.cpp"
            "data_manager_pif_test.cpp"
            "disease_model_pif_test.cpp"
            "repo_pif_test.cpp"
            "config_pif_test.cpp"
            "pif_integration_test.cpp")

# Needed for generated version.h from src/hgps
target_include_directories(
    hgps_tests
    PRIVATE "${CMAKE_BINARY_DIR}/src/hgps")

# Download and extract test data
cmake_policy(SET CMP0135 NEW)
include(FetchContent)
FetchContent_Declare(
    test_data
    URL https://github.com/imperialCHEPI/healthgps-data/releases/download/20240624/data_20240624.zip
    URL_HASH SHA256=b68abc8d40b937d377aa7357bff66a1f4f5196da5b867f7ca88d02b37d2ebb5c)
FetchContent_MakeAvailable(test_data)
target_compile_definitions(hgps_tests PRIVATE "-DTEST_DATA_PATH=\"${test_data_SOURCE_DIR}\"")

target_link_libraries(
    hgps_tests
    PRIVATE hgps_core
            hgps_input
            hgps_console_lib
            hgps
            fmt::fmt
            Threads::Threads
            cxxopts::cxxopts
            TBB::tbb
            GTest::gtest)

# We use a long discovery timeout because this can take a while on CI systems
gtest_discover_tests(hgps_tests DISCOVERY_TIMEOUT 30)

install(TARGETS hgps_tests DESTINATION "")
if(WIN32)
    install(IMPORTED_RUNTIME_ARTIFACTS fmt::fmt)
    install(IMPORTED_RUNTIME_ARTIFACTS GTest::gtest)
endif()

# Additional data files required for tests
set(TO_COPY example_file.txt nutrient_table.csv ../../schemas)
foreach(FILE IN LISTS TO_COPY)
    file(COPY "${FILE}" DESTINATION ".")
endforeach()

set(ROOT_NAMESPACE hgps::test)