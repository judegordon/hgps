#include "analysis_module.h"

#include "analysis/analysis_income.h"
#include "analysis/analysis_series.h"
#include "analysis/analysis_summary.h"
#include "analysis/analysis_tracking.h"
#include "data/converter.h"
#include "models/lms_model.h"
#include "models/weight_model.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace hgps {

inline constexpr double DALY_UNITS = 100'000.0;

AnalysisModule::AnalysisModule(AnalysisDefinition &&definition, WeightModel &&classifier,
                               core::IntegerInterval age_range, unsigned int comorbidities)
    : definition_{std::move(definition)},
      weight_classifier_{std::move(classifier)},
      residual_disability_weight_{create_age_gender_table<double>(age_range)},
      comorbidities_{comorbidities} {}

SimulationModuleType AnalysisModule::type() const noexcept {
    return SimulationModuleType::Analysis;
}

const std::string &AnalysisModule::name() const noexcept {
    return name_;
}

void AnalysisModule::set_income_analysis_enabled(bool enabled) noexcept {
    enable_income_analysis_ = enabled;
}

void AnalysisModule::set_individual_id_tracking_config(
    std::optional<hgps::input::IndividualIdTrackingConfig> config) noexcept {
    individual_id_tracking_config_ = std::move(config);
}

void AnalysisModule::initialise_population(RuntimeContext &context) {
    const auto &age_range = context.inputs().settings().age_range();
    auto expected_sum = create_age_gender_table<double>(age_range);
    auto expected_count = create_age_gender_table<int>(age_range);
    auto &pop = context.population();

    for (const auto &entity : pop) {
        if (!entity.is_active()) {
            continue;
        }

        auto sum = 1.0;
        for (const auto &disease : entity.diseases) {
            if (disease.second.status == DiseaseStatus::active &&
                definition_.disability_weights().contains(disease.first)) {
                sum *= (1.0 - definition_.disability_weights().at(disease.first));
            }
        }

        expected_sum(entity.age, entity.gender) += sum;
        expected_count(entity.age, entity.gender)++;
    }

    for (int age = age_range.lower(); age <= age_range.upper(); ++age) {
        residual_disability_weight_(age, core::Gender::male) = calculate_residual_disability_weight(
            age, core::Gender::male, expected_sum, expected_count);

        residual_disability_weight_(age, core::Gender::female) =
            calculate_residual_disability_weight(age, core::Gender::female, expected_sum,
                                                 expected_count);
    }

    analysis::initialise_output_channels(context, channels_);
    publish_result_message(context);
}

void AnalysisModule::update_population(RuntimeContext &context) {
    publish_result_message(context);
}

double AnalysisModule::calculate_residual_disability_weight(
    int age, const core::Gender gender, const DoubleAgeGenderTable &expected_sum,
    const IntegerAgeGenderTable &expected_count) {
    double residual_value = 0.0;

    if (!expected_sum.contains(age) || !definition_.observed_YLD().contains(age)) {
        return residual_value;
    }

    const auto denominator = expected_count(age, gender);
    if (denominator != 0.0) {
        const auto expected_mean = expected_sum(age, gender) / denominator;
        const auto observed_yld = definition_.observed_YLD()(age, gender);
        residual_value = 1.0 - (1.0 - observed_yld) / expected_mean;

        if (std::isnan(residual_value)) {
            residual_value = 0.0;
        }
    }

    return residual_value;
}

void AnalysisModule::publish_result_message(RuntimeContext &context) const {
    const auto sample_size = context.inputs().settings().age_range().upper() + 1u;
    auto result = ModelResult{sample_size};

    analysis::calculate_population_statistics(definition_, weight_classifier_,
                                              residual_disability_weight_, context, result.series,
                                              channels_);

    analysis::calculate_standard_deviation(definition_, residual_disability_weight_, context,
                                           result.series);

    analysis::calculate_historical_statistics(definition_, residual_disability_weight_,
                                              comorbidities_, enable_income_analysis_, context,
                                              result);

    if (enable_income_analysis_) {
        analysis::calculate_income_based_population_statistics(
            definition_, weight_classifier_, residual_disability_weight_, context, result.series);
    }

    context.publish(std::make_unique<ResultEventMessage>(
        context.scenario().name(), context.current_run(), context.time_now(), result));

    analysis::publish_individual_tracking_if_enabled(context, individual_id_tracking_config_);
}

std::unique_ptr<AnalysisModule> build_analysis_module(Repository &repository,
                                                      const ModelInput &config) {
    auto analysis_entity = repository.manager().get_disease_analysis(config.settings().country());
    auto &lms_definition = repository.get_lms_definition();
    if (lms_definition.empty()) {
        throw std::logic_error("Failed to create analysis module: invalid LMS model definition.");
    }

    auto definition = detail::StoreConverter::to_analysis_definition(analysis_entity);
    auto classifier = WeightModel{LmsModel{lms_definition}};

    auto module =
        std::make_unique<AnalysisModule>(std::move(definition), std::move(classifier),
                                         config.settings().age_range(), config.run().comorbidities);

    module->set_income_analysis_enabled(config.enable_income_analysis());
    module->set_individual_id_tracking_config(config.individual_id_tracking_config());

    return module;
}

} // namespace hgps