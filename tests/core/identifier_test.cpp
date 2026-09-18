// Ported from the baseline's src/HealthGPS.Tests/Identifier.Test.cpp, preserving suite and test
// names and expected values.
#include "core/identifier.h"

#include <gtest/gtest.h>

#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

TEST(TestCore_Identity, CreateEmpty) {
    using namespace hgps::core;
    auto empty = Identifier::empty();
    auto other = Identifier{};
    auto again = Identifier{""};

    ASSERT_TRUE(empty.is_empty());
    ASSERT_TRUE(other.is_empty());
    ASSERT_TRUE(again.is_empty());

    ASSERT_EQ(0U, empty.size());
    ASSERT_EQ(0U, other.size());
    ASSERT_EQ(0U, again.size());

    ASSERT_EQ(other, empty);
    ASSERT_EQ(again, empty);
    ASSERT_EQ(other, again);
}

TEST(TestCore_Identity, CreateImplicit) {
    using namespace hgps::core;
    const Identifier cat_char = "Cat";
    const Identifier cat_str = std::string{"Cat"};

    ASSERT_EQ(cat_char, cat_str);
}

TEST(TestCore_Identity, CreateValid) {
    using namespace hgps::core;
    const auto str = std::string{"TEST_5"};
    const auto key = Identifier{str};

    ASSERT_FALSE(key.is_empty());
    ASSERT_EQ(str.size(), key.size());
    ASSERT_EQ("test_5", key.to_string());
}

TEST(TestCore_Identity, Equality) {
    using namespace hgps::core;

    const auto dodo = Identifier{"Dodo"};
    const auto dodo_lower = Identifier{"dodo"};
    const auto dodo_upper = Identifier{"DODO"};
    const auto duck = Identifier{"Duck"};

    ASSERT_EQ(dodo, dodo);
    ASSERT_EQ(dodo, dodo_lower);
    ASSERT_EQ(dodo, dodo_upper);
    ASSERT_EQ(dodo_lower, dodo_upper);
    ASSERT_EQ(dodo.hash(), dodo_upper.hash());

    ASSERT_TRUE(dodo == dodo_lower);
    ASSERT_TRUE(dodo.equal(dodo_lower));
    ASSERT_TRUE(dodo_lower == dodo_upper);
    ASSERT_TRUE(dodo_lower.equal(dodo_upper));
    ASSERT_TRUE(dodo != duck);
    ASSERT_FALSE(dodo == duck);
    ASSERT_FALSE(dodo.equal(duck));
}

TEST(TestCore_Identity, Comparable) {
    using namespace hgps::core;

    const auto cat = Identifier{"Cat"};
    const auto dog = Identifier{"DOG"};
    const auto cow = Identifier{"cat"};

    ASSERT_GT(dog, cat);
    ASSERT_LT(cow, dog);
    ASSERT_EQ(cat, cow);
    ASSERT_TRUE(dog > cat);
    ASSERT_TRUE(cow < dog);
    ASSERT_TRUE(dog >= cat);
    ASSERT_TRUE(cow <= dog);

    ASSERT_TRUE(cat >= cow);
    ASSERT_TRUE(cow >= cat);
    ASSERT_FALSE(cat > dog);
    ASSERT_FALSE(dog < cow);
}

TEST(TestCore_Identity, CreateInvalidThrow) {
    using namespace hgps::core;
    ASSERT_THROW(Identifier{"5Start"}, std::invalid_argument);
    ASSERT_THROW(Identifier{"With Space"}, std::invalid_argument);
    ASSERT_THROW(Identifier{"With-10"}, std::invalid_argument);
    ASSERT_THROW(Identifier{"With.dot"}, std::invalid_argument);
}

TEST(TestCore_Identity, CreateMapKey) {
    using namespace hgps::core;

    const auto cat = Identifier{"Cat"};
    const auto dog = Identifier{"DOG"};
    const auto cow = Identifier{"cow"};
    const auto tmp = Identifier{"None"};

    const auto animals = std::map<Identifier, int>{{cat, 3}, {dog, 7}, {cow, 2}};

    ASSERT_EQ(3U, animals.size());
    ASSERT_TRUE(animals.contains(cat));
    ASSERT_TRUE(animals.contains(dog));
    ASSERT_TRUE(animals.contains(cow));
    ASSERT_FALSE(animals.contains(tmp));
}

TEST(TestCore_Identity, CreateUnorderedMapKey) {
    using namespace hgps::core;

    const auto cat = Identifier{"Cat"};
    const auto dog = Identifier{"DOG"};
    const auto cow = Identifier{"cow"};
    const auto tmp = Identifier{"None"};

    const auto animals = std::unordered_map<Identifier, int>{{cat, 3}, {dog, 7}, {cow, 2}};

    ASSERT_EQ(3U, animals.size());
    ASSERT_TRUE(animals.contains(cat));
    ASSERT_TRUE(animals.contains(dog));
    ASSERT_TRUE(animals.contains(cow));
    ASSERT_FALSE(animals.contains(tmp));
}

TEST(TestCore_Identity, ConvertToStream) {
    using namespace hgps::core;

    const auto dodo = Identifier{"Dodo_and_Duck"};

    std::stringstream stream;
    stream << dodo;

    ASSERT_EQ(dodo.to_string(), stream.str());
}

TEST(TestCore_Identity, ToStringMatchesValue) {
    using namespace hgps::core;
    const Identifier id{"bmi"};
    ASSERT_EQ("bmi", id.to_string());
}

TEST(TestCore_Identity, HashStable) {
    using namespace hgps::core;
    const Identifier a{"x"};
    const Identifier b{"x"};
    ASSERT_EQ(std::hash<Identifier>{}(a), std::hash<Identifier>{}(b));
}

TEST(TestCore_Identity, OrderingConsistentWithEquals) {
    using namespace hgps::core;
    const Identifier a{"a"};
    const Identifier b{"b"};
    ASSERT_TRUE(a < b);
    ASSERT_FALSE(b < a);
}

// Added here, not in the baseline: the defect this type existed to have.
//
// Baseline finding B-04 — operator== compared only the cached 64-bit hash while the defaulted
// operator<=> compared the string, so for a hash collision `a == b` and `a < b` could both be
// true. Equality here compares the string, so equality and ordering cannot disagree. This test
// states the property directly rather than trying to manufacture a std::hash collision.
TEST(TestCore_Identity, EqualityAndOrderingAgree) {
    using namespace hgps::core;

    const auto names = std::vector<Identifier>{Identifier{"bmi"}, Identifier{"energy"},
                                               Identifier{"bmi"}, Identifier{"sodium"},
                                               Identifier{"age"}};

    for (const auto &left : names) {
        for (const auto &right : names) {
            const bool equal = (left == right);
            const bool ordered_equal = std::is_eq(left <=> right);
            EXPECT_EQ(equal, ordered_equal)
                << left.to_string() << " vs " << right.to_string()
                << ": operator== and operator<=> disagree (baseline finding B-04)";
            EXPECT_EQ(equal, left.equal(right));

            if (equal) {
                EXPECT_FALSE(left < right);
                EXPECT_FALSE(right < left);
            }
        }
    }
}

// Added here: the hash is for bucketing only, so an identifier must still be usable as an
// unordered_map key while equality ignores the hash.
TEST(TestCore_Identity, HashIsOnlyForBucketing) {
    using namespace hgps::core;

    std::unordered_map<Identifier, int> counts;
    counts[Identifier{"BMI"}] = 1;
    counts[Identifier{"bmi"}] += 1;

    ASSERT_EQ(1U, counts.size());
    ASSERT_EQ(2, counts.at(Identifier{"bmi"}));
}
