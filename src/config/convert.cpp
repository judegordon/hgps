#include "convert.h"

#include <algorithm>
#include <fstream>

#include <fmt/format.h>

namespace hgps::config {
namespace {

using nlohmann::json;

/// What a converted config's `$schema` says. This repository has no published URL yet, so the
/// reference is the repository-relative path; the loader checks the suffix and never fetches it.
constexpr const char *kSchemaReference = "schemas/v2/config.json";

void note(ConversionResult &result, ConversionNote::Level level, std::string message) {
    result.notes.push_back(ConversionNote{.level = level, .message = std::move(message)});
}

/// The documented defaults, from schemas/v2/config/project_requirements.json. Spelled out here so
/// a converted config is explicit about every switch rather than relying on the loader's
/// defaults — which is the point of making the block required.
json default_project_requirements() {
    return json{
        {"demographics",
         json{{"age", true}, {"gender", true}, {"region", false}, {"ethnicity", false},
              {"gender2", "male"}}},
        {"income", json{{"enabled", true},
                        {"type", "categorical"},
                        {"categories", "3"},
                        {"adjust_to_factors_mean", false},
                        {"trended", false},
                        {"income_based_csv_output", true}}},
        {"physical_activity", json{{"enabled", true},
                                   {"type", "simple"},
                                   {"adjust_to_factors_mean", false},
                                   {"trended", false}}},
        {"risk_factors", json{{"adjust_to_factors_mean", true}, {"trended", true}}},
        {"trend", json{{"enabled", false}, {"type", "null"}}},
        {"two_stage", json{{"use_logistic", false}}}};
}

/// Fills any block or key the input's project_requirements leaves out, so the result is complete.
json complete_project_requirements(const json &given, ConversionResult &result) {
    auto complete = default_project_requirements();

    for (const auto &block : given.items()) {
        if (!complete.contains(block.key())) {
            note(result, ConversionNote::Level::warning,
                 fmt::format("project_requirements.{} is not a block this build knows; dropped",
                             block.key()));
            continue;
        }

        for (const auto &member : block.value().items()) {
            if (!complete[block.key()].contains(member.key())) {
                // max_age_for_linear_models and logistic_file have no default, so they only
                // appear when the input has them.
                if (member.key() == "max_age_for_linear_models" ||
                    member.key() == "logistic_file") {
                    complete[block.key()][member.key()] = member.value();
                    continue;
                }
                note(result, ConversionNote::Level::warning,
                     fmt::format("project_requirements.{}.{} is not a key this build knows; "
                                 "dropped",
                                 block.key(), member.key()));
                continue;
            }
            complete[block.key()][member.key()] = member.value();
        }
    }

    return complete;
}

/// Derives what the legacy root fields say about the modern block.
json project_requirements_from_legacy(const json &document, ConversionResult &result) {
    auto requirements = default_project_requirements();

    if (document.contains("trend_type") && document["trend_type"].is_string()) {
        const auto type = document["trend_type"].get<std::string>();
        requirements["trend"]["type"] = type;
        requirements["trend"]["enabled"] = type != "null";
        note(result, ConversionNote::Level::info,
             fmt::format("project_requirements.trend taken from the deprecated root "
                         "'trend_type': \"{}\"",
                         type));
    }

    if (document.contains("income_categories") && document["income_categories"].is_string()) {
        const auto categories = document["income_categories"].get<std::string>();
        requirements["income"]["categories"] = categories;
        note(result, ConversionNote::Level::info,
             fmt::format("project_requirements.income.categories taken from the deprecated root "
                         "'income_categories': \"{}\"",
                         categories));
    }

    // What the config's own modelling block implies. These are inferences, and each one is
    // reported as such: a converted config is a draft a human should read, not a fact.
    if (document.contains("modelling") &&
        document["modelling"].contains("demographic_models")) {
        const auto &models = document["modelling"]["demographic_models"];
        if (models.contains("region")) {
            requirements["demographics"]["region"] = true;
            note(result, ConversionNote::Level::info,
                 "demographics.region set to true because modelling.demographic_models has a "
                 "region model");
        }
        if (models.contains("ethnicity")) {
            requirements["demographics"]["ethnicity"] = true;
            note(result, ConversionNote::Level::info,
                 "demographics.ethnicity set to true because modelling.demographic_models has an "
                 "ethnicity model");
        }
        if (models.contains("income")) {
            const auto &income = models["income"];
            if (income.contains("continuous")) {
                requirements["income"]["type"] = "continuous";
                note(result, ConversionNote::Level::info,
                     "income.type set to continuous because modelling.demographic_models.income "
                     "has a continuous model");
            }
        }
        if (models.contains("physical_activity")) {
            requirements["physical_activity"]["type"] = "continuous";
            note(result, ConversionNote::Level::info,
                 "physical_activity.type set to continuous because "
                 "modelling.demographic_models has a physical_activity model");
        }
    }

    note(result, ConversionNote::Level::warning,
         "the input has no 'project_requirements' block and no new_config.json beside it, so the "
         "rest of it comes from the documented defaults in "
         "schemas/v2/config/project_requirements.json. Read the converted block before trusting "
         "the results: upstream gates most current behaviour on it, and no upstream config.json "
         "carries it (audit finding D-03).");

    return requirements;
}

} // namespace

ConversionResult convert_config(const json &document,
                                const std::filesystem::path &source_path) {
    ConversionResult result;

    if (!document.is_object()) {
        note(result, ConversionNote::Level::error, "the input is not a JSON object");
        return result;
    }

    json output;
    output["$schema"] = kSchemaReference;
    output["version"] = 2;

    // 1. project_requirements: from the input, from a sibling new_config.json, or derived.
    if (document.contains("project_requirements")) {
        output["project_requirements"] =
            complete_project_requirements(document["project_requirements"], result);
        note(result, ConversionNote::Level::info,
             "project_requirements taken from the input, with any missing key filled from the "
             "documented defaults");
    } else {
        const auto sibling = source_path.parent_path() / "new_config.json";
        bool taken_from_sibling = false;

        if (source_path.filename() != "new_config.json" &&
            std::filesystem::is_regular_file(sibling)) {
            std::ifstream stream{sibling};
            try {
                const auto other = json::parse(stream);
                if (other.contains("project_requirements")) {
                    output["project_requirements"] =
                        complete_project_requirements(other["project_requirements"], result);
                    note(result, ConversionNote::Level::info,
                         fmt::format("project_requirements taken from {}, which is where "
                                     "upstream keeps it",
                                     sibling.filename().string()));
                    taken_from_sibling = true;
                }
            } catch (const json::parse_error &error) {
                note(result, ConversionNote::Level::warning,
                     fmt::format("{} could not be parsed, so project_requirements was derived "
                                 "instead: {}",
                                 sibling.filename().string(), error.what()));
            }
        }

        if (!taken_from_sibling) {
            output["project_requirements"] = project_requirements_from_legacy(document, result);
        }
    }

    // 2. data, inputs, modelling and output pass through unchanged.
    for (const auto *section : {"data", "inputs", "modelling", "output"}) {
        if (!document.contains(section)) {
            note(result, ConversionNote::Level::error,
                 fmt::format("the input has no '{}' section", section));
            continue;
        }
        output[section] = document[section];
    }

    if (output.contains("data") && output["data"].is_object() &&
        !output["data"].contains("checksum")) {
        const auto source = output["data"].value("source", std::string{});
        if (source.starts_with("http://") || source.starts_with("https://") ||
            source.ends_with(".zip")) {
            note(result, ConversionNote::Level::warning,
                 fmt::format("data.source is '{}' but there is no checksum; config v2 requires "
                             "one for a URL or an archive, so add the release's SHA-256 or point "
                             "at an extracted directory",
                             source));
        }
    }

    // 3. running: the seed becomes a scalar, sync_timeout_ms goes.
    if (!document.contains("running")) {
        note(result, ConversionNote::Level::error, "the input has no 'running' section");
        return result;
    }

    auto running = document["running"];

    if (running.contains("seed")) {
        const auto &seed = running["seed"];
        if (seed.is_array()) {
            if (seed.empty()) {
                note(result, ConversionNote::Level::error,
                     "running.seed is an empty array, which upstream treats as 'run unseeded'. "
                     "Config v2 requires a seed, because an unseeded run cannot be reproduced "
                     "and its results file records the seed as 0 (audit finding B-06). Choose a "
                     "seed and put it here.");
            } else {
                // The count is read before the assignment, because `seed` is a reference into
                // `running` and assigning to running["seed"] replaces what it refers to.
                const auto count = seed.size();
                running["seed"] = seed.front();
                if (count > 1) {
                    note(result, ConversionNote::Level::warning,
                         fmt::format("running.seed had {} values; the first was kept, as upstream "
                                     "does",
                                     count));
                } else {
                    note(result, ConversionNote::Level::info,
                         "running.seed converted from a one-element array to a scalar");
                }
            }
        }
    } else {
        note(result, ConversionNote::Level::error,
             "the input has no running.seed. Config v2 requires one: an unseeded run cannot be "
             "reproduced (audit finding B-06).");
    }

    if (running.contains("sync_timeout_ms")) {
        const auto value = running["sync_timeout_ms"];
        running.erase("sync_timeout_ms");
        note(result, ConversionNote::Level::info,
             fmt::format("running.sync_timeout_ms ({}) removed: scenarios now run one after the "
                         "other and the baseline's figures travel in a journal, so there is "
                         "nothing to wait for",
                         value.dump()));
    }

    output["running"] = running;

    // 4. The deprecated root fields are dropped, having already been read.
    for (const auto *field : {"trend_type", "income_categories"}) {
        if (document.contains(field)) {
            note(result, ConversionNote::Level::info,
                 fmt::format("the deprecated root '{}' was dropped; project_requirements carries "
                             "it now",
                             field));
        }
    }

    // 5. PIF passes through, with a warning if it is switched on: this build rejects it at load.
    if (document.contains("population_impact_fraction")) {
        output["population_impact_fraction"] = document["population_impact_fraction"];
        if (output["population_impact_fraction"].value("enabled", false)) {
            note(result, ConversionNote::Level::warning,
                 "population_impact_fraction.enabled is true, and this build rejects that at "
                 "load: PIF is not implemented yet (docs/backlog.md). The block is kept so the "
                 "config is ready for when it is.");
        }
    }

    // 6. Anything else is reported rather than silently dropped.
    for (const auto &member : document.items()) {
        static const std::vector<std::string> known{
            "$schema",   "$comment", "version",   "project_requirements",
            "data",      "inputs",   "modelling", "running",
            "output",    "trend_type", "income_categories",
            "population_impact_fraction"};

        if (std::find(known.begin(), known.end(), member.key()) == known.end()) {
            note(result, ConversionNote::Level::warning,
                 fmt::format("'{}' is not part of config v1 or v2 and was dropped",
                             member.key()));
        }
    }

    result.document = std::move(output);
    return result;
}

void rebase_input_paths(json &document, const std::filesystem::path &from_directory,
                        const std::filesystem::path &to_directory,
                        std::vector<ConversionNote> &notes) {
    const auto from = std::filesystem::absolute(from_directory);
    const auto to = std::filesystem::absolute(to_directory);

    std::size_t rewritten = 0;

    /// Rewrites one string member if it is present and relative.
    const auto rebase = [&](json *parent, const std::string &key) {
        if (parent == nullptr || !parent->is_object() || !parent->contains(key) ||
            !(*parent)[key].is_string()) {
            return;
        }

        const auto value = (*parent)[key].get<std::string>();
        const std::filesystem::path path{value};
        if (path.is_absolute() || value.starts_with("${")) {
            return;
        }

        const auto target = std::filesystem::weakly_canonical(from / path);
        auto relative = std::filesystem::relative(target, to);
        if (relative.empty()) {
            notes.push_back(ConversionNote{
                .level = ConversionNote::Level::warning,
                .message = fmt::format("'{}' could not be rebased and was left as written", value)});
            return;
        }

        (*parent)[key] = relative.generic_string();
        ++rewritten;
    };

    const auto member = [](json &node, const char *key) -> json * {
        return node.contains(key) && node[key].is_object() ? &node[key] : nullptr;
    };

    if (auto *inputs = member(document, "inputs")) {
        rebase(member(*inputs, "dataset"), "name");
    }

    if (auto *modelling = member(document, "modelling")) {
        if (auto *models = member(*modelling, "risk_factor_models")) {
            for (const auto &key : {"static", "dynamic"}) {
                rebase(models, key);
            }
        }
        if (auto *adjustments = member(*modelling, "baseline_adjustments")) {
            if (auto *names = member(*adjustments, "file_names")) {
                for (const auto &key : {"factorsmean_male", "factorsmean_female"}) {
                    rebase(names, key);
                }
            }
            if (auto *strata = member(*adjustments, "income_stratum_factors_mean")) {
                if (strata->contains("strata") && (*strata)["strata"].is_array()) {
                    for (auto &stratum : (*strata)["strata"]) {
                        for (const auto &key : {"factorsmean_male", "factorsmean_female"}) {
                            rebase(&stratum, key);
                        }
                    }
                }
            }
        }
    }

    if (auto *requirements = member(document, "project_requirements")) {
        if (auto *two_stage = member(*requirements, "two_stage")) {
            rebase(two_stage, "logistic_file");
        }
    }

    notes.push_back(ConversionNote{
        .level = ConversionNote::Level::info,
        .message = fmt::format("{} input path(s) rewritten to stay relative to the upstream "
                               "example at {}; the model CSVs and JSONs are referenced, not "
                               "copied",
                               rewritten, from.string())});
}

} // namespace hgps::config
