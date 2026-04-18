#include "program_dirs.h"

#ifdef __linux__
#include <climits>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#include "hgps_core/diagnostics/internal_error.h"

#include <sago/platform_folders.h>

#include <array>
#include <string>

namespace {
constexpr const char *program_name = "healthgps";

void throw_path_error() { throw hgps::core::InternalError("Could not get program path"); }
} // namespace

namespace hgps {

std::filesystem::path get_program_directory() { return get_program_path().parent_path(); }

std::filesystem::path get_program_path() {
#if defined(__linux__)
    std::array<char, PATH_MAX> path{};
    const auto length = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length <= 0) {
        throw_path_error();
    }
    path.at(static_cast<std::size_t>(length)) = '\0';
    return std::filesystem::path{path.data()};

#elif defined(_WIN32)
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        throw_path_error();
    }
    return std::filesystem::path{path.data()};

#else
#error "Unsupported platform"
#endif
}

std::filesystem::path get_cache_directory() {
    return std::filesystem::path{sago::getCacheDir()} / program_name;
}

std::filesystem::path get_temporary_directory() {
    return std::filesystem::temp_directory_path() / program_name;
}

} // namespace hgps