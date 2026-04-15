#include "model_parser.h"

#include "ebhlm_model_parser.h"
#include "hlm_model_parser.h"
#include "kevinhall_model_parser.h"
#include "model_parser_common.h"
#include "staticlinear_model_parser.h"

#include "hgps_core/diagnostics.h"

#include <utility>

namespace hgps::input {

std::unique_ptr<hgps::RiskFactorModelDefinition>
load_risk_model_definition(hgps::RiskFactorModelType model_type,
                           const std::filesystem::path &model_path,
                           const Configuration &config,
                           hgps::core::InputIssueReport &diagnostics) {
    const auto loaded_model = load_and_validate_model_json(model_path, diagnostics);
    if (!loaded_model.has_value()) {
        return nullptr;
    }

    const auto &[model_name, opt] = *loaded_model;
    const auto source_path = model_path.string();

    try {
        switch (model_type) {
        case hgps::RiskFactorModelType::Static:
            if (model_name == "hlm") {
                return load_hlm_risk_model_definition(opt, diagnostics, source_path);
            }

            if (model_name == "staticlinear") {
                return load_staticlinear_risk_model_definition(opt, config, diagnostics,
                                                               source_path);
            }

            diagnostics.error(
                hgps::core::IssueCode::invalid_enum_value,
                {.source_path = source_path, .field_path = "ModelName"},
                "Static model name '" + model_name + "' is not recognised");
            return nullptr;

        case hgps::RiskFactorModelType::Dynamic:
            if (model_name == "ebhlm") {
                return load_ebhlm_risk_model_definition(opt, config, diagnostics, source_path);
            }

            if (model_name == "kevinhall") {
                return load_kevinhall_risk_model_definition(opt, config, diagnostics, source_path);
            }

            diagnostics.error(
                hgps::core::IssueCode::invalid_enum_value,
                {.source_path = source_path, .field_path = "ModelName"},
                "Dynamic model name '" + model_name + "' is not recognised");
            return nullptr;

        default:
            diagnostics.error(hgps::core::IssueCode::invalid_enum_value,
                              {.source_path = source_path, .field_path = "model_type"},
                              "Unknown risk factor model type");
            return nullptr;
        }
    } catch (const std::exception &e) {
        diagnostics.error(hgps::core::IssueCode::parse_failure,
                          {.source_path = source_path},
                          e.what());
        return nullptr;
    } catch (...) {
        diagnostics.error(hgps::core::IssueCode::parse_failure,
                          {.source_path = source_path},
                          "Unknown error while loading risk factor model definition");
        return nullptr;
    }
}

} // namespace hgps::input