// New here. The mechanism ruled in docs/decisions/0012-disease-naming-pulmonary.md: the disease
// registry is validated against the directory tree at load time and EVERY mismatch is reported,
// so the upstream `pulmonar`/`pulmonary` inconsistency becomes a diagnostic instead of a run that
// dies part-way through configuration (audit D-01).
#include "data/index.h"
#include "data/store.h"

#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <fstream>

namespace {

using hgps::data::DataIndex;
using hgps::data::Store;
using hgps::diag::IssueCode;
using hgps::diag::IssueReport;

/// Copies the synthetic pack into a scratch directory so a test can break it.
std::filesystem::path copy_pack(const std::string &name) {
    const auto destination = hgps::test::scratch_dir(name) / "pack";
    std::filesystem::copy(hgps::test::synthetic_pack_dir(), destination,
                          std::filesystem::copy_options::recursive);
    return destination;
}

nlohmann::json read_index(const std::filesystem::path &pack) {
    std::ifstream stream{pack / "index.json"};
    return nlohmann::json::parse(stream);
}

void write_index(const std::filesystem::path &pack, const nlohmann::json &document) {
    std::ofstream stream{pack / "index.json", std::ios::trunc};
    stream << document.dump(2);
}

} // namespace

TEST(DataRegistryValidation, AConsistentStoreReportsNothing) {
    IssueReport report;
    const auto store = Store::open(hgps::test::synthetic_pack_dir(), report);

    ASSERT_TRUE(store.has_value()) << report.to_string();
    EXPECT_TRUE(report.empty()) << report.to_string();
}

TEST(DataRegistryValidation, ARegistryEntryWithNoDirectoryIsReported) {
    // This is the `pulmonar` case: the registry names a disease the tree does not have.
    const auto pack = copy_pack("registry_missing_directory");
    auto document = read_index(pack);
    document["diseases"]["registry"].push_back(
        nlohmann::json::parse(R"({"group": "other", "id": "pulmonar", "name": "Mis-spelt"})"));
    write_index(pack, document);

    IssueReport report;
    const auto store = Store::open(pack, report);

    // The store still opens: the mismatch is an input issue for the caller to act on, reported
    // alongside everything else wrong with the inputs, not an exception from a loader.
    ASSERT_TRUE(store.has_value());
    ASSERT_TRUE(report.contains(IssueCode::data_disease_not_in_tree)) << report.to_string();
    EXPECT_NE(std::string::npos, report.to_string().find("pulmonar"));
    EXPECT_TRUE(report.has_errors());
}

TEST(DataRegistryValidation, ADirectoryWithNoRegistryEntryIsReported) {
    const auto pack = copy_pack("registry_missing_entry");
    std::filesystem::create_directories(pack / "diseases" / "phantomitis");

    IssueReport report;
    const auto store = Store::open(pack, report);

    ASSERT_TRUE(store.has_value());
    ASSERT_TRUE(report.contains(IssueCode::data_disease_not_in_registry)) << report.to_string();
    EXPECT_NE(std::string::npos, report.to_string().find("phantomitis"));
}

TEST(DataRegistryValidation, EveryMismatchIsReportedNotJustTheFirst) {
    const auto pack = copy_pack("registry_many_mismatches");

    auto document = read_index(pack);
    document["diseases"]["registry"].push_back(
        nlohmann::json::parse(R"({"group": "other", "id": "ghosta", "name": "A"})"));
    document["diseases"]["registry"].push_back(
        nlohmann::json::parse(R"({"group": "other", "id": "ghostb", "name": "B"})"));
    write_index(pack, document);

    std::filesystem::create_directories(pack / "diseases" / "orphana");
    std::filesystem::create_directories(pack / "diseases" / "orphanb");

    IssueReport report;
    const auto store = Store::open(pack, report);
    ASSERT_TRUE(store.has_value());

    std::size_t not_in_tree = 0;
    std::size_t not_in_registry = 0;
    for (const auto &issue : report.issues()) {
        not_in_tree += issue.code == IssueCode::data_disease_not_in_tree ? 1U : 0U;
        not_in_registry += issue.code == IssueCode::data_disease_not_in_registry ? 1U : 0U;
    }

    EXPECT_EQ(2U, not_in_tree) << report.to_string();
    EXPECT_EQ(2U, not_in_registry) << report.to_string();
}

TEST(DataRegistryValidation, UnregisteredDirectoriesAreReportedInASortedOrder) {
    // The report must read the same way on every file system, whatever order directory iteration
    // happens to give.
    const auto pack = copy_pack("registry_sorted");
    for (const auto *name : {"zeta", "alpha", "mu"}) {
        std::filesystem::create_directories(pack / "diseases" / name);
    }

    IssueReport report;
    ASSERT_TRUE(Store::open(pack, report).has_value());

    std::vector<std::string> reported;
    for (const auto &issue : report.issues()) {
        if (issue.code != IssueCode::data_disease_not_in_registry) {
            continue;
        }
        for (const auto *name : {"alpha", "mu", "zeta"}) {
            if (issue.message.find(name) != std::string::npos) {
                reported.emplace_back(name);
            }
        }
    }

    ASSERT_EQ(3U, reported.size());
    EXPECT_EQ(std::vector<std::string>({"alpha", "mu", "zeta"}), reported);
}

TEST(DataRegistryValidation, MetadataJsonIsCrossCheckedAsAWarning) {
    // Metadata.json is documentation, not something the program reads — but it is where the
    // upstream `pulmonar` spelling lives, and HLM_India's config copied it and fails outright
    // (audit D-01). A warning here says so before anyone copies it again.
    const auto pack = copy_pack("registry_metadata");
    {
        std::ofstream stream{pack / "diseases" / "Metadata.json"};
        stream << R"({"input_file": {"running": {"diseases": ["asthma", "pulmonar"]}}})";
    }

    IssueReport report;
    ASSERT_TRUE(Store::open(pack, report).has_value());

    EXPECT_FALSE(report.has_errors()) << report.to_string();
    ASSERT_EQ(1U, report.warning_count()) << report.to_string();
    EXPECT_EQ(IssueCode::data_disease_not_in_registry, report.issues().front().code);
    EXPECT_NE(std::string::npos, report.issues().front().message.find("pulmonar"));
}

TEST(DataRegistryValidation, AnEmptyOrMalformedRegistryIsRejected) {
    {
        const auto pack = copy_pack("registry_empty");
        auto document = read_index(pack);
        document["diseases"]["registry"] = nlohmann::json::array();
        write_index(pack, document);

        IssueReport report;
        EXPECT_FALSE(Store::open(pack, report).has_value());
        EXPECT_TRUE(report.contains(IssueCode::data_index_invalid));
    }

    {
        const auto pack = copy_pack("registry_not_an_array");
        auto document = read_index(pack);
        document["diseases"]["registry"] = "asthma";
        write_index(pack, document);

        IssueReport report;
        EXPECT_FALSE(Store::open(pack, report).has_value());
        EXPECT_TRUE(report.contains(IssueCode::data_index_invalid));
    }

    // The next two cases leave a usable index — the remaining entries are fine — so the store
    // opens and the problem is an error in the report for the caller to stop on, exactly as a
    // registry-versus-tree mismatch is. `open` returns nullopt only when there is nothing
    // usable to return.
    {
        const auto pack = copy_pack("registry_duplicate");
        auto document = read_index(pack);
        document["diseases"]["registry"].push_back(
            nlohmann::json::parse(R"({"group": "other", "id": "asthma", "name": "Again"})"));
        write_index(pack, document);

        IssueReport report;
        EXPECT_TRUE(Store::open(pack, report).has_value());
        EXPECT_TRUE(report.has_errors());
        ASSERT_TRUE(report.contains(IssueCode::data_index_invalid));
        EXPECT_NE(std::string::npos, report.to_string().find("twice"));
    }

    {
        const auto pack = copy_pack("registry_bad_id");
        auto document = read_index(pack);
        document["diseases"]["registry"].push_back(
            nlohmann::json::parse(R"({"group": "other", "id": "5starts-with-a-digit", "name": "X"})"));
        write_index(pack, document);

        IssueReport report;
        EXPECT_TRUE(Store::open(pack, report).has_value());
        EXPECT_TRUE(report.has_errors());
        EXPECT_TRUE(report.contains(IssueCode::data_index_invalid));
    }
}

TEST(DataRegistryValidation, AMissingIndexSectionIsReportedByName) {
    const auto pack = copy_pack("registry_missing_section");
    auto document = read_index(pack);
    document.erase("analysis");
    document.erase("demographic");
    write_index(pack, document);

    IssueReport report;
    EXPECT_FALSE(Store::open(pack, report).has_value());

    // Both, in one pass.
    EXPECT_EQ(2U, report.error_count()) << report.to_string();
    EXPECT_NE(std::string::npos, report.to_string().find("/analysis"));
    EXPECT_NE(std::string::npos, report.to_string().find("/demographic"));
}

TEST(DataIndexTokens, NamedSubstitution) {
    IssueReport report;
    EXPECT_EQ("P900.csv", DataIndex::substitute_named("P{COUNTRY_CODE}.csv",
                                                      {{"COUNTRY_CODE", "900"}}, "index.json",
                                                      report));
    EXPECT_EQ("male_asthma_bmi.csv",
              DataIndex::substitute_named("{GENDER}_{DISEASE_TYPE}_{RISK_FACTOR}.csv",
                                          {{"GENDER", "male"},
                                           {"DISEASE_TYPE", "asthma"},
                                           {"RISK_FACTOR", "bmi"}},
                                          "index.json", report));
    EXPECT_FALSE(report.has_errors());

    // An unknown token is an error naming the tokens this build knows, rather than a path with a
    // literal brace in it that later fails to open.
    EXPECT_EQ("_.csv", DataIndex::substitute_named("{NONSENSE}_{COUNTRY_CODE}.csv", {},
                                                   "index.json", report));
    EXPECT_TRUE(report.contains(IssueCode::data_index_invalid));
}

TEST(DataIndexTokens, SequentialSubstitutionForTheRepeatedDiseaseToken) {
    // `{DISEASE_TYPE}_{DISEASE_TYPE}.csv` is the one pattern where the same token name stands for
    // two different values in order: the source disease and then the target.
    EXPECT_EQ("diabetes_asthma.csv",
              DataIndex::substitute_sequential("{DISEASE_TYPE}_{DISEASE_TYPE}.csv",
                                               {"diabetes", "asthma"}));

    // Fewer values than tokens leaves the extra tokens empty rather than reading past the end.
    EXPECT_EQ("diabetes_.csv",
              DataIndex::substitute_sequential("{A}_{B}.csv", {"diabetes"}));
}
