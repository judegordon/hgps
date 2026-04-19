find_package(fmt CONFIG REQUIRED)
find_package(crossguid CONFIG REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)
find_package(platform_folders CONFIG REQUIRED)

add_library(hgps STATIC)

configure_file(version.h.in version.h ESCAPE_QUOTES)

target_sources(
    hgps
    PRIVATE
        analysis/analysis_income.cpp
        analysis/analysis_series.cpp
        analysis/analysis_summary.cpp
        analysis/analysis_tracking.cpp

        simulation/analysis_module.cpp
        simulation/model_result.cpp
        simulation/module_factory.cpp
        simulation/runner.cpp
        simulation/runtime_context.cpp
        simulation/runtime_metric.cpp
        simulation/simulation.cpp
        simulation/simulation_module.cpp

        scenarios/baseline_scenario.cpp
        scenarios/fiscal_scenario.cpp
        scenarios/food_labelling_scenario.cpp
        scenarios/marketing_dynamic_scenario.cpp
        scenarios/marketing_scenario.cpp
        scenarios/physical_activity_scenario.cpp
        scenarios/simple_policy_scenario.cpp

        models/default_cancer_model.cpp
        models/default_disease_model.cpp
        models/dynamic_hierarchical_linear_model.cpp
        models/kevin_hall_model.cpp
        models/lms_model.cpp
        models/risk_factor_adjustable_model.cpp
        models/ses_noise_module.cpp
        models/static_hierarchical_linear_model.cpp
        models/static_linear_model.cpp
        models/univariate_visitor.cpp
        models/weight_model.cpp

        disease/disease.cpp
        disease/disease_table.cpp
        disease/life_table.cpp
        disease/relative_risk.cpp

        data/converter.cpp
        data/data_series.cpp
        data/demographic.cpp
        data/mapping.cpp
        data/model_input.cpp
        data/nutrients.cpp
        data/person.cpp
        data/population.cpp
        data/repository.cpp
        data/riskfactor.cpp

        events/error_message.cpp
        events/event_bus.cpp
        events/event_subscriber.cpp
        events/individual_tracking_message.cpp
        events/info_message.cpp
        events/result_message.cpp
        events/runner_message.cpp

        utils/mt_random.cpp
        utils/program_dirs.cpp
        utils/random_algorithm.cpp
        utils/settings.cpp
        utils/sha256.cpp
)

target_link_libraries(
    hgps
    PRIVATE
        hgps_core
        fmt::fmt
        crossguid
        OpenSSL::Crypto
        sago::platform_folders
)

target_include_directories(
    hgps
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)