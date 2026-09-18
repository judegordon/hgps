// The compatibility-flag type, and the rule that every flag has an entry in docs/deviations.md.
//
// A flag that nobody can look up is worse than no flag: a manifest carrying "B-99" would say a
// deviation had been restored without saying which (ADR 0041).
#include "hgps/baseline_compat.h"

#include "support/test_paths.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::api::BaselineCompat;
using hgps::api::CompatFlag;

} // namespace

TEST(BaselineCompatFlags, NothingIsOnByDefault) {
    const BaselineCompat compat;
    EXPECT_TRUE(compat.none());
    EXPECT_FALSE(compat.any());
    EXPECT_EQ(0U, compat.count());
    EXPECT_TRUE(compat.names().empty());
    for (const auto flag : BaselineCompat::known()) {
        EXPECT_FALSE(compat.is_set(flag)) << BaselineCompat::name_of(flag);
    }
}

TEST(BaselineCompatFlags, AllTurnsOnEveryFlagThereIs) {
    const auto compat = BaselineCompat::all();
    EXPECT_EQ(BaselineCompat::flag_count, compat.count());
    EXPECT_EQ(BaselineCompat::known().size(), compat.names().size());
    for (const auto flag : BaselineCompat::known()) {
        EXPECT_TRUE(compat.is_set(flag)) << BaselineCompat::name_of(flag);
    }
}

TEST(BaselineCompatFlags, NameMatchingIgnoresCaseAndSeparators) {
    for (const auto *spelling : {"B-24", "b-24", "B_24", "b24", "B24"}) {
        BaselineCompat compat;
        ASSERT_TRUE(BaselineCompat::apply_name(spelling, compat)) << spelling;
        EXPECT_TRUE(compat.is_set(CompatFlag::b24)) << spelling;
    }
}

TEST(BaselineCompatFlags, AnUnknownNameIsRefusedAndChangesNothing) {
    BaselineCompat compat;
    compat.set(CompatFlag::b24);

    EXPECT_FALSE(BaselineCompat::apply_name("B-99", compat));
    EXPECT_FALSE(BaselineCompat::apply_name("", compat));
    EXPECT_FALSE(BaselineCompat::apply_name("b-2 4", compat));

    // Still exactly what it was: a refused name must not half-apply.
    EXPECT_EQ(1U, compat.count());
    EXPECT_TRUE(compat.is_set(CompatFlag::b24));
}

TEST(BaselineCompatFlags, ApplyNameAcceptsAll) {
    BaselineCompat compat;
    ASSERT_TRUE(BaselineCompat::apply_name("all", compat));
    EXPECT_EQ(BaselineCompat::flag_count, compat.count());
}

TEST(BaselineCompatFlags, MergeIsTheUnion) {
    BaselineCompat left;
    left.set(CompatFlag::b24);

    BaselineCompat right;
    right.merge(left);
    EXPECT_TRUE(right.is_set(CompatFlag::b24));

    // Merging something empty takes nothing away.
    right.merge(BaselineCompat{});
    EXPECT_TRUE(right.is_set(CompatFlag::b24));
}

TEST(BaselineCompatFlags, NamesAreTheFlagsThatAreOn) {
    BaselineCompat compat;
    EXPECT_TRUE(compat.names().empty());

    compat.set(CompatFlag::b24);
    EXPECT_EQ(std::vector<std::string>{"B-24"}, compat.names());

    compat.set(CompatFlag::b24, false);
    EXPECT_TRUE(compat.names().empty());
}

TEST(BaselineCompatFlags, TheSentenceListsEveryFlagAndAll) {
    const auto sentence = BaselineCompat::known_names_sentence();
    for (const auto flag : BaselineCompat::known()) {
        EXPECT_NE(std::string::npos, sentence.find(BaselineCompat::name_of(flag)))
            << BaselineCompat::name_of(flag) << " is missing from: " << sentence;
    }
    EXPECT_NE(std::string::npos, sentence.find("all"));
}

TEST(BaselineCompatFlags, EveryFlagIsDocumentedInDeviationsMd) {
    // The flag is named after its deviation so that a reader can look it up. This is the test that
    // keeps that true: adding a flag without an entry in docs/deviations.md fails here.
    const auto path = hgps::test::docs_dir() / "deviations.md";
    std::ifstream stream{path};
    ASSERT_TRUE(stream) << "could not read " << path;

    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto text = buffer.str();

    for (const auto flag : BaselineCompat::known()) {
        const auto name = std::string{BaselineCompat::name_of(flag)};
        EXPECT_NE(std::string::npos, text.find("**" + name + "**"))
            << name << " is a compatibility flag with no row in docs/deviations.md";
        EXPECT_FALSE(BaselineCompat::description_of(flag).empty()) << name;
    }
}
