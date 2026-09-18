#include "fixture_packs.h"

#include "test_paths.h"

#include <cctype>

// ThreadSanitizer, asked of the compiler rather than of the build system, so a preset that adds
// -fsanitize=thread by any route gets the same answer.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define HGPS_THREAD_SANITIZER 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define HGPS_THREAD_SANITIZER 1
#endif

namespace hgps::test {

const std::vector<FixturePack> &fixture_packs() {
#if defined(HGPS_THREAD_SANITIZER)
    // **One pack under ThreadSanitizer, both everywhere else.**
    //
    // The second pack doubled the simulations the suite runs, and under TSan a simulation is
    // seconds rather than a fifth of one: the 184 `Packs/` tests were 1,975 of the tsan preset's
    // 2,529 seconds of test time, and the `macos · appleclang · tsan` job reached 88 minutes on
    // every push (docs/SUMMARY.md, "What the second fixture pack cost").
    //
    // What TSan is for is races, and a race is a property of the code rather than of the
    // configuration that reaches it: the threading in this program — the parallel sections, the
    // server, the run queue — is the same code on both packs. What the second pack buys is the
    // thing described in fixture_packs.h, which is a *logic* check: an assumption about one
    // configuration's file layout, scenario set or disease set. That is worth running everywhere
    // it is cheap, and it is cheap in release and under AddressSanitizer, where both packs still
    // run. It is not worth forty minutes of every push to run the same races twice.
    //
    // The cost of being wrong about that is a race reachable only through the second pack's
    // configuration and not the first's. Nothing found in six runs is of that shape, and the
    // release and ASan entries still run both packs, so such a race would have to be invisible to
    // AddressSanitizer as well. docs/decisions/0046-*.md.
    static const std::vector<FixturePack> packs{
        FixturePack{.id = "Synthetic", .directory = synthetic_model_dir()},
    };
#else
    static const std::vector<FixturePack> packs{
        FixturePack{.id = "Synthetic", .directory = synthetic_model_dir()},
        FixturePack{.id = "synthetic-b", .directory = synthetic_variant_model_dir()},
    };
#endif
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
