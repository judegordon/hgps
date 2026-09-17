#include "json.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include <fmt/format.h>

namespace hgps::io {
namespace {

using diag::IssueCode;
using diag::IssueLocation;

/// nlohmann's byte offset to (line, column), counting from 1.
std::pair<std::size_t, std::size_t> offset_to_line_column(const std::string &text,
                                                          std::size_t offset) {
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < offset && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return {line, column};
}

std::string type_name(const nlohmann::json &value) {
    return value.type_name();
}

} // namespace

std::optional<nlohmann::json> read_json(const std::filesystem::path &path,
                                        diag::IssueReport &report) {
    std::ifstream stream{path};
    if (!stream) {
        report.error(IssueCode::file_not_found, IssueLocation{.file = path.string()},
                     "cannot open the file for reading");
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto text = buffer.str();

    try {
        return nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error &error) {
        const auto [line, column] = offset_to_line_column(text, error.byte);
        report.error(IssueCode::json_parse_error,
                     IssueLocation{.file = path.string(), .line = line, .column = column},
                     error.what());
        return std::nullopt;
    }
}

JsonCursor::JsonCursor(const nlohmann::json &node, std::string file, std::string pointer,
                       diag::IssueReport &report)
    : node_{&node}, file_{std::move(file)}, pointer_{std::move(pointer)}, report_{&report} {}

bool JsonCursor::is_object() const { return node_->is_object(); }

bool JsonCursor::is_array() const { return node_->is_array(); }

std::string JsonCursor::child_pointer(const std::string &field) const {
    return fmt::format("{}/{}", pointer_, field);
}

diag::IssueLocation JsonCursor::location_of(const std::string &field) const {
    return IssueLocation{.file = file_, .field = field.empty() ? pointer_ : child_pointer(field)};
}

void JsonCursor::error(const std::string &field, IssueCode code,
                       const std::string &message) const {
    report_->error(code, location_of(field), message);
}

void JsonCursor::warning(const std::string &field, IssueCode code,
                         const std::string &message) const {
    report_->warning(code, location_of(field), message);
}

const nlohmann::json *JsonCursor::find(const std::string &field) const {
    if (!node_->is_object()) {
        return nullptr;
    }
    const auto it = node_->find(field);
    return it == node_->end() ? nullptr : &*it;
}

bool JsonCursor::has(const std::string &field) const { return find(field) != nullptr; }

std::optional<JsonCursor> JsonCursor::object(const std::string &field) const {
    const auto *value = find(field);
    if (value == nullptr) {
        error(field, IssueCode::config_missing_required, "this object is required");
        return std::nullopt;
    }
    if (!value->is_object()) {
        error(field, IssueCode::config_wrong_type,
              fmt::format("expected an object, found {}", type_name(*value)));
        return std::nullopt;
    }
    return JsonCursor{*value, file_, child_pointer(field), *report_};
}

std::optional<JsonCursor> JsonCursor::optional_object(const std::string &field) const {
    const auto *value = find(field);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (!value->is_object()) {
        error(field, IssueCode::config_wrong_type,
              fmt::format("expected an object, found {}", type_name(*value)));
        return std::nullopt;
    }
    return JsonCursor{*value, file_, child_pointer(field), *report_};
}

std::vector<JsonCursor> JsonCursor::array(const std::string &field) const {
    std::vector<JsonCursor> result;

    const auto *value = find(field);
    if (value == nullptr) {
        error(field, IssueCode::config_missing_required, "this array is required");
        return result;
    }
    if (!value->is_array()) {
        error(field, IssueCode::config_wrong_type,
              fmt::format("expected an array, found {}", type_name(*value)));
        return result;
    }

    result.reserve(value->size());
    for (std::size_t i = 0; i < value->size(); ++i) {
        result.emplace_back((*value)[i], file_, fmt::format("{}/{}", child_pointer(field), i),
                            *report_);
    }
    return result;
}

std::optional<std::string> JsonCursor::string(const std::string &field) const {
    const auto *value = find(field);
    if (value == nullptr) {
        error(field, IssueCode::config_missing_required, "this string is required");
        return std::nullopt;
    }
    if (!value->is_string()) {
        error(field, IssueCode::config_wrong_type,
              fmt::format("expected a string, found {}", type_name(*value)));
        return std::nullopt;
    }
    return value->get<std::string>();
}

std::optional<double> JsonCursor::number(const std::string &field) const {
    const auto *value = find(field);
    if (value == nullptr) {
        error(field, IssueCode::config_missing_required, "this number is required");
        return std::nullopt;
    }
    if (!value->is_number()) {
        error(field, IssueCode::config_wrong_type,
              fmt::format("expected a number, found {}", type_name(*value)));
        return std::nullopt;
    }
    return value->get<double>();
}

std::optional<int> JsonCursor::integer(const std::string &field) const {
    const auto *value = find(field);
    if (value == nullptr) {
        error(field, IssueCode::config_missing_required, "this integer is required");
        return std::nullopt;
    }
    if (!value->is_number_integer()) {
        error(field, IssueCode::config_wrong_type,
              fmt::format("expected an integer, found {}", type_name(*value)));
        return std::nullopt;
    }
    return value->get<int>();
}

std::optional<unsigned int> JsonCursor::unsigned_integer(const std::string &field) const {
    const auto value = integer(field);
    if (!value.has_value()) {
        return std::nullopt;
    }
    if (*value < 0) {
        error(field, IssueCode::config_bad_value,
              fmt::format("expected a non-negative integer, found {}", *value));
        return std::nullopt;
    }
    return static_cast<unsigned int>(*value);
}

std::optional<bool> JsonCursor::boolean(const std::string &field) const {
    const auto *value = find(field);
    if (value == nullptr) {
        error(field, IssueCode::config_missing_required, "this boolean is required");
        return std::nullopt;
    }
    if (!value->is_boolean()) {
        error(field, IssueCode::config_wrong_type,
              fmt::format("expected true or false, found {}", type_name(*value)));
        return std::nullopt;
    }
    return value->get<bool>();
}

std::optional<std::vector<double>> JsonCursor::number_array(const std::string &field) const {
    const auto *value = find(field);
    if (value == nullptr) {
        error(field, IssueCode::config_missing_required, "this array of numbers is required");
        return std::nullopt;
    }
    if (!value->is_array()) {
        error(field, IssueCode::config_wrong_type,
              fmt::format("expected an array of numbers, found {}", type_name(*value)));
        return std::nullopt;
    }

    std::vector<double> result;
    result.reserve(value->size());
    for (std::size_t i = 0; i < value->size(); ++i) {
        if (!(*value)[i].is_number()) {
            report_->error(IssueCode::config_wrong_type,
                           IssueLocation{.file = file_,
                                         .field = fmt::format("{}/{}", child_pointer(field), i)},
                           fmt::format("expected a number, found {}", type_name((*value)[i])));
            return std::nullopt;
        }
        result.push_back((*value)[i].get<double>());
    }
    return result;
}

std::optional<std::vector<std::string>> JsonCursor::string_array(const std::string &field) const {
    const auto *value = find(field);
    if (value == nullptr) {
        error(field, IssueCode::config_missing_required, "this array of strings is required");
        return std::nullopt;
    }
    if (!value->is_array()) {
        error(field, IssueCode::config_wrong_type,
              fmt::format("expected an array of strings, found {}", type_name(*value)));
        return std::nullopt;
    }

    std::vector<std::string> result;
    result.reserve(value->size());
    for (std::size_t i = 0; i < value->size(); ++i) {
        if (!(*value)[i].is_string()) {
            report_->error(IssueCode::config_wrong_type,
                           IssueLocation{.file = file_,
                                         .field = fmt::format("{}/{}", child_pointer(field), i)},
                           fmt::format("expected a string, found {}", type_name((*value)[i])));
            return std::nullopt;
        }
        result.push_back((*value)[i].get<std::string>());
    }
    return result;
}

bool JsonCursor::boolean_or_default(const std::string &field, bool fallback,
                                    IssueCode code) const {
    const auto *value = find(field);
    if (value == nullptr) {
        warning(field, code,
                fmt::format("not given; using the documented default of {}",
                            fallback ? "true" : "false"));
        return fallback;
    }
    return boolean(field).value_or(fallback);
}

int JsonCursor::integer_or_default(const std::string &field, int fallback, IssueCode code) const {
    const auto *value = find(field);
    if (value == nullptr) {
        warning(field, code, fmt::format("not given; using the documented default of {}", fallback));
        return fallback;
    }
    return integer(field).value_or(fallback);
}

std::string JsonCursor::string_or_default(const std::string &field, const std::string &fallback,
                                          IssueCode code) const {
    const auto *value = find(field);
    if (value == nullptr) {
        warning(field, code,
                fmt::format("not given; using the documented default of '{}'", fallback));
        return fallback;
    }
    return string(field).value_or(fallback);
}

void JsonCursor::reject_unknown_members(const std::vector<std::string> &allowed) const {
    if (!node_->is_object()) {
        return;
    }

    for (const auto &member : node_->items()) {
        const auto &key = member.key();
        if (std::find(allowed.begin(), allowed.end(), key) != allowed.end()) {
            continue;
        }

        // A misspelling is the common case, so name the closest allowed key when one is close
        // enough to be worth suggesting.
        std::string suggestion;
        std::size_t best = std::string::npos;
        for (const auto &candidate : allowed) {
            if (candidate.size() + 2 < key.size() || key.size() + 2 < candidate.size()) {
                continue;
            }
            std::size_t distance = 0;
            const auto length = std::min(candidate.size(), key.size());
            for (std::size_t i = 0; i < length; ++i) {
                distance += candidate[i] == key[i] ? 0U : 1U;
            }
            distance += std::max(candidate.size(), key.size()) - length;
            if (distance < best && distance <= 2) {
                best = distance;
                suggestion = candidate;
            }
        }

        error(key, IssueCode::config_unknown_property,
              suggestion.empty()
                  ? fmt::format("unknown property '{}'", key)
                  : fmt::format("unknown property '{}'; did you mean '{}'?", key, suggestion));
    }
}

void JsonCursor::reject_removed_member(const std::string &field, const std::string &advice) const {
    if (find(field) != nullptr) {
        error(field, IssueCode::config_removed_property,
              fmt::format("'{}' was removed in config v2; {}", field, advice));
    }
}

} // namespace hgps::io
