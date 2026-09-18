#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace hgps::test {

/// @brief One of the synthetic configurations every test that runs a configuration runs against.
///
/// There are two, and the second exists because of what the first hid. The first pack is tidy in
/// every way a test could come to depend on — `result.csv` in `results/`, model files beside the
/// config, one scenario, all three diseases — and forty-five passing server tests missed a
/// hard-coded output file name because every one of them used it (docs/SUMMARY.md). The second
/// differs on each of those axes at once; `tools/gen-fixtures/model_pack.h` lists them.
///
/// The struct carries **no facts about the pack's contents** on purpose. A test that needs to know
/// the horizon, the disease set or the scenario names asks the loaded configuration, so it cannot
/// assert a constant that only one pack satisfies — which is the mistake the second pack exists to
/// catch.
struct FixturePack {
    /// @brief A short identifier. It is the GoogleTest parameter suffix, and the directory name,
    ///        so it is also the id the server lists the pack under.
    std::string id;

    /// @brief The directory holding `config.json` and whatever else the pack refers to.
    std::filesystem::path directory;

    std::filesystem::path config() const { return directory / "config.json"; }
};

/// @brief Both packs, in a fixed order. `gen-fixtures` writes them into the build tree.
const std::vector<FixturePack> &fixture_packs();

/// @brief The pack id, as a GoogleTest parameter suffix.
std::string pack_suffix(const ::testing::TestParamInfo<FixturePack> &info);

/// @brief A test that runs once per pack.
class FixturePackTest : public ::testing::TestWithParam<FixturePack> {
  protected:
    const FixturePack &pack() const { return GetParam(); }

    /// @brief A scratch directory named after both the test and the pack, so the two
    ///        instantiations of a test never share a folder.
    std::filesystem::path pack_scratch(const std::string &name) const;
};

} // namespace hgps::test

/// @brief Runs a `FixturePackTest` suite against every pack.
///
/// Written as a macro so that adding a third pack is one edit rather than one per suite.
#define HGPS_TEST_EVERY_FIXTURE_PACK(suite)                                                        \
    INSTANTIATE_TEST_SUITE_P(Packs, suite, ::testing::ValuesIn(hgps::test::fixture_packs()),       \
                             hgps::test::pack_suffix)
