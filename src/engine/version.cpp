#include "hgps/version.h"

#include "hgps/build_info.generated.h"

namespace hgps::api {

const BuildInfo &build_info() noexcept {
    static const BuildInfo info{
        .version = HGPS_BUILD_VERSION,
        .git_commit = HGPS_BUILD_GIT_COMMIT,
        .git_describe = HGPS_BUILD_GIT_DESCRIBE,
        .git_dirty = HGPS_BUILD_GIT_DIRTY,
        .platform = HGPS_BUILD_PLATFORM,
        .compiler = HGPS_BUILD_COMPILER,
        .build_type = HGPS_BUILD_TYPE,
    };
    return info;
}

std::string_view version() noexcept { return build_info().version; }

} // namespace hgps::api
