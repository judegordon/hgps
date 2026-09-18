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

TEST(FactorValues, FindsAnEntryWhoseIndexIsNotItsPosition) {
    // A person holds a *sparse* subset of the process-wide index table, so an entry is almost never
    // at the position its index would suggest: measured over a whole run, `KevinHall_FINCH` finds it
    // there 0.0% of the time and `HLM_France` 22% (ADR 0040). This is therefore the ordinary case
    // rather than an exotic one, and it is the case the long-vector branch has to get right — the
    // binary search is bounded by `index` precisely because the entry can be anywhere below it.
    //
    // Ninety interned names with every third one held gives thirty entries, which is over the
    // sixteen-entry scan threshold, so this goes down the bounded-search branch — and leaves no entry
    // except the first at its own index. The companion below holds nine, which stays on the scan.
    auto &table = hgps::model::factor_index();
    std::vector<hgps::core::Identifier> names;
    std::vector<std::uint32_t> indices;
    for (int n = 0; n < 90; ++n) {
        names.emplace_back("sparse_factor_" + std::to_string(n));
        indices.push_back(table.intern(names.back()));
    }

    // Every third one, so past the first entry no index equals the position it sits at.
    FactorValues values;
    std::vector<std::uint32_t> held;
    for (std::size_t n = 0; n < names.size(); n += 3) {
        held.push_back(indices[n]);
        values[names[n]] = static_cast<double>(n);
    }

    ASSERT_EQ(held.size(), values.size());
    for (std::size_t n = 0; n < held.size(); ++n) {
        ASSERT_NE(nullptr, values.find_index(held[n])) << "index " << held[n] << " at position " << n;
        EXPECT_DOUBLE_EQ(static_cast<double>(n * 3), *values.find_index(held[n]));
        EXPECT_DOUBLE_EQ(static_cast<double>(n * 3), values.at_index(held[n]));
    }

    // And the ones it does not hold are absent rather than found at a neighbouring position.
    for (std::size_t n = 0; n < names.size(); ++n) {
        if (n % 3 != 0) {
            EXPECT_EQ(nullptr, values.find_index(indices[n]))
                << "index " << indices[n] << " is not held and must not be found";
        }
    }
}

TEST(FactorValues, FindsASparseEntryInAShortVectorToo) {
    // The other side of the threshold: the same sparse shape, small enough to be scanned. Both
    // branches have to give the same answers, and only one of them is exercised by any given example
    // (ADR 0040), so both are pinned here.
    auto &table = hgps::model::factor_index();
    std::vector<hgps::core::Identifier> names;
    std::vector<std::uint32_t> indices;
    for (int n = 0; n < 30; ++n) {
        names.emplace_back("short_sparse_factor_" + std::to_string(n));
        indices.push_back(table.intern(names.back()));
    }

    FactorValues values;
    for (std::size_t n = 0; n < names.size(); n += 3) {
        values[names[n]] = static_cast<double>(n);
    }
    ASSERT_EQ(10U, values.size());

    for (std::size_t n = 0; n < names.size(); ++n) {
        if (n % 3 == 0) {
            ASSERT_NE(nullptr, values.find_index(indices[n]));
            EXPECT_DOUBLE_EQ(static_cast<double>(n), *values.find_index(indices[n]));
        } else {
            EXPECT_EQ(nullptr, values.find_index(indices[n]));
        }
    }
}
