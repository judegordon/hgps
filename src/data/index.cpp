#include "index.h"

#include "core/string_util.h"
#include "io/json.h"

#include <algorithm>
#include <set>
#include <utility>

#include <fmt/format.h>

namespace hgps::data {
namespace {

using diag::IssueCode;
using diag::IssueLocation;

constexpr const char *kIndexFileName = "index.json";
constexpr const char *kMetadataFileName = "Metadata.json";

} // namespace

// Parentheses, not braces: `nlohmann::json x{y}` where y is a json invokes the initializer-list
// constructor and quietly wraps it in an array. This cost an hour once; it is worth a comment.
DataIndex::DataIndex(std::filesystem::path root, nlohmann::json document)
    : root_{std::move(root)}, document_(std::move(document)) {}

std::optional<DataIndex> DataIndex::load(const std::filesystem::path &root,
                                         diag::IssueReport &report) {
    const auto path = root / kIndexFileName;
    if (!std::filesystem::is_directory(root)) {
        report.error(IssueCode::data_source_invalid, IssueLocation{.file = root.string()},
                     "the data store is not a directory");
        return std::nullopt;
    }

    auto document = io::read_json(path, report);
    if (!document.has_value()) {
        return std::nullopt;
    }

    DataIndex index{root, std::move(*document)};

    // The sections the queries in this module need. Reported together, so a store missing three
    // of them says so once.
    const auto before = report.error_count();
    for (const auto *section : {"country", "demographic", "diseases", "analysis"}) {
        if (!index.document_.contains(section) || !index.document_[section].is_object()) {
            report.error(IssueCode::data_index_invalid,
                         IssueLocation{.file = path.string(), .field = fmt::format("/{}", section)},
                         fmt::format("'{}' is required and must be an object", section));
        }
    }

    if (report.error_count() != before) {
        return std::nullopt;
    }

    if (!index.read_registry(report)) {
        return std::nullopt;
    }

    return index;
}

bool DataIndex::read_registry(diag::IssueReport &report) {
    const auto path = (root_ / kIndexFileName).string();
    const auto &diseases = document_["diseases"];

    if (!diseases.contains("registry") || !diseases["registry"].is_array()) {
        report.error(IssueCode::data_index_invalid,
                     IssueLocation{.file = path, .field = "/diseases/registry"},
                     "'registry' is required and must be an array of {group, id, name} objects");
        return false;
    }

    std::set<std::string> seen;
    std::size_t position = 0;
    for (const auto &item : diseases["registry"]) {
        const auto field = fmt::format("/diseases/registry/{}", position++);

        if (!item.is_object() || !item.contains("id") || !item["id"].is_string()) {
            report.error(IssueCode::data_index_invalid,
                         IssueLocation{.file = path, .field = field},
                         "each registry entry needs a string 'id'");
            continue;
        }

        RegistryEntry entry;
        const auto id = core::to_lower(item["id"].get<std::string>());

        try {
            entry.code = core::Identifier{id};
        } catch (const std::invalid_argument &error) {
            // The boundary conversion permitted by ADR 0018: an identifier the registry cannot
            // name becomes a located issue rather than an exception out of a data loader.
            report.error(IssueCode::data_index_invalid,
                         IssueLocation{.file = path, .field = field},
                         fmt::format("'{}' is not a valid disease code: {}", id, error.what()));
            continue;
        }

        entry.name = item.value("name", id);
        const auto group = core::to_lower(item.value("group", std::string{"other"}));
        entry.group = group == "cancer" ? core::DiseaseGroup::cancer : core::DiseaseGroup::other;

        if (!seen.insert(id).second) {
            report.error(IssueCode::data_index_invalid,
                         IssueLocation{.file = path, .field = field},
                         fmt::format("disease '{}' appears twice in the registry", id));
            continue;
        }

        registry_.push_back(std::move(entry));
    }

    if (registry_.empty()) {
        report.error(IssueCode::data_index_invalid,
                     IssueLocation{.file = path, .field = "/diseases/registry"},
                     "the disease registry is empty");
        return false;
    }

    return true;
}

std::optional<RegistryEntry> DataIndex::find_disease(const core::Identifier &code) const {
    const auto it = std::find_if(registry_.begin(), registry_.end(),
                                 [&code](const RegistryEntry &entry) { return entry.code == code; });
    if (it == registry_.end()) {
        return std::nullopt;
    }
    return *it;
}

std::filesystem::path DataIndex::diseases_directory() const {
    return root_ / document_["diseases"].value("path", std::string{"diseases"});
}

std::filesystem::path DataIndex::disease_directory(const core::Identifier &code) const {
    return diseases_directory() / code.to_string();
}

void DataIndex::validate_registry_against_tree(diag::IssueReport &report) const {
    const auto path = (root_ / kIndexFileName).string();
    const auto diseases_root = diseases_directory();

    if (!std::filesystem::is_directory(diseases_root)) {
        report.error(IssueCode::data_missing_file, IssueLocation{.file = diseases_root.string()},
                     "the diseases directory named by index.json does not exist");
        return;
    }

    // Registry entries with no directory.
    for (const auto &entry : registry_) {
        const auto directory = disease_directory(entry.code);
        if (!std::filesystem::is_directory(directory)) {
            report.error(
                IssueCode::data_disease_not_in_tree,
                IssueLocation{.file = path, .field = "/diseases/registry"},
                fmt::format("the registry lists disease '{}' ({}) but {} does not exist",
                            entry.code.to_string(), entry.name, directory.string()));
        }
    }

    // Directories with no registry entry.
    std::set<std::string> registered;
    for (const auto &entry : registry_) {
        registered.insert(entry.code.to_string());
    }

    std::vector<std::string> unregistered;
    for (const auto &item : std::filesystem::directory_iterator{diseases_root}) {
        if (!item.is_directory()) {
            continue;
        }
        const auto name = core::to_lower(item.path().filename().string());
        if (!registered.contains(name)) {
            unregistered.push_back(name);
        }
    }

    // Sorted, so the report reads the same on every file system.
    std::sort(unregistered.begin(), unregistered.end());
    for (const auto &name : unregistered) {
        report.error(IssueCode::data_disease_not_in_registry,
                     IssueLocation{.file = (diseases_root / name).string()},
                     fmt::format("the data store has a directory for disease '{}' that the "
                                 "registry in index.json does not list",
                                 name));
    }

    // Metadata.json is not read by the program, but it is where the upstream `pulmonar` spelling
    // comes from, and HLM_India's config copied it and fails outright (audit D-01). Cross-checking
    // it turns that into a warning here rather than a surprise there.
    const auto metadata_path = diseases_root / kMetadataFileName;
    if (!std::filesystem::exists(metadata_path)) {
        return;
    }

    diag::IssueReport metadata_report;
    const auto metadata = io::read_json(metadata_path, metadata_report);
    if (!metadata.has_value()) {
        report.warning(IssueCode::data_index_invalid, IssueLocation{.file = metadata_path.string()},
                       "could not be read; it is documentation only, so this does not stop the run");
        return;
    }

    std::set<std::string> metadata_names;
    const auto collect = [&metadata_names](const nlohmann::json &node) {
        if (node.is_array()) {
            for (const auto &item : node) {
                if (item.is_string()) {
                    metadata_names.insert(core::to_lower(item.get<std::string>()));
                } else if (item.is_object() && item.contains("id") && item["id"].is_string()) {
                    metadata_names.insert(core::to_lower(item["id"].get<std::string>()));
                }
            }
        }
    };

    if (metadata->contains("input_file") && (*metadata)["input_file"].contains("running")) {
        collect((*metadata)["input_file"]["running"].value("diseases", nlohmann::json::array()));
    }
    if (metadata->contains("diseases")) {
        collect((*metadata)["diseases"].value("registry", nlohmann::json::array()));
    }

    for (const auto &name : metadata_names) {
        if (!registered.contains(name)) {
            report.warning(
                IssueCode::data_disease_not_in_registry,
                IssueLocation{.file = metadata_path.string()},
                fmt::format("names disease '{}', which is not in index.json's registry and has no "
                            "directory; a config copied from this file would fail. The canonical "
                            "spellings are the registry's.",
                            name));
        }
    }
}

std::string DataIndex::substitute_named(const std::string &pattern,
                                        const std::map<std::string, std::string> &tokens,
                                        const std::filesystem::path &file,
                                        diag::IssueReport &report) {
    std::string result;
    result.reserve(pattern.size());

    std::size_t position = 0;
    while (position < pattern.size()) {
        const auto start = pattern.find('{', position);
        if (start == std::string::npos) {
            result.append(pattern, position, std::string::npos);
            break;
        }

        const auto end = pattern.find('}', start + 1);
        if (end == std::string::npos) {
            result.append(pattern, position, std::string::npos);
            break;
        }

        result.append(pattern, position, start - position);

        const auto name = pattern.substr(start + 1, end - start - 1);
        const auto found = tokens.find(name);
        if (found == tokens.end()) {
            report.error(IssueCode::data_index_invalid, IssueLocation{.file = file.string()},
                         fmt::format("'{}' in the path pattern '{}' is not a token this build "
                                     "knows; expected one of COUNTRY_CODE, DISEASE_TYPE, GENDER "
                                     "or RISK_FACTOR",
                                     name, pattern));
        } else {
            result += found->second;
        }

        position = end + 1;
    }

    return result;
}

std::string DataIndex::substitute_sequential(const std::string &pattern,
                                             const std::vector<std::string> &values) {
    std::string result;
    result.reserve(pattern.size());

    std::size_t position = 0;
    std::size_t next_value = 0;
    while (position < pattern.size()) {
        const auto start = pattern.find('{', position);
        if (start == std::string::npos) {
            result.append(pattern, position, std::string::npos);
            break;
        }

        const auto end = pattern.find('}', start + 1);
        if (end == std::string::npos) {
            result.append(pattern, position, std::string::npos);
            break;
        }

        result.append(pattern, position, start - position);
        if (next_value < values.size()) {
            result += values[next_value++];
        }

        position = end + 1;
    }

    return result;
}

} // namespace hgps::data
