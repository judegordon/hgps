#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace hgps::test {

/// @brief A valid config v2 document, and the scratch directory holding the files it names.
///
/// The baseline's config tests build fragments per test; this builds one complete valid document
/// so that a test can state exactly what it breaks, which is also what the schema-agreement test
/// needs.
class ConfigFixture {
  public:
    explicit ConfigFixture(const std::string &test_name);

    const std::filesystem::path &dir() const noexcept { return dir_; }

    /// @brief A complete, valid config v2 document whose referenced files all exist.
    nlohmann::json document() const { return document_; }

    /// @brief Writes a document to <dir>/config.json and returns the path.
    std::filesystem::path write(const nlohmann::json &document,
                                const std::string &name = "config.json") const;

    /// @brief Creates an empty file in the fixture directory and returns its name.
    std::string touch(const std::string &name) const;

  private:
    std::filesystem::path dir_;
    nlohmann::json document_;
};

} // namespace hgps::test
