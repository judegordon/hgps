find_package(fmt CONFIG REQUIRED)
find_package(jsoncons CONFIG REQUIRED)
find_package(libzippp CONFIG REQUIRED)
find_package(unofficial-curlpp CONFIG REQUIRED)

add_library(hgps_input STATIC "")

target_sources(
    hgps_input
    PRIVATE
        api.h

        config.cpp
        config.h
        config_parsing.cpp
        config_parsing.h
        config_path_parsing.cpp
        config_path_parsing.h
        config_section_parsing.cpp
        config_section_parsing.h

        csvparser.cpp
        csvparser.h

        data_manager.cpp
        data_manager.h
        data_manager_analysis.cpp
        data_manager_csv.cpp
        data_manager_demographic.cpp
        data_manager_disease.cpp
        data_manager_paths.cpp
        data_manager_pif.cpp

        data_source.cpp
        data_source.h

        download_file.cpp
        download_file.h

        ebhlm_model_parser.cpp
        ebhlm_model_parser.h
        hlm_model_parser.cpp
        hlm_model_parser.h
        kevinhall_model_parser.cpp
        kevinhall_model_parser.h
        staticlinear_model_parser.cpp
        staticlinear_model_parser.h

        json_access.h
        json_parser.cpp
        json_parser.h

        model_parser.cpp
        model_parser.h
        model_parser_common.cpp
        model_parser_common.h
        model_repository_registration.cpp
        model_repository_registration.h

        pif_data.cpp
        pif_data.h
        poco.h

        risk_factor_expected_loader.cpp
        risk_factor_expected_loader.h
        riskmodel.h

        schema.cpp
        schema.h

        staticlinear_auxiliary_loader.cpp
        staticlinear_auxiliary_loader.h
        staticlinear_csv_loader.cpp
        staticlinear_csv_loader.h
        staticlinear_trend_loader.cpp
        staticlinear_trend_loader.h

        validated_data_source.cpp
        validated_data_source.h

        zip_file.cpp
        zip_file.h
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