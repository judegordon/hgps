#include "fixture_packs.h"

#include "test_paths.h"

#include <cctype>

namespace hgps::test {

const std::vector<FixturePack> &fixture_packs() {
    static const std::vector<FixturePack> packs{
        FixturePack{.id = "Synthetic", .directory = synthetic_model_dir()},
        FixturePack{.id = "synthetic-b", .directory = synthetic_variant_model_dir()},
    };
    return packs;
}

std::string pack_suffix(const ::testing::TestParamInfo<FixturePack> &info) {
    // GoogleTest requires an ASCII alphanumeric suffix, and the second pack's id has a hyphen in
    // it precisely because a hyphen is a thing a client might not expect.
    std::string suffix;
    for (const char character : info.param.id) {
        suffix += (std::isalnum(static_cast<unsigned char>(character)) != 0) ? character : '_';
    }
    return suffix;
}

std::filesystem::path FixturePackTest::pack_scratch(const std::string &name) const {
    return scratch_dir(name + "_" + pack().id);
}

} // namespace hgps::test
