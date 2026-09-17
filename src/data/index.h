#pragma once

#include "core/entities.h"
#include "diagnostics/issue_report.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hgps::data {

/// @brief One entry of the data store's disease registry.
struct RegistryEntry {
    core::DiseaseGroup group{};
    core::Identifier code{};
    std::string name;
};

/// @brief The `index.json` manifest that describes a data store's layout.
///
/// Same format as upstream, so an existing data checkout or release archive reads unchanged. What
/// is new is that every problem with it is a located input issue, and that the disease registry is
/// checked against the directory tree (docs/decisions/0012-disease-naming-pulmonary.md).
class DataIndex {
  public:
    /// @brief Reads and validates <root>/index.json.
    /// @return The index, or nullopt if it could not be read or is missing required members.
    static std::optional<DataIndex> load(const std::filesystem::path &root,
                                         diag::IssueReport &report);

    const std::filesystem::path &root() const noexcept { return root_; }

    /// @brief The registry, in the order the file lists it.
    const std::vector<RegistryEntry> &registry() const noexcept { return registry_; }

    std::optional<RegistryEntry> find_disease(const core::Identifier &code) const;

    /// @brief The directory holding the disease subdirectories.
    std::filesystem::path diseases_directory() const;

    /// @brief One disease's directory.
    std::filesystem::path disease_directory(const core::Identifier &code) const;

    /// @brief Checks the registry against the directory tree, reporting every mismatch.
    ///
    /// A registry entry with no directory and a disease directory with no registry entry are both
    /// errors that name the disease and the path. This is what turns the upstream
    /// `pulmonar`/`pulmonary` inconsistency (audit D-01) into a diagnostic at load time instead of
    /// a failure part-way through configuration. If `diseases/Metadata.json` is present it is
    /// cross-checked too, because that is the file whose spelling the broken upstream example
    /// copied.
    void validate_registry_against_tree(diag::IssueReport &report) const;

    /// @brief The raw JSON, for the query loaders in this module.
    const nlohmann::json &node() const noexcept { return document_; }

    /// @brief Substitutes `{NAME}` tokens from a table. An unknown token is an error.
    static std::string substitute_named(const std::string &pattern,
                                        const std::map<std::string, std::string> &tokens,
                                        const std::filesystem::path &file,
                                        diag::IssueReport &report);

    /// @brief Substitutes each `{…}` token with the next value, in order.
    ///
    /// Needed for exactly one pattern — `{DISEASE_TYPE}_{DISEASE_TYPE}.csv`, where the same token
    /// name stands for the source disease and then the target.
    static std::string substitute_sequential(const std::string &pattern,
                                             const std::vector<std::string> &values);

  private:
    DataIndex(std::filesystem::path root, nlohmann::json document);

    std::filesystem::path root_;
    nlohmann::json document_;
    std::vector<RegistryEntry> registry_;

    bool read_registry(diag::IssueReport &report);
};

} // namespace hgps::data
