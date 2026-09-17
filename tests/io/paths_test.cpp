// New here. The env-expansion behaviour differs from the baseline on purpose: an undefined
// variable is reported rather than expanded to an empty string (audit N-17), which otherwise
// quietly relocates a run's output.
#include "io/paths.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

TEST(TestIo_Paths, ProgramPathIsTheTestBinary) {
    const auto path = hgps::io::program_path();
    EXPECT_TRUE(std::filesystem::exists(path)) << path.string();
    EXPECT_EQ(path.parent_path(), hgps::io::program_directory());
    EXPECT_NE(std::string::npos, path.filename().string().find("hgps_tests"));
}

TEST(TestIo_Paths, CacheDirectoryHonoursTheOverride) {
    ::setenv("HEALTHGPS_CACHE_DIR", "/tmp/hgps-cache-under-test", 1);
    EXPECT_EQ(std::filesystem::path{"/tmp/hgps-cache-under-test"}, hgps::io::cache_directory());
    ::unsetenv("HEALTHGPS_CACHE_DIR");

    // Without the override it is a real per-user cache path, and it mentions the program.
    const auto path = hgps::io::cache_directory();
    EXPECT_NE(std::string::npos, path.string().find("healthgps"));
}

TEST(TestIo_Paths, ExpandsDefinedVariables) {
    ::setenv("HGPS_TEST_VAR", "/data/root", 1);

    std::vector<std::string> undefined;
    EXPECT_EQ("/data/root/diseases",
              hgps::io::expand_environment_variables("${HGPS_TEST_VAR}/diseases", undefined));
    EXPECT_TRUE(undefined.empty());

    EXPECT_EQ("/data/root/data/root",
              hgps::io::expand_environment_variables("${HGPS_TEST_VAR}${HGPS_TEST_VAR}",
                                                     undefined));
    EXPECT_TRUE(undefined.empty());

    ::unsetenv("HGPS_TEST_VAR");
}

TEST(TestIo_Paths, ReportsUndefinedVariablesInsteadOfSwallowingThem) {
    ::unsetenv("HGPS_DEFINITELY_NOT_SET");

    std::vector<std::string> undefined;
    const auto result =
        hgps::io::expand_environment_variables("${HGPS_DEFINITELY_NOT_SET}/results", undefined);

    ASSERT_EQ(1U, undefined.size());
    EXPECT_EQ("HGPS_DEFINITELY_NOT_SET", undefined.front());

    // The text is left as the user wrote it, so the diagnostic can quote it back to them.
    EXPECT_EQ("${HGPS_DEFINITELY_NOT_SET}/results", result);
}

TEST(TestIo_Paths, LeavesTextWithNoVariablesAlone) {
    std::vector<std::string> undefined;
    EXPECT_EQ("/plain/path", hgps::io::expand_environment_variables("/plain/path", undefined));
    EXPECT_EQ("${unterminated", hgps::io::expand_environment_variables("${unterminated", undefined));
    EXPECT_EQ("100% done", hgps::io::expand_environment_variables("100% done", undefined));
    EXPECT_TRUE(undefined.empty());
}

TEST(TestIo_Paths, TemporaryDirectoryExists) {
    const auto path = hgps::io::temporary_directory();
    EXPECT_TRUE(std::filesystem::is_directory(path));
}
