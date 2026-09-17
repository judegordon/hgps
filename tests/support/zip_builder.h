#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hgps::test {

/// @brief Writes a minimal ZIP archive with stored (uncompressed) entries.
///
/// Used by the data-source tests so they do not depend on a `zip` binary being installed. The
/// archives it writes are ordinary ZIPs: `unzip`, which is what DataSource actually runs, reads
/// them.
void write_stored_zip(const std::filesystem::path &path,
                      const std::vector<std::pair<std::string, std::string>> &entries);

} // namespace hgps::test
