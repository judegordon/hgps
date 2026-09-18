// New here. The baseline's data-source resolution has no test; its checksum is optional and a
// mismatch is not reported at all (docs/decisions/0011-data-fetched-not-vendored.md).
#include "io/data_source.h"

#include "io/process.h"
#include "io/sha256.h"
#include "support/test_paths.h"
#include "support/zip_builder.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace {

using hgps::diag::IssueCode;
using hgps::io::DataSource;
using hgps::io::DataSourceKind;

/// Points the cache at a scratch directory so a test never writes to a developer's real cache.
class DataSourceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        cache_ = hgps::test::scratch_dir("data_source_cache");
        ::setenv("HEALTHGPS_CACHE_DIR", cache_.string().c_str(), 1);
        work_ = hgps::test::scratch_dir("data_source_work");
    }

    void TearDown() override { ::unsetenv("HEALTHGPS_CACHE_DIR"); }

    std::filesystem::path cache_;
    std::filesystem::path work_;
};

} // namespace

TEST_F(DataSourceTest, ADirectoryResolvesToItself) {
    hgps::diag::IssueReport report;
    const auto data_dir = work_ / "data";
    std::filesystem::create_directories(data_dir);

    const DataSource source{data_dir.string(), std::nullopt, work_};
    EXPECT_EQ(DataSourceKind::directory, source.kind());

    const auto resolved = source.resolve(report);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(data_dir, *resolved);
    EXPECT_FALSE(report.has_errors());
}

TEST_F(DataSourceTest, ARelativeDirectoryIsResolvedAgainstTheConfigDirectory) {
    hgps::diag::IssueReport report;
    std::filesystem::create_directories(work_ / "local-data");

    const DataSource source{"local-data", std::nullopt, work_};
    const auto resolved = source.resolve(report);

    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(work_ / "local-data", *resolved);
}

TEST_F(DataSourceTest, AMissingDirectoryIsReported) {
    hgps::diag::IssueReport report;
    const DataSource source{(work_ / "absent").string(), std::nullopt, work_};

    EXPECT_FALSE(source.resolve(report).has_value());
    EXPECT_TRUE(report.contains(IssueCode::data_source_invalid));
}

TEST_F(DataSourceTest, AnArchiveWithoutAChecksumIsRejected) {
    // The rule the baseline does not have: data that informs model output is not fetched
    // unverified.
    hgps::diag::IssueReport report;
    const auto archive = work_ / "data.zip";
    hgps::test::write_stored_zip(archive, {{"index.json", "{}"}});

    const DataSource source{archive.string(), std::nullopt, work_};
    EXPECT_EQ(DataSourceKind::zip_archive, source.kind());
    EXPECT_FALSE(source.resolve(report).has_value());
    EXPECT_TRUE(report.contains(IssueCode::data_checksum_missing));
}

TEST_F(DataSourceTest, AUrlWithoutAChecksumIsRejectedWithoutTouchingTheNetwork) {
    hgps::diag::IssueReport report;
    const DataSource source{"https://example.invalid/data.zip", std::nullopt, work_};

    EXPECT_EQ(DataSourceKind::url, source.kind());
    EXPECT_FALSE(source.resolve(report).has_value());
    EXPECT_TRUE(report.contains(IssueCode::data_checksum_missing));
}

TEST_F(DataSourceTest, AChecksumThatIsNotAHashIsRejected) {
    hgps::diag::IssueReport report;
    const auto archive = work_ / "data.zip";
    hgps::test::write_stored_zip(archive, {{"index.json", "{}"}});

    const DataSource source{archive.string(), std::string{"not-a-hash"}, work_};
    EXPECT_FALSE(source.resolve(report).has_value());
    EXPECT_TRUE(report.contains(IssueCode::config_bad_value));
}

TEST_F(DataSourceTest, AMismatchedChecksumNamesBothHashes) {
    hgps::diag::IssueReport report;
    const auto archive = work_ / "data.zip";
    hgps::test::write_stored_zip(archive, {{"index.json", "{}"}});

    const std::string wrong(64, 'a');
    const DataSource source{archive.string(), wrong, work_};

    EXPECT_FALSE(source.resolve(report).has_value());
    ASSERT_TRUE(report.contains(IssueCode::data_checksum_mismatch));

    const auto actual = hgps::io::sha256_file(archive);
    const auto &message = report.issues().front().message;
    EXPECT_NE(std::string::npos, message.find(actual));
    EXPECT_NE(std::string::npos, message.find(wrong));
}

TEST_F(DataSourceTest, AVerifiedArchiveExtractsIntoTheContentAddressedCache) {
    if (!hgps::io::tool_available("unzip")) {
        FAIL() << "unzip is not available; it is required to resolve a zip or URL data source";
    }

    hgps::diag::IssueReport report;
    const auto archive = work_ / "data.zip";
    hgps::test::write_stored_zip(archive,
                                 {{"index.json", "{\"country\": {}}"},
                                  {"countries.csv", "Code,Name,Alpha2,Alpha3\n250,France,FR,FRA\n"}});

    const auto hash = hgps::io::sha256_file(archive);
    const DataSource source{archive.string(), hash, work_};

    const auto resolved = source.resolve(report);
    ASSERT_TRUE(resolved.has_value()) << report.to_string();
    EXPECT_FALSE(report.has_errors());

    EXPECT_EQ(DataSource::cache_directory_for(hash), *resolved);
    EXPECT_TRUE(std::filesystem::is_regular_file(*resolved / "index.json"));
    EXPECT_TRUE(std::filesystem::is_regular_file(*resolved / "countries.csv"));

    // Content-addressed: <cache>/data/<first two hex digits>/<the rest>.
    EXPECT_EQ(hash.substr(0, 2), resolved->parent_path().filename().string());
    EXPECT_EQ(hash.substr(2), resolved->filename().string());

    // Resolving again is a cache hit: the archive itself is no longer needed.
    std::filesystem::remove(archive);
    hgps::diag::IssueReport second_report;
    const DataSource again{archive.string(), hash, work_};
    const auto cached = again.resolve(second_report);
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(*resolved, *cached);
    EXPECT_FALSE(second_report.has_errors());
}

TEST_F(DataSourceTest, NoPartialCacheEntryIsLeftBehindWhenExtractionFails) {
    hgps::diag::IssueReport report;

    // A file that is not a zip at all: the checksum matches, extraction cannot.
    const auto archive = work_ / "broken.zip";
    {
        std::ofstream stream{archive, std::ios::binary};
        stream << "this is not a zip archive";
    }

    const auto hash = hgps::io::sha256_file(archive);
    const DataSource source{archive.string(), hash, work_};

    EXPECT_FALSE(source.resolve(report).has_value());
    EXPECT_TRUE(report.contains(IssueCode::data_extract_failed));

    // The cache path must not exist, or the next run would treat the failure as a complete
    // extraction.
    EXPECT_FALSE(std::filesystem::exists(DataSource::cache_directory_for(hash)));
}

TEST(TestIo_DataSource, CacheDirectoryForRejectsThingsThatAreNotHashes) {
    EXPECT_THROW(DataSource::cache_directory_for("abc"), std::invalid_argument);
    EXPECT_THROW(DataSource::cache_directory_for(std::string(63, 'a')), std::invalid_argument);
    EXPECT_NO_THROW(DataSource::cache_directory_for(std::string(64, 'f')));
}
