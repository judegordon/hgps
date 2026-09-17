#pragma once

#include "diagnostics/issue_report.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hgps::io {

/// @brief Reads a JSON document, converting a parse failure into a located issue.
///
/// nlohmann reports a byte offset; this maps it to a line and a column, because "line 34, column
/// 5" is actionable and "byte 1187" is not.
/// @brief Reads and parses a JSON file, discarding the named members as it goes.
///
/// The fitted static-model files carry per-observation diagnostics from the R fit — `residuals`
/// and `fittedValues`, 40,000 numbers each per factor — that the simulation never reads. France's
/// static_model.json is 18.8 MB of text, almost all of it those arrays, and building a
/// `nlohmann::json` tree for them costs about 110 MB of resident memory. Dropping them during the
/// parse rather than after it is the difference between this program using more memory than the
/// baseline and using half as much (docs/performance.md).
///
/// @param discarded_members Member names to skip, at any depth, along with their values.
std::optional<nlohmann::json> read_json(const std::filesystem::path &path,
                                        diag::IssueReport &report,
                                        const std::vector<std::string> &discarded_members);

std::optional<nlohmann::json> read_json(const std::filesystem::path &path,
                                        diag::IssueReport &report);

/// @brief A cursor over a JSON object that records what it could not find.
///
/// Every accessor takes the field name, records a located issue when the field is missing or of
/// the wrong type, and returns nullopt — so a loader can keep going and collect the fifth problem
/// as well as the first (docs/decisions/0007-two-tier-diagnostics.md). The JSON pointer of the
/// current position is carried along, so every issue says exactly where it is.
class JsonCursor {
  public:
    JsonCursor(const nlohmann::json &node, std::string file, std::string pointer,
               diag::IssueReport &report);

    const nlohmann::json &node() const noexcept { return *node_; }
    const std::string &pointer() const noexcept { return pointer_; }
    const std::string &file() const noexcept { return file_; }
    diag::IssueReport &report() const noexcept { return *report_; }

    bool is_object() const;
    bool is_array() const;
    bool has(const std::string &field) const;

    /// @brief A cursor over a child object, or nullopt with an issue recorded.
    std::optional<JsonCursor> object(const std::string &field) const;

    /// @brief A cursor over a child object, silently absent if the field is not there.
    std::optional<JsonCursor> optional_object(const std::string &field) const;

    /// @brief A cursor over each element of a child array.
    std::vector<JsonCursor> array(const std::string &field) const;

    std::optional<std::string> string(const std::string &field) const;
    std::optional<double> number(const std::string &field) const;
    std::optional<int> integer(const std::string &field) const;
    std::optional<unsigned int> unsigned_integer(const std::string &field) const;
    std::optional<bool> boolean(const std::string &field) const;
    std::optional<std::vector<double>> number_array(const std::string &field) const;
    std::optional<std::vector<std::string>> string_array(const std::string &field) const;

    /// @brief A value with a documented default: absent means the default, and the substitution is
    ///        reported as a warning naming the key and the value used. Never silent.
    bool boolean_or_default(const std::string &field, bool fallback,
                            diag::IssueCode code = diag::IssueCode::config_default_applied) const;
    int integer_or_default(const std::string &field, int fallback,
                           diag::IssueCode code = diag::IssueCode::config_default_applied) const;
    std::string string_or_default(const std::string &field, const std::string &fallback,
                                  diag::IssueCode code =
                                      diag::IssueCode::config_default_applied) const;

    /// @brief Records an error for every member of this object that is not in `allowed`.
    ///
    /// config v2 sets additionalProperties: false everywhere, so a misspelled key is an error and
    /// not a silent no-op (docs/decisions/0010-config-v2-and-a-converter.md).
    void reject_unknown_members(const std::vector<std::string> &allowed) const;

    /// @brief Records an error naming the replacement for a property that config v2 removed.
    void reject_removed_member(const std::string &field, const std::string &advice) const;

    /// @brief The location of a field of this object, for an issue raised by the caller.
    diag::IssueLocation location_of(const std::string &field) const;

    void error(const std::string &field, diag::IssueCode code, const std::string &message) const;
    void warning(const std::string &field, diag::IssueCode code, const std::string &message) const;

  private:
    const nlohmann::json *node_;
    std::string file_;
    std::string pointer_;
    diag::IssueReport *report_;

    const nlohmann::json *find(const std::string &field) const;
    std::string child_pointer(const std::string &field) const;
};

} // namespace hgps::io
