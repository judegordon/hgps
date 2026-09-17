#include "config_fixture.h"

#include "test_paths.h"

#include <fstream>

namespace hgps::test {

ConfigFixture::ConfigFixture(const std::string &test_name) : dir_{scratch_dir(test_name)} {
    touch("France.DataFile.csv");
    touch("France.FactorsMean.Male.csv");
    touch("France.FactorsMean.Female.csv");
    touch("static_model.json");
    touch("dynamic_model.json");

    document_ = nlohmann::json::parse(R"({
        "$schema": "https://raw.githubusercontent.com/jude/hgps/main/schemas/v2/config.json",
        "version": 2,
        "project_requirements": {
            "demographics": {"age": true, "gender": true, "region": false, "ethnicity": false},
            "income": {"enabled": true, "type": "categorical", "categories": "3",
                       "adjust_to_factors_mean": false, "trended": false,
                       "income_based_csv_output": true},
            "physical_activity": {"enabled": true, "type": "simple",
                                  "adjust_to_factors_mean": false, "trended": false},
            "risk_factors": {"adjust_to_factors_mean": true, "trended": true},
            "trend": {"enabled": false, "type": "null"},
            "two_stage": {"use_logistic": false}
        },
        "data": {"source": "."},
        "inputs": {
            "dataset": {
                "name": "France.DataFile.csv",
                "format": "csv",
                "delimiter": ",",
                "encoding": "ASCII",
                "columns": {"Age": "integer", "Gender": "integer", "BMI": "double"}
            },
            "settings": {"country_code": "FRA", "size_fraction": 0.0001, "age_range": [0, 100]}
        },
        "modelling": {
            "ses_model": {"function_name": "normal", "function_parameters": [0.0, 1.0]},
            "risk_factors": [
                {"name": "Gender", "level": 0, "range": [0, 1]},
                {"name": "Age", "level": 0, "range": [1, 87]},
                {"name": "BMI", "level": 3, "range": [13.88, 39.49]}
            ],
            "risk_factor_models": {"static": "static_model.json", "dynamic": "dynamic_model.json"},
            "baseline_adjustments": {
                "format": "csv",
                "delimiter": ",",
                "encoding": "ASCII",
                "file_names": {
                    "factorsmean_male": "France.FactorsMean.Male.csv",
                    "factorsmean_female": "France.FactorsMean.Female.csv"
                }
            }
        },
        "running": {
            "seed": 123456789,
            "start_time": 2010,
            "stop_time": 2050,
            "trial_runs": 1,
            "diseases": ["alzheimer", "asthma"],
            "interventions": {
                "active_type_id": null,
                "types": {
                    "simple": {
                        "active_period": {"start_time": 2022, "finish_time": 2022},
                        "impact_type": "absolute",
                        "impacts": [
                            {"risk_factor": "BMI", "impact_value": -1.0, "from_age": 0,
                             "to_age": null}
                        ]
                    }
                }
            }
        },
        "output": {"comorbidities": 5, "folder": "results", "file_name": "result.csv"}
    })");
}

std::filesystem::path ConfigFixture::write(const nlohmann::json &document,
                                           const std::string &name) const {
    const auto path = dir_ / name;
    std::ofstream stream{path};
    stream << document.dump(2);
    return path;
}

std::string ConfigFixture::touch(const std::string &name) const {
    std::ofstream stream{dir_ / name};
    return name;
}

} // namespace hgps::test
