// The index-keyed risk-factor store, tested against the behaviour it replaced.
//
// It presents the surface of `std::map<Identifier, double>` so that the change from one to the other
// was a change of representation and not of two hundred call sites. These tests are what says the
// surface really is the same — including the parts nothing in the tree happens to use today, because
// the next caller will.
#include "model/factor_values.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using hgps::model::FactorIndex;
using hgps::model::FactorValues;
using hgps::operator""_id;

TEST(FactorIndex, InternsANameOnceAndKeepsIt) {
    FactorIndex index;
    EXPECT_EQ(0U, index.size());
    EXPECT_EQ(FactorIndex::unknown, index.find("bmi"_id));

    const auto bmi = index.intern("bmi"_id);
    EXPECT_EQ(bmi, index.intern("bmi"_id));
    EXPECT_EQ(bmi, index.find("bmi"_id));
    EXPECT_EQ(1U, index.size());

    const auto energy = index.intern("energy"_id);
    EXPECT_NE(bmi, energy);
    EXPECT_EQ("bmi", index.name_of(bmi).to_string());
    EXPECT_EQ("energy", index.name_of(energy).to_string());
}

TEST(FactorIndex, IndicesAreDenseAndStartAtZero) {
    // Dense, because the store is a vector keyed by them and a sparse assignment would waste the
    // whole point.
    FactorIndex index;
    for (const char *name : {"a", "b", "c", "d"}) {
        index.intern(hgps::core::Identifier{name});
    }
    std::vector<std::uint32_t> assigned;
    for (const char *name : {"a", "b", "c", "d"}) {
        assigned.push_back(index.find(hgps::core::Identifier{name}));
    }
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 3}), assigned);
}

TEST(FactorIndex, AnUnassignedIndexHasNoName) {
    FactorIndex index;
    index.intern("bmi"_id);
    EXPECT_THROW(index.name_of(1), std::out_of_range);
}

TEST(FactorIndex, FindNeverAssigns) {
    // The distinction matters: a hot path asks `find` and must not grow the table from a typo.
    FactorIndex index;
    EXPECT_EQ(FactorIndex::unknown, index.find("bmi"_id));
    EXPECT_EQ(0U, index.size());
}

TEST(FactorValues, BehavesLikeTheMapItReplaced) {
    FactorValues values;
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(0U, values.size());
    EXPECT_FALSE(values.contains("bmi"_id));
    EXPECT_EQ(values.end(), values.find("bmi"_id));

    // operator[] inserts a zero, as std::map does.
    EXPECT_DOUBLE_EQ(0.0, values["bmi"_id]);
    EXPECT_EQ(1U, values.size());
    EXPECT_TRUE(values.contains("bmi"_id));

    values["bmi"_id] = 25.5;
    EXPECT_DOUBLE_EQ(25.5, values.at("bmi"_id));
    EXPECT_DOUBLE_EQ(25.5, values["bmi"_id]);
    EXPECT_EQ(1U, values.size()) << "reading an existing key must not insert";

    const auto found = values.find("bmi"_id);
    ASSERT_NE(values.end(), found);
    EXPECT_EQ("bmi", found->first.to_string());
    EXPECT_DOUBLE_EQ(25.5, found->second);
}

TEST(FactorValues, AtThrowsForAMissingName) {
    FactorValues values;
    values["bmi"_id] = 1.0;
    EXPECT_THROW(values.at("energy"_id), std::out_of_range);

    const FactorValues &constant = values;
    EXPECT_THROW(constant.at("energy"_id), std::out_of_range);
}

TEST(FactorValues, WritingThroughAnIteratorChangesTheStore) {
    FactorValues values;
    values["bmi"_id] = 25.0;
    values.find("bmi"_id)->second = 30.0;
    EXPECT_DOUBLE_EQ(30.0, values.at("bmi"_id));
}

TEST(FactorValues, IterationVisitsEveryEntryOnce) {
    FactorValues values;
    values["bmi"_id] = 1.0;
    values["energy"_id] = 2.0;
    values["sodium"_id] = 3.0;

    std::vector<std::string> names;
    double sum = 0.0;
    for (const auto &[name, value] : values) {
        names.push_back(name.to_string());
        sum += value;
    }
    EXPECT_EQ(3U, names.size());
    EXPECT_DOUBLE_EQ(6.0, sum);
}

TEST(FactorValues, IterationOrderIsTheSameForTwoPeopleWithTheSameFactors) {
    // The order is index order, not name order — and the contract is that it is *stated and
    // identical* for every person, because a sum or product over a person's factors must not depend
    // on which person it is (determinism clause D5's reasoning, applied per person).
    FactorValues first;
    FactorValues second;
    for (const char *name : {"zinc", "alpha", "middle"}) {
        first[hgps::core::Identifier{name}] = 1.0;
    }
    // The same names, inserted in the opposite order.
    for (const char *name : {"middle", "alpha", "zinc"}) {
        second[hgps::core::Identifier{name}] = 1.0;
    }

    std::vector<std::string> left;
    std::vector<std::string> right;
    for (const auto &[name, value] : first) {
        left.push_back(name.to_string());
    }
    for (const auto &[name, value] : second) {
        right.push_back(name.to_string());
    }
    EXPECT_EQ(left, right);
}

TEST(FactorValues, IndexLookupsAgreeWithNameLookups) {
    FactorValues values;
    values["bmi"_id] = 25.0;
    const auto index = hgps::model::factor_index().find("bmi"_id);
    ASSERT_NE(FactorIndex::unknown, index);

    ASSERT_NE(nullptr, values.find_index(index));
    EXPECT_DOUBLE_EQ(25.0, *values.find_index(index));
    EXPECT_DOUBLE_EQ(25.0, values.at_index(index));

    const auto missing = hgps::model::factor_index().intern("a_factor_nobody_has"_id);
    EXPECT_EQ(nullptr, values.find_index(missing));
    EXPECT_THROW(values.at_index(missing), std::out_of_range);
}

TEST(FactorValues, CopyingCopiesTheValues) {
    // The immigration path clones a person, which copies this. A shallow copy sharing storage would
    // make two people move together.
    FactorValues original;
    original["bmi"_id] = 25.0;

    auto copy = original;
    copy["bmi"_id] = 30.0;

    EXPECT_DOUBLE_EQ(25.0, original.at("bmi"_id));
    EXPECT_DOUBLE_EQ(30.0, copy.at("bmi"_id));
}

TEST(FactorValues, AnUnknownNameIsNotContainedAndDoesNotGrowTheIndex) {
    const auto before = hgps::model::factor_index().size();
    const FactorValues values;
    EXPECT_FALSE(values.contains("a_name_no_test_uses_anywhere_else"_id));
    EXPECT_EQ(values.end(), values.find("a_name_no_test_uses_anywhere_else"_id));
    EXPECT_EQ(before, hgps::model::factor_index().size())
        << "a read of an unknown name must not intern it";
}
