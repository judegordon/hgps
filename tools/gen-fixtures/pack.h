#pragma once

#include <filesystem>
#include <string>

namespace hgps::tools {

/// @brief Description of the synthetic data pack this tool writes.
struct FixturePackSpec {
    /// @brief The fictional country. Code 900 is unassigned in ISO 3166-1, so nothing here can be
    ///        mistaken for a real country's data.
    int country_code{900};
    std::string country_name{"Synthland"};
    std::string alpha2{"SL"};
    std::string alpha3{"SYN"};

    int first_year{2010};
    int last_year{2020};
    int max_age{49};
};

/// @brief Writes the pack. Every number is a closed-form function of (age, year, sex), so the
///        pack is byte-identical every time it is generated — which is what lets a test compare
///        output files at all.
/// @return The number of files written.
std::size_t write_fixture_pack(const std::filesystem::path &output, const FixturePackSpec &spec);

} // namespace hgps::tools
