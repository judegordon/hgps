#include "diagnostics.h"

#include <sstream>
#include <utility>

namespace hgps::core {

std::string_view to_string(DiagnosticLevel level) noexcept {
    switch (level) {
    case DiagnosticLevel::warning:
        return "warning";
    case DiagnosticLevel::error:
        return "error";
    }

    return "unknown";
}

std::string_view to_string(DiagnosticCode code) noexcept {
    switch (code) {
    case DiagnosticCode::unknown:
        return "unknown";
    case DiagnosticCode::missing_key:
        return "missing_key";
    case DiagnosticCode::wrong_type:
        return "wrong_type";
    case DiagnosticCode::invalid_value:
        return "invalid_value";
    case DiagnosticCode::invalid_enum_value:
        return "invalid_enum_value";
    case DiagnosticCode::missing_file:
        return "missing_file";
    case DiagnosticCode::invalid_path:
        return "invalid_path";
    case DiagnosticCode::parse_failure:
        return "parse_failure";
    case DiagnosticCode::missing_column:
        return "missing_column";
    case DiagnosticCode::duplicate_entry:
        return "duplicate_entry";
    case DiagnosticCode::schema_violation:
        return "schema_violation";
    }

    return "unknown";
}

void Diagnostics::add(Diagnostic diagnostic) {
    if (diagnostic.level == DiagnosticLevel::error) {
        ++error_count_;
    } else {
        ++warning_count_;
    }

    items_.push_back(std::move(diagnostic));
}

void Diagnostics::add(DiagnosticLevel level, DiagnosticCode code, DiagnosticLocation location,
                      std::string message) {
    add(Diagnostic{level, code, std::move(location), std::move(message)});
}

void Diagnostics::error(DiagnosticCode code, DiagnosticLocation location, std::string message) {
    add(DiagnosticLevel::error, code, std::move(location), std::move(message));
}

void Diagnostics::warning(DiagnosticCode code, DiagnosticLocation location, std::string message) {
    add(DiagnosticLevel::warning, code, std::move(location), std::move(message));
}

bool Diagnostics::empty() const noexcept {
    return items_.empty();
}

bool Diagnostics::has_errors() const noexcept {
    return error_count_ != 0;
}

bool Diagnostics::has_warnings() const noexcept {
    return warning_count_ != 0;
}

std::size_t Diagnostics::size() const noexcept {
    return items_.size();
}

std::size_t Diagnostics::error_count() const noexcept {
    return error_count_;
}

std::size_t Diagnostics::warning_count() const noexcept {
    return warning_count_;
}

const Diagnostics::container_type &Diagnostics::all() const noexcept {
    return items_;
}

Diagnostics::const_iterator Diagnostics::begin() const noexcept {
    return items_.begin();
}

Diagnostics::const_iterator Diagnostics::end() const noexcept {
    return items_.end();
}

void Diagnostics::clear() noexcept {
    items_.clear();
    error_count_ = 0;
    warning_count_ = 0;
}

std::string format_diagnostic(const Diagnostic &diagnostic) {
    std::ostringstream stream;

    stream << '[' << to_string(diagnostic.level) << ']'
           << '[' << to_string(diagnostic.code) << ']';

    if (diagnostic.location.has_source_path()) {
        stream << ' ' << diagnostic.location.source_path;
    }

    if (diagnostic.location.has_field_path()) {
        stream << " (" << diagnostic.location.field_path << ')';
    }

    if (diagnostic.location.has_line()) {
        stream << ':' << diagnostic.location.line;
        if (diagnostic.location.has_column()) {
            stream << ':' << diagnostic.location.column;
        }
    }

    if (!diagnostic.message.empty()) {
        stream << " - " << diagnostic.message;
    }

    return stream.str();
}

} // namespace hgps::core