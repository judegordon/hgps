// The v1 to v2 converter. The upstream examples are the acceptance tests for it, and they are
// exercised here as files rather than as strings so a change upstream is noticed.
#include "config/convert.h"

#include "config/loader.h"
#include "diagnostics/issue_report.h"
#include "support/test_paths.h"

#include <fstream>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::config::ConversionNote;
using hgps::config::convert_config;
using hgps::config::rebase_input_paths;

nlohmann::json read_json(const std::filesystem::path &path) {
    std::ifstream stream{path};
    EXPECT_TRUE(stream.good()) << "cannot read " << path;
    return nlohmann::json::parse(stream);
}

bool has_note_containing(const std::vector<ConversionNote> &notes, ConversionNote::Level level,
                         std::string_view text) {
    return std::any_of(notes.begin(), notes.end(), [&](const ConversionNote &note) {
        return note.level == level && note.message.find(text) != std::string::npos;
    });
}

/// A minimal upstream v1 config: enough sections that the converter has something to do.
nlohmann::json upstream_v1() {
    return nlohmann::json{
        {"$schema", "https://raw.githubusercontent.com/imperialCHEPI/healthgps/main/schemas/v1/"
                    "config.json"},
        {"version", 2},
        {"data", {{"source", "data"}}},
        {"inputs", {{"dataset", {{"name", "People.csv"}}}}},
        {"modelling", {{"risk_factor_models", {{"static", "static_model.json"}}}}},
        {"running",
         {{"seed", nlohmann::json::array({42})},
          {"start_time", 2010},
          {"stop_time", 2012},
          {"sync_timeout_ms", 15000}}},
        {"output", {{"folder", "results"}}}};
}

} // namespace

TEST(ConvertConfig, MakesTheSeedAScalarAndDropsTheTimeout) {
    const auto result = convert_config(upstream_v1(), "config.json");

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(2, (*result.document)["version"]);
    EXPECT_TRUE((*result.document)["$schema"].get<std::string>().ends_with(
        "schemas/v2/config.json"));

    ASSERT_TRUE((*result.document)["running"]["seed"].is_number());
    EXPECT_EQ(42, (*result.document)["running"]["seed"]);
    EXPECT_FALSE((*result.document)["running"].contains("sync_timeout_ms"));
    EXPECT_TRUE(has_note_containing(result.notes, ConversionNote::Level::info, "sync_timeout_ms"));
}

TEST(ConvertConfig, RefusesAConfigWithNoUsableSeed) {
    // Upstream treats an empty seed array as "seed from entropy", which cannot be reproduced and
    // which the results file then records as seed 0 (audit finding B-06).
    auto empty = upstream_v1();
    empty["running"]["seed"] = nlohmann::json::array();
    const auto from_empty = convert_config(empty, "config.json");
    EXPECT_FALSE(from_empty.succeeded());
    EXPECT_TRUE(has_note_containing(from_empty.notes, ConversionNote::Level::error, "B-06"));

    auto absent = upstream_v1();
    absent["running"].erase("seed");
    const auto from_absent = convert_config(absent, "config.json");
    EXPECT_FALSE(from_absent.succeeded());
    EXPECT_TRUE(has_note_containing(from_absent.notes, ConversionNote::Level::error, "B-06"));
}

TEST(ConvertConfig, KeepsOnlyTheFirstOfSeveralSeeds) {
    auto several = upstream_v1();
    several["running"]["seed"] = nlohmann::json::array({7, 8, 9});
    const auto result = convert_config(several, "config.json");

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(7, (*result.document)["running"]["seed"]);
    EXPECT_TRUE(has_note_containing(result.notes, ConversionNote::Level::warning, "3 values"));
}

TEST(ConvertConfig, FillsProjectRequirementsFromTheDeprecatedRootFields) {
    auto legacy = upstream_v1();
    legacy["trend_type"] = "income_trend";
    legacy["income_categories"] = "5";

    const auto result = convert_config(legacy, "config.json");
    ASSERT_TRUE(result.succeeded());

    const auto &requirements = (*result.document)["project_requirements"];
    EXPECT_EQ("income_trend", requirements["trend"]["type"]);
    EXPECT_EQ(true, requirements["trend"]["enabled"]);
    EXPECT_EQ("5", requirements["income"]["categories"]);
    EXPECT_FALSE(result.document->contains("trend_type"));
    EXPECT_FALSE(result.document->contains("income_categories"));

    // And the rest comes from the documented defaults, which is worth saying out loud.
    EXPECT_EQ("categorical", requirements["income"]["type"]);
    EXPECT_TRUE(has_note_containing(result.notes, ConversionNote::Level::warning, "D-03"));
}

TEST(ConvertConfig, CompletesAProjectRequirementsBlockTheInputAlreadyHas) {
    auto given = upstream_v1();
    given["project_requirements"] = nlohmann::json{{"income", {{"type", "continuous"}}}};

    const auto result = convert_config(given, "config.json");
    ASSERT_TRUE(result.succeeded());

    const auto &requirements = (*result.document)["project_requirements"];
    EXPECT_EQ("continuous", requirements["income"]["type"]);
    EXPECT_EQ("3", requirements["income"]["categories"]); // filled from the default
    EXPECT_TRUE(requirements.contains("two_stage"));
}

TEST(ConvertConfig, TakesProjectRequirementsFromASiblingNewConfig) {
    const auto directory = hgps::test::scratch_dir("convert_sibling");

    nlohmann::json modern;
    modern["project_requirements"] = nlohmann::json{{"trend", {{"enabled", true}}}};
    {
        std::ofstream stream{directory / "new_config.json"};
        stream << modern.dump(2);
    }

    const auto legacy_path = directory / "config.json";
    {
        std::ofstream stream{legacy_path};
        stream << upstream_v1().dump(2);
    }

    const auto result = convert_config(upstream_v1(), legacy_path);
    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(true, (*result.document)["project_requirements"]["trend"]["enabled"]);
    EXPECT_TRUE(
        has_note_containing(result.notes, ConversionNote::Level::info, "new_config.json"));
    // The derived route was not taken, so the "read this before trusting it" warning is absent.
    EXPECT_FALSE(has_note_containing(result.notes, ConversionNote::Level::warning, "D-03"));
}

TEST(ConvertConfig, ReportsAnUnknownRequirementRatherThanCopyingIt) {
    auto given = upstream_v1();
    given["project_requirements"] = nlohmann::json{{"weather", {{"sunny", true}}}};

    const auto result = convert_config(given, "config.json");
    ASSERT_TRUE(result.succeeded());
    EXPECT_FALSE((*result.document)["project_requirements"].contains("weather"));
    EXPECT_TRUE(has_note_containing(result.notes, ConversionNote::Level::warning, "weather"));
}

TEST(ConvertConfig, RejectsAnInputThatIsNotAConfig) {
    EXPECT_FALSE(convert_config(nlohmann::json::array({1, 2}), "config.json").succeeded());

    auto missing = upstream_v1();
    missing.erase("running");
    EXPECT_FALSE(convert_config(missing, "config.json").succeeded());
}

TEST(ConvertConfig, WarnsWhenAnArchiveOrUrlDataSourceHasNoChecksum) {
    auto remote = upstream_v1();
    remote["data"]["source"] = "https://example.invalid/data.zip";
    const auto result = convert_config(remote, "config.json");

    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(has_note_containing(result.notes, ConversionNote::Level::warning, "no checksum"));
}

TEST(ConvertConfig, RebasesEveryInputPathAndLeavesTheOutputFolderAlone) {
    auto document = upstream_v1();
    document["modelling"]["baseline_adjustments"]["file_names"] =
        nlohmann::json{{"factorsmean_male", "Male.csv"}, {"factorsmean_female", "Male.csv"}};
    document["modelling"]["baseline_adjustments"]["income_stratum_factors_mean"]["strata"] =
        nlohmann::json::array({{{"factorsmean_male", "Q1.Male.csv"}}});
    document["output"]["folder"] = "results";

    auto result = convert_config(document, "/upstream/example/config.json");
    ASSERT_TRUE(result.succeeded());

    std::vector<ConversionNote> notes;
    rebase_input_paths(*result.document, "/upstream/example", "/repo/examples/Example", notes);

    const auto &converted = *result.document;
    EXPECT_EQ("../../../upstream/example/People.csv", converted["inputs"]["dataset"]["name"]);
    EXPECT_EQ("../../../upstream/example/static_model.json",
              converted["modelling"]["risk_factor_models"]["static"]);
    EXPECT_EQ("../../../upstream/example/Male.csv",
              converted["modelling"]["baseline_adjustments"]["file_names"]["factorsmean_male"]);
    EXPECT_EQ("../../../upstream/example/Q1.Male.csv",
              converted["modelling"]["baseline_adjustments"]["income_stratum_factors_mean"]
                       ["strata"][0]["factorsmean_male"]);

    // Where the results go is the user's choice, not an input.
    EXPECT_EQ("results", converted["output"]["folder"]);
}

TEST(ConvertConfig, LeavesAbsoluteAndVariablePathsAlone) {
    auto document = upstream_v1();
    document["inputs"]["dataset"]["name"] = "/absolute/People.csv";
    document["modelling"]["risk_factor_models"]["static"] = "${HOME}/static_model.json";

    auto result = convert_config(document, "/upstream/example/config.json");
    ASSERT_TRUE(result.succeeded());

    std::vector<ConversionNote> notes;
    rebase_input_paths(*result.document, "/upstream/example", "/repo/examples/Example", notes);

    EXPECT_EQ("/absolute/People.csv", (*result.document)["inputs"]["dataset"]["name"]);
    EXPECT_EQ("${HOME}/static_model.json",
              (*result.document)["modelling"]["risk_factor_models"]["static"]);
}

// --- The converted upstream examples, as acceptance tests -------------------------------------
//
// These are the files in examples/, produced by tools/convert-config from the read-only upstream
// examples. They are checked in, so this test is what notices if the converter, the loader or
// the upstream examples move apart. The data itself is not loaded here — that needs the release
// archive — so this is a config-level check only.

namespace {

struct ExampleExpectation {
    const char *name;
    bool loads;
    /// Set only for an example this build cannot run: the code its rejection must carry.
    std::optional<hgps::diag::IssueCode> blocking_code;
};

// docs/examples.md carries the same table in prose.
const std::vector<ExampleExpectation> kExamples = {
    {"Dummy_disease_test", true, std::nullopt},
    {"HLM_France", true, std::nullopt},
    {"HLM_India", false, hgps::diag::IssueCode::feature_not_implemented},
    {"KevinHall_FINCH", true, std::nullopt},
    {"KevinHall_India", true, std::nullopt},
    {"KevinHall_PIF", false, hgps::diag::IssueCode::feature_not_implemented},
};

} // namespace

TEST(ConvertedExamples, AreAllPresentAndSayWhatTheyAre) {
    for (const auto &expectation : kExamples) {
        const auto path = hgps::test::examples_dir() / expectation.name / "config.json";
        ASSERT_TRUE(std::filesystem::is_regular_file(path))
            << path << " is missing; run tools/convert-config over the upstream examples";

        const auto document = read_json(path);
        EXPECT_EQ(2, document.value("version", 0)) << expectation.name;
        EXPECT_TRUE(document.contains("project_requirements")) << expectation.name;
        ASSERT_TRUE(document["running"]["seed"].is_number()) << expectation.name;
    }
}

TEST(ConvertedExamples, LoadExactlyWhenThisBuildSupportsThem) {
    for (const auto &expectation : kExamples) {
        const auto path = hgps::test::examples_dir() / expectation.name / "config.json";

        hgps::diag::IssueReport report;
        const auto config = hgps::config::load(path, hgps::config::LoadOptions{}, report);

        EXPECT_EQ(expectation.loads, config.has_value())
            << expectation.name << ":\n"
            << report.to_string();

        if (expectation.blocking_code.has_value()) {
            EXPECT_TRUE(report.contains(*expectation.blocking_code))
                << expectation.name << " should be rejected for a named missing feature:\n"
                << report.to_string();
        }
    }
}

TEST(ConvertedExamples, StillNameTheUpstreamModelFilesTheyWereRebasedOnto) {
    // The model CSVs and JSONs are not copied into this repository: the upstream examples are
    // read-only and the data is not ours to redistribute. So the converted configs point back at
    // them, and this is the test that notices when a path stops resolving.
    for (const auto &expectation : kExamples) {
        const auto directory = hgps::test::examples_dir() / expectation.name;
        const auto document = read_json(directory / "config.json");

        const auto check = [&](const std::filesystem::path &relative) {
            const auto resolved = std::filesystem::weakly_canonical(directory / relative);
            EXPECT_TRUE(std::filesystem::is_regular_file(resolved))
                << expectation.name << ": " << relative << " does not resolve to a file";
            const auto upstream =
                std::filesystem::weakly_canonical(hgps::test::upstream_examples_dir());
            EXPECT_TRUE(resolved.string().starts_with(upstream.string()))
                << expectation.name << ": " << relative << " should point into the upstream "
                << "examples, not into this repository";
        };

        check(document["inputs"]["dataset"]["name"].get<std::string>());
        for (const auto &role : {"static", "dynamic"}) {
            if (document["modelling"]["risk_factor_models"].contains(role)) {
                check(document["modelling"]["risk_factor_models"][role].get<std::string>());
            }
        }
        for (const auto &file : document["modelling"]["baseline_adjustments"]["file_names"]) {
            check(file.get<std::string>());
        }
    }
}
