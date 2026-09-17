#include "paths.h"

#include <array>
#include <cstdlib>
#include <stdexcept>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

#include <fmt/format.h>

namespace hgps::io {
namespace {

constexpr const char *kProgramName = "healthgps";

const char *environment(const char *name) { return std::getenv(name); }

} // namespace

std::filesystem::path program_path() {
#ifdef __APPLE__
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        throw std::runtime_error("Could not determine the program path");
    }
    return std::filesystem::path{buffer.data()};
#else
    std::array<char, PATH_MAX> buffer{};
    const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) {
        throw std::runtime_error("Could not determine the program path");
    }
    buffer[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path{buffer.data()};
#endif
}

std::filesystem::path program_directory() { return program_path().parent_path(); }

std::filesystem::path cache_directory() {
    if (const char *override_dir = environment("HEALTHGPS_CACHE_DIR")) {
        return std::filesystem::path{override_dir};
    }

#ifdef __APPLE__
    if (const char *home = environment("HOME")) {
        return std::filesystem::path{home} / "Library" / "Caches" / kProgramName;
    }
#else
    if (const char *xdg = environment("XDG_CACHE_HOME")) {
        return std::filesystem::path{xdg} / kProgramName;
    }
    if (const char *home = environment("HOME")) {
        return std::filesystem::path{home} / ".cache" / kProgramName;
    }
#endif

    return std::filesystem::temp_directory_path() / kProgramName / "cache";
}

std::filesystem::path temporary_directory() {
    auto path = std::filesystem::temp_directory_path() / kProgramName;
    std::filesystem::create_directories(path);
    return path;
}

std::string expand_environment_variables(const std::string &value,
                                         std::vector<std::string> &undefined) {
    std::string result;
    result.reserve(value.size());

    std::size_t position = 0;
    while (position < value.size()) {
        const auto start = value.find("${", position);
        if (start == std::string::npos) {
            result.append(value, position, std::string::npos);
            break;
        }

        const auto end = value.find('}', start + 2);
        if (end == std::string::npos) {
            // An unterminated ${ is literal text, not a variable: reporting it as undefined would
            // name something the user never wrote.
            result.append(value, position, std::string::npos);
            break;
        }

        result.append(value, position, start - position);

        const auto name = value.substr(start + 2, end - start - 2);
        if (const char *replacement = environment(name.c_str())) {
            result += replacement;
        } else {
            undefined.push_back(name);
            result += fmt::format("${{{}}}", name);
        }

        position = end + 1;
    }

    return result;
}

} // namespace hgps::io
