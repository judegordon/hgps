#include "pack.h"

#include <cmath>
#include <fstream>
#include <vector>

#include <fmt/format.h>

namespace hgps::tools {
namespace {

struct DiseaseSpec {
    std::string code;
    std::string name;
    std::string group;
};

const std::vector<DiseaseSpec> &diseases() {
    // Three diseases: two ordinary, one cancer, so both disease model families are exercised.
    static const std::vector<DiseaseSpec> list{{"asthma", "Asthma", "other"},
                                               {"diabetes", "Diabetes", "other"},
                                               {"breastcancer", "Breast Cancer", "cancer"}};
    return list;
}

const std::vector<std::string> &risk_factors() {
    static const std::vector<std::string> list{"bmi", "energy"};
    return list;
}

void write(const std::filesystem::path &path, const std::string &contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        throw std::runtime_error(fmt::format("could not write {}", path.string()));
    }
    stream << contents;
}

/// A smooth, bounded shape in [0, 1] used to give every series an age profile without randomness.
double age_shape(int age, int max_age, double offset) {
    const double t = static_cast<double>(age) / static_cast<double>(max_age);
    return 0.5 * (1.0 + std::sin(6.2831853071795864 * (t + offset)));
}

std::string population_csv(const FixturePackSpec &spec) {
    std::string out = "LocID,Location,Time,Age,PopMale,PopFemale,PopTotal\n";
    for (int year = spec.first_year; year <= spec.last_year; ++year) {
        for (int age = 0; age <= spec.max_age; ++age) {
            // A cohort that shrinks with age and grows slowly with time.
            const double growth = 1.0 + 0.004 * (year - spec.first_year);
            const double base = 40'000.0 * std::exp(-0.012 * age) * growth;
            const double males = base * 0.503;
            const double females = base * 0.497;
            out += fmt::format("{},{},{},{},{:.3f},{:.3f},{:.3f}\n", spec.country_code,
                               spec.country_name, year, age, males, females, males + females);
        }
    }
    return out;
}

std::string mortality_csv(const FixturePackSpec &spec) {
    std::string out = "LocID,Location,Time,Age,DeathMale,DeathFemale,DeathTotal\n";
    for (int year = spec.first_year; year <= spec.last_year; ++year) {
        for (int age = 0; age <= spec.max_age; ++age) {
            // A Gompertz-ish hazard, higher for males, applied to the same cohort shape.
            const double growth = 1.0 + 0.004 * (year - spec.first_year);
            const double base = 40'000.0 * std::exp(-0.012 * age) * growth;
            const double hazard = 0.0006 * std::exp(0.075 * age);
            const double males = base * 0.503 * hazard * 1.25;
            const double females = base * 0.497 * hazard;
            out += fmt::format("{},{},{},{},{:.4f},{:.4f},{:.4f}\n", spec.country_code,
                               spec.country_name, year, age, males, females, males + females);
        }
    }
    return out;
}

std::string indicators_csv(const FixturePackSpec &spec) {
    std::string out = "LocID,Location,Time,Births,SRB,Deaths,NetMigrations,LEx,LExMale,LExFemale\n";
    for (int year = spec.first_year; year <= spec.last_year; ++year) {
        const double offset = year - spec.first_year;
        const double births = 9'800.0 + 25.0 * offset;
        const double deaths = 8'400.0 + 30.0 * offset;
        const double migration = 450.0 - 5.0 * offset;
        const double life_expectancy = 79.0 + 0.1 * offset;
        out += fmt::format("{},{},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f}\n",
                           spec.country_code, spec.country_name, year, births, 105.0, deaths,
                           migration, life_expectancy, life_expectancy - 2.5,
                           life_expectancy + 2.5);
    }
    return out;
}

std::string disease_measures_csv(const FixturePackSpec &spec, std::size_t disease_index) {
    // Measure ids follow the upstream convention: 1 prevalence, 2 incidence, 3 remission,
    // 4 mortality.
    std::string out = "measure,measure_id,gender,gender_id,age,mean\n";
    const std::vector<std::pair<std::string, int>> measures{
        {"prevalence", 1}, {"incidence", 2}, {"remission", 3}, {"mortality", 4}};

    const double offset = 0.15 * static_cast<double>(disease_index);

    for (const auto &[measure, measure_id] : measures) {
        for (int gender_id = 1; gender_id <= 2; ++gender_id) {
            for (int age = 0; age <= spec.max_age; ++age) {
                const double shape = age_shape(age, spec.max_age, offset);
                const double sex = gender_id == 1 ? 1.0 : 0.85;
                double value = 0.0;
                switch (measure_id) {
                case 1:
                    value = 0.002 + 0.06 * shape * sex;
                    break;
                case 2:
                    value = 0.0008 + 0.01 * shape * sex;
                    break;
                case 3:
                    value = 0.02 + 0.05 * (1.0 - shape);
                    break;
                default:
                    value = 0.0001 + 0.004 * shape * sex;
                    break;
                }
                out += fmt::format("{},{},{},{},{},{:.8f}\n", measure, measure_id,
                                   gender_id == 1 ? "Male" : "Female", gender_id, age, value);
            }
        }
    }
    return out;
}

/// One population impact fraction table: every (sex, age, year-since-intervention) cell, present
/// exactly once, which is what the loader requires and what the published pack supplies.
///
/// The values rise with the years since the intervention and are larger in middle age, which is the
/// shape a real one has — a policy takes time to work and bites where the risk factor is prevalent.
/// The magnitudes are invented, like everything else in this pack, but they are large enough that a
/// test can see the effect: `Scenario1` reaches 0.25 and `Scenario2` twice that, so a test can also
/// check that choosing a scenario chooses a different table.
std::string population_impact_fraction_csv(const FixturePackSpec &spec, double peak,
                                           int years) {
    std::string csv = "Gender,Age,YearPostInt,IF_Mean\n";
    for (int year = 0; year < years; ++year) {
        // 0 is male and 1 is female, which is what the data and the baseline's loader say — its own
        // schema says the opposite and is wrong (docs/deviations.md B-27).
        for (int sex = 0; sex <= 1; ++sex) {
            for (int age = 0; age <= static_cast<int>(spec.max_age); ++age) {
                const auto ramp = static_cast<double>(year + 1) / static_cast<double>(years);
                const auto mid = 1.0 - std::abs(static_cast<double>(age) - 45.0) / 60.0;
                const auto value = peak * ramp * std::max(0.0, mid) * (sex == 0 ? 1.0 : 0.8);
                csv += fmt::format("{},{},{},{:.8f}\n", sex, age, year, value);
            }
        }
    }
    return csv;
}

std::string relative_risk_to_disease_csv(const FixturePackSpec &spec, std::size_t pair_index) {
    std::string out = "Age,Male,Female\n";
    for (int age = 0; age <= spec.max_age; ++age) {
        const double effect =
            1.0 + 0.4 * age_shape(age, spec.max_age, 0.05 * static_cast<double>(pair_index));
        out += fmt::format("{},{:.6f},{:.6f}\n", age, effect, effect * 0.95);
    }
    return out;
}

std::string relative_risk_to_factor_csv(const FixturePackSpec &spec, const std::string &factor) {
    // Columns: age then one column per factor value band, as the upstream files are shaped.
    std::string out = "Age";
    const int bands = 5;
    for (int band = 0; band < bands; ++band) {
        out += fmt::format(",{}", 10 + band * 10);
    }
    out += "\n";

    for (int age = 0; age <= spec.max_age; ++age) {
        out += fmt::format("{}", age);
        for (int band = 0; band < bands; ++band) {
            const double risk = 1.0 + 0.05 * static_cast<double>(band) *
                                          (1.0 + 0.01 * static_cast<double>(age)) *
                                          (factor == "bmi" ? 1.0 : 0.6);
            out += fmt::format(",{:.6f}", risk);
        }
        out += "\n";
    }
    return out;
}

std::string cancer_lookup_csv(const FixturePackSpec &spec, const std::string &kind) {
    std::string out = "Time,Male,Female\n";
    for (int age = 0; age <= spec.max_age; ++age) {
        double male = 0.0;
        if (kind == "distribution") {
            male = 0.02 + 0.01 * age_shape(age, spec.max_age, 0.2);
        } else if (kind == "survival_rate") {
            male = 0.95 - 0.004 * age;
        } else {
            male = 0.1 + 0.002 * age;
        }
        out += fmt::format("{},{:.6f},{:.6f}\n", age, male, male * 1.05);
    }
    return out;
}

std::string disability_weights_csv() {
    std::string out = "disease,disability_weight\n";
    for (const auto &disease : diseases()) {
        const double weight = disease.group == "cancer" ? 0.29 : 0.12;
        out += fmt::format("{},{:.4f}\n", disease.code, weight);
    }
    return out;
}

std::string lms_csv(const FixturePackSpec &spec) {
    std::string out = "age,gender_id,lambda,mu,sigma\n";
    for (int gender_id = 1; gender_id <= 2; ++gender_id) {
        for (int age = 0; age <= spec.max_age; ++age) {
            const double mu = 15.0 + 8.0 * (1.0 - std::exp(-0.08 * age));
            out += fmt::format("{},{},{:.6f},{:.6f},{:.6f}\n", age, gender_id, -0.5, mu, 0.12);
        }
    }
    return out;
}

std::string observed_yld_csv(const FixturePackSpec &spec) {
    // The upstream layout of analysis/cost/BoD{CODE}.csv: fifteen columns, `mean` at index 12,
    // rows whose measure is YLD. Despite the path, these are observed years lived with
    // disability as a fraction in [0, 1], not costs.
    std::string out = "location_id,location,disease,time,age_group_id,age_group,age,is_filled,"
                      "gender_id,gender,measure_id,measure,mean,lower,upper\n";
    for (int gender_id = 1; gender_id <= 2; ++gender_id) {
        for (int age = 0; age <= spec.max_age; ++age) {
            // A YLD that grows with age and stays well inside [0, 1].
            const double yld =
                (0.02 + 0.004 * static_cast<double>(age)) * (gender_id == 1 ? 1.0 : 0.95);
            out += fmt::format("{},{},All causes,{},5,{} to {},{},False,{},{},3,YLD,{:.8f},{:.8f},"
                               "{:.8f}\n",
                               spec.country_code, spec.country_name, spec.first_year, age, age, age,
                               gender_id, gender_id == 1 ? "Male" : "Female", yld, yld * 0.9,
                               yld * 1.1);
        }
    }
    return out;
}

std::string index_json(const FixturePackSpec &spec) {
    std::string registry;
    for (std::size_t i = 0; i < diseases().size(); ++i) {
        const auto &disease = diseases()[i];
        registry += fmt::format(
            "      {{\n        \"group\": \"{}\",\n        \"id\": \"{}\",\n"
            "        \"name\": \"{}\"\n      }}{}\n",
            disease.group, disease.code, disease.name,
            i + 1 == diseases().size() ? "" : ",");
    }

    return fmt::format(R"({{
  "$comment": "SYNTHETIC data. Generated by tools/gen-fixtures; see SYNTHETIC.md. Same layout as the upstream Health-GPS data store so the loaders are exercised for real, but every number is invented.",
  "country": {{
    "format": "csv",
    "delimiter": ",",
    "encoding": "ASCII",
    "description": "A single fictional country.",
    "source": "synthetic",
    "license": "see SYNTHETIC.md",
    "path": "",
    "file_name": "countries.csv"
  }},
  "demographic": {{
    "format": "csv",
    "delimiter": ",",
    "encoding": "ASCII",
    "description": "Synthetic population, mortality and indicator series.",
    "source": "synthetic",
    "license": "see SYNTHETIC.md",
    "path": "undb",
    "age_limits": [0, {max_age}],
    "time_limits": [{first_year}, {last_year}],
    "projections": {first_year},
    "population": {{
      "description": "Population by sex and single year of age.",
      "path": "population",
      "file_name": "P{{COUNTRY_CODE}}.csv"
    }},
    "mortality": {{
      "description": "Deaths by sex and single year of age.",
      "path": "mortality",
      "file_name": "M{{COUNTRY_CODE}}.csv"
    }},
    "indicators": {{
      "description": "Births, deaths, net migration and life expectancy by year.",
      "path": "indicators",
      "file_name": "Pi{{COUNTRY_CODE}}.csv"
    }}
  }},
  "diseases": {{
    "format": "csv",
    "delimiter": ",",
    "encoding": "ASCII",
    "description": "Synthetic disease measures and relative risks.",
    "source": "synthetic",
    "license": "see SYNTHETIC.md",
    "path": "diseases",
    "age_limits": [0, {max_age}],
    "time_year": {first_year},
    "disease": {{
      "path": "{{DISEASE_TYPE}}",
      "file_name": "D{{COUNTRY_CODE}}.csv",
      "relative_risk": {{
        "path": "relative_risk",
        "to_disease": {{
          "path": "disease",
          "file_name": "{{DISEASE_TYPE}}_{{DISEASE_TYPE}}.csv",
          "default_value": 1.0
        }},
        "to_risk_factor": {{
          "path": "risk_factor",
          "file_name": "{{GENDER}}_{{DISEASE_TYPE}}_{{RISK_FACTOR}}.csv"
        }}
      }},
      "population_impact_fraction": {{
        "description": "Population impact fraction tables, by risk factor and policy scenario.",
        "path": "PIF/{{RISK_FACTOR}}/{{SCENARIO}}",
        "file_name": "IF{{COUNTRY_CODE}}.csv"
      }},
      "parameters": {{
        "path": "P{{COUNTRY_CODE}}",
        "files": {{
          "distribution": "prevalence_distribution.csv",
          "survival_rate": "survival_rate_parameters.csv",
          "death_weight": "death_weights.csv"
        }}
      }}
    }},
    "registry": [
{registry}    ]
  }},
  "analysis": {{
    "format": "csv",
    "delimiter": ",",
    "encoding": "ASCII",
    "description": "Synthetic burden-of-disease inputs.",
    "source": "synthetic",
    "license": "see SYNTHETIC.md",
    "path": "analysis",
    "age_limits": [0, {max_age}],
    "time_year": {first_year},
    "disability_file_name": "disability_weights.csv",
    "lms_file_name": "lms_parameters.csv",
    "cost_of_disease": {{
      "path": "cost",
      "file_name": "BoD{{COUNTRY_CODE}}.csv"
    }}
  }}
}}
)",
                       fmt::arg("max_age", spec.max_age),
                       fmt::arg("first_year", spec.first_year),
                       fmt::arg("last_year", spec.last_year), fmt::arg("registry", registry));
}

std::string synthetic_notice(const FixturePackSpec &spec) {
    return fmt::format(R"(# SYNTHETIC DATA — NOT FOR ANALYSIS

Every number in this directory is **invented**. It was generated by `tools/gen-fixtures` from
closed-form functions of age, year and sex, with no random component and no relationship to any
real population, disease or cost.

## Why it exists

The real Health-GPS data is IHME disease data under CC BY-NC-ND 4.0 and UN population data under
its own terms, and this repository vendors none of it
(`docs/decisions/0011-data-fetched-not-vendored.md`). Tests and CI still need a data store in the
real layout, so this pack provides one. It is small on purpose: one country, {diseases} diseases,
ages 0–{max_age}, years {first_year}–{last_year}.

## What it must never be used for

Anything that produces a number a person might believe. No analysis, no publication, no
calibration, no "rough estimate". A simulation run against this pack tells you that the code
works, and nothing whatsoever about the world.

## Regenerating it

```bash
./out/build/release/tools/gen-fixtures --output tests/fixtures/pack
```

The output is byte-identical every time, which is what lets the reproducibility test compare
result files at all. The country is code {country_code} ("{country_name}"), which is unassigned in
ISO 3166-1 so that nothing here can be mistaken for a real country's data.

## A known artefact of how small it is

The pack's population table stops at age {max_age}, and the synthetic config's `age_range` ends
there too. A person who reaches the top age therefore has no band above them to move into and
leaves the cohort, so the simulated death rate against this pack runs above the rate the mortality
table implies. That is a property of the fixture, not of the simulation: the real data store
carries ages up to 100 with a config that stops below it. Do not read the pack's death rates as a
check on anything, and do not "fix" the simulation to match them.
)",
                       fmt::arg("diseases", diseases().size()), fmt::arg("max_age", spec.max_age),
                       fmt::arg("first_year", spec.first_year),
                       fmt::arg("last_year", spec.last_year),
                       fmt::arg("country_code", spec.country_code),
                       fmt::arg("country_name", spec.country_name));
}

} // namespace

std::size_t write_fixture_pack(const std::filesystem::path &output,
                               const FixturePackSpec &spec) {
    std::size_t written = 0;
    const auto emit = [&written, &output](const std::string &relative,
                                          const std::string &contents) {
        write(output / relative, contents);
        ++written;
    };

    emit("SYNTHETIC.md", synthetic_notice(spec));
    emit("index.json", index_json(spec));
    emit("countries.csv",
         fmt::format("Code,Name,Alpha2,Alpha3\n{},{},{},{}\n", spec.country_code, spec.country_name,
                     spec.alpha2, spec.alpha3));

    emit(fmt::format("undb/population/P{}.csv", spec.country_code), population_csv(spec));
    emit(fmt::format("undb/mortality/M{}.csv", spec.country_code), mortality_csv(spec));
    emit(fmt::format("undb/indicators/Pi{}.csv", spec.country_code), indicators_csv(spec));

    for (std::size_t i = 0; i < diseases().size(); ++i) {
        const auto &disease = diseases()[i];
        const auto directory = fmt::format("diseases/{}", disease.code);

        emit(fmt::format("{}/D{}.csv", directory, spec.country_code),
             disease_measures_csv(spec, i));

        // Disease-to-disease relative risks for every ordered pair, as upstream does.
        for (std::size_t j = 0; j < diseases().size(); ++j) {
            if (i == j) {
                continue;
            }
            emit(fmt::format("{}/relative_risk/disease/{}_{}.csv", directory, disease.code,
                             diseases()[j].code),
                 relative_risk_to_disease_csv(spec, i * diseases().size() + j));
        }

        for (const auto &factor : risk_factors()) {
            for (const auto *gender : {"male", "female"}) {
                emit(fmt::format("{}/relative_risk/risk_factor/{}_{}_{}.csv", directory, gender,
                                 disease.code, factor),
                     relative_risk_to_factor_csv(spec, factor));
            }
        }

        // Population impact fractions for one risk factor and two scenarios, so a test can check
        // both that the mechanism works and that the scenario name selects a different table
        // (ADR 0038). Only `Smoking` exists, so a config naming anything else exercises the
        // "the store does not have that" error, which is the case upstream reports as a silent
        // warning and then runs with no policy at all.
        for (const auto &[scenario, peak] :
             {std::pair{"Scenario1", 0.25}, std::pair{"Scenario2", 0.5}}) {
            emit(fmt::format("{}/PIF/Smoking/{}/IF{}.csv", directory, scenario, spec.country_code),
                 population_impact_fraction_csv(spec, peak, 8));
        }

        if (disease.group == "cancer") {
            emit(fmt::format("{}/P{}/prevalence_distribution.csv", directory, spec.country_code),
                 cancer_lookup_csv(spec, "distribution"));
            emit(fmt::format("{}/P{}/survival_rate_parameters.csv", directory, spec.country_code),
                 cancer_lookup_csv(spec, "survival_rate"));
            emit(fmt::format("{}/P{}/death_weights.csv", directory, spec.country_code),
                 cancer_lookup_csv(spec, "death_weight"));
        }
    }

    emit("analysis/disability_weights.csv", disability_weights_csv());
    emit("analysis/lms_parameters.csv", lms_csv(spec));
    emit(fmt::format("analysis/cost/BoD{}.csv", spec.country_code), observed_yld_csv(spec));

    return written;
}

} // namespace hgps::tools
