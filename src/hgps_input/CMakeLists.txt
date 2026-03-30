find_package(fmt CONFIG REQUIRED)
find_package(jsoncons CONFIG REQUIRED)
find_package(libzippp CONFIG REQUIRED)
find_package(unofficial-curlpp CONFIG REQUIRED)

add_library(hgps_input STATIC)

target_sources(
    hgps_input
    PRIVATE
        config/config.cpp
        config/config_parsing.cpp
        config/config_path_parsing.cpp
        config/config_section_parsing.cpp
        config/schema.cpp

        data/data_manager.cpp
        data/data_manager_analysis.cpp
        data/data_manager_csv.cpp
        data/data_manager_demographic.cpp
        data/data_manager_disease.cpp
        data/data_manager_paths.cpp
        data/data_manager_pif.cpp
        data/data_source.cpp
        data/pif_data.cpp
        data/validated_data_source.cpp

        io/csv_parser.cpp
        io/download_file.cpp
        io/json_parser.cpp
        io/zip_file.cpp

        models/ebhlm_model_parser.cpp
        models/hlm_model_parser.cpp
        models/kevinhall_model_parser.cpp
        models/model_parser.cpp
        models/model_parser_common.cpp
        models/model_repository_registration.cpp
        models/risk_factor_expected_loader.cpp
        models/staticlinear_auxiliary_loader.cpp
        models/staticlinear_csv_loader.cpp
        models/staticlinear_model_parser.cpp
        models/staticlinear_trend_loader.cpp
)

target_include_directories(
    hgps_input
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
    hgps_input
    PRIVATE
        hgps
        hgps_core
        fmt::fmt
        jsoncons
        libzippp::libzippp
        unofficial::curlpp::curlpp
)