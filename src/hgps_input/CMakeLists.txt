find_package(fmt CONFIG REQUIRED)
find_package(jsoncons CONFIG REQUIRED)
find_package(libzippp CONFIG REQUIRED)
find_package(unofficial-curlpp CONFIG REQUIRED)

add_library(hgps_input STATIC)

target_sources(
    hgps_input
    PRIVATE
        config.cpp
        config_parsing.cpp
        config_path_parsing.cpp
        config_section_parsing.cpp
        csv_parser.cpp
        data_manager.cpp
        data_manager_analysis.cpp
        data_manager_csv.cpp
        data_manager_demographic.cpp
        data_manager_disease.cpp
        data_manager_paths.cpp
        data_manager_pif.cpp
        data_source.cpp
        download_file.cpp
        ebhlm_model_parser.cpp
        hlm_model_parser.cpp
        kevinhall_model_parser.cpp
        staticlinear_model_parser.cpp
        json_parser.cpp
        model_parser.cpp
        model_parser_common.cpp
        model_repository_registration.cpp
        pif_data.cpp
        risk_factor_expected_loader.cpp
        schema.cpp
        staticlinear_auxiliary_loader.cpp
        staticlinear_csv_loader.cpp
        staticlinear_trend_loader.cpp
        validated_data_source.cpp
        zip_file.cpp
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