#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hgps::core {

enum class DiagnosticLevel {
    warning,
    error
};

enum class DiagnosticCode {
    unknown = 0,
    missing_key,
    wrong_type,
    invalid_value,
    invalid_enum_value,
    missing_file,
    invalid_path,
    parse_failure,
    missing_column,
    duplicate_entry,
    schema_violation
};

struct DiagnosticLocation {
    std::string source_path{};
    std::string field_path{};
    std::size_t line{0};
    std::size_t column{0};

    [[nodiscard]] bool has_source_path() const noexcept { return !source_path.empty(); }
    [[nodiscard]] bool has_field_path() const noexcept { return !field_path.empty(); }
    [[nodiscard]] bool has_line() const noexcept { return line != 0; }
    [[nodiscard]] bool has_column() const noexcept { return column != 0; }
};

struct Diagnostic {
    DiagnosticLevel level{DiagnosticLevel::error};
    DiagnosticCode code{DiagnosticCode::unknown};
    DiagnosticLocation location{};
    std::string message{};
};

[[nodiscard]] constexpr bool is_error(DiagnosticLevel level) noexcept {
    return level == DiagnosticLevel::error;
}

[[nodiscard]] constexpr bool is_warning(DiagnosticLevel level) noexcept {
    return level == DiagnosticLevel::warning;
}

[[nodiscard]] std::string_view to_string(DiagnosticLevel level) noexcept;
[[nodiscard]] std::string_view to_string(DiagnosticCode code) noexcept;

class Diagnostics {
  public:
    using container_type = std::vector<Diagnostic>;
    using const_iterator = container_type::const_iterator;

    Diagnostics() = default;

    void add(Diagnostic diagnostic);

    void add(DiagnosticLevel level, DiagnosticCode code, DiagnosticLocation location,
             std::string message);

    void error(DiagnosticCode code, DiagnosticLocation location, std::string message);

    void warning(DiagnosticCode code, DiagnosticLocation location, std::string message);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool has_warnings() const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t error_count() const noexcept;
    [[nodiscard]] std::size_t warning_count() const noexcept;

    [[nodiscard]] const container_type &all() const noexcept;

    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;

    void clear() noexcept;

  private:
    container_type items_{};
    std::size_t error_count_{0};
    std::size_t warning_count_{0};
};

[[nodiscard]] std::string format_diagnostic(const Diagnostic &diagnostic);

} // namespace hgps::core