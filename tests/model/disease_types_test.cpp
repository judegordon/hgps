// Ports the baseline's DiseaseModels.Test.cpp lookup-table cases (suite TestHealthGPS_Disease),
// RelativeRiskLookup.Test.cpp (suite TestRelativeRiskLookup, 4) and WeightModel.Test.cpp (suite
// WeightModelTest, 5), keeping their values — the LMS z-score cut-offs and the interpolation
// behaviour are what these pin.
#include "model/disease/disease_table.h"
#include "model/weight_model.h"

#include "diagnostics/internal_error.h"

#include <gtest/gtest.h>

#include <cmath>

using hgps::core::DiseaseGroup;
using hgps::core::DiseaseInfo;
using hgps::core::FloatArray2D;
using hgps::core::Gender;
using hgps::core::Identifier;
using namespace hgps::model;

namespace {

DiseaseTable make_table() {
    const DiseaseInfo info{
        .group = DiseaseGroup::other, .code = Identifier{"asthma"}, .name = "Asthma"};

    const std::map<std::string, int> measures{{MeasureKey::prevalence, 1},
                                              {MeasureKey::incidence, 2},
                                              {MeasureKey::remission, 3},
                                              {MeasureKey::mortality, 4}};

    std::map<int, std::map<Gender, DiseaseMeasure>> data;
    for (int age = 0; age <= 3; ++age) {
        for (const auto gender : {Gender::male, Gender::female}) {
            const double scale = gender == Gender::male ? 1.0 : 0.5;
            data[age][gender] = DiseaseMeasure{{{1, 0.1 * age * scale},
                                                {2, 0.01 * age * scale},
                                                {3, 0.2 * scale},
                                                {4, 0.001 * age * scale}}};
        }
    }

    return DiseaseTable{info, measures, data};
}

} // namespace

TEST(TestHealthGPS_Disease, DiseaseMeasureLookup) {
    const DiseaseMeasure measure{{{1, 0.25}, {4, 0.5}}};

    EXPECT_EQ(2U, measure.size());
    EXPECT_DOUBLE_EQ(0.25, measure.at(1));
    EXPECT_DOUBLE_EQ(0.5, measure[4]);
    EXPECT_TRUE(measure.contains(1));
    EXPECT_FALSE(measure.contains(2));
    EXPECT_THROW(measure.at(2), std::out_of_range);
}

TEST(TestHealthGPS_Disease, DiseaseTableShapeAndAccess) {
    const auto table = make_table();

    EXPECT_EQ(Identifier{"asthma"}, table.info().code);
    EXPECT_EQ(4U, table.rows());
    EXPECT_EQ(2U, table.columns());
    EXPECT_EQ(8U, table.size());
    EXPECT_EQ(0, table.min_age());
    EXPECT_EQ(3, table.max_age());

    EXPECT_TRUE(table.contains(2));
    EXPECT_FALSE(table.contains(9));

    EXPECT_EQ(1, table.at(MeasureKey::prevalence));
    EXPECT_EQ(4, table[MeasureKey::mortality]);
    EXPECT_THROW(table.at("nonsense"), std::out_of_range);

    EXPECT_DOUBLE_EQ(0.2, table(2, Gender::male).at(table.at(MeasureKey::prevalence)));
    EXPECT_DOUBLE_EQ(0.1, table(2, Gender::female).at(table.at(MeasureKey::prevalence)));
    EXPECT_THROW(table(9, Gender::male), std::out_of_range);
}

TEST(TestHealthGPS_Disease, DiseaseTableRejectsAnEmptyCodeOrRaggedRows) {
    const std::map<std::string, int> measures{{MeasureKey::prevalence, 1}};

    EXPECT_THROW(DiseaseTable(DiseaseInfo{}, measures, {}), std::invalid_argument);

    const DiseaseInfo info{
        .group = DiseaseGroup::other, .code = Identifier{"asthma"}, .name = "Asthma"};

    // Age 1 has only one sex, which would make the table's column count a lie.
    std::map<int, std::map<Gender, DiseaseMeasure>> ragged{
        {0, {{Gender::male, DiseaseMeasure{{{1, 0.1}}}},
             {Gender::female, DiseaseMeasure{{{1, 0.1}}}}}},
        {1, {{Gender::male, DiseaseMeasure{{{1, 0.1}}}}}}};

    EXPECT_THROW(DiseaseTable(info, measures, ragged), std::invalid_argument);
}

TEST(TestRelativeRiskLookup, InterpolatesBetweenBreakpointsAndClampsOutside) {
    const MonotonicVector<int> ages{{0, 1}};
    const MonotonicVector<float> values{{10.0F, 20.0F, 30.0F}};

    FloatArray2D data{2, 3};
    data(0, 0) = 1.0F;
    data(0, 1) = 2.0F;
    data(0, 2) = 4.0F;
    data(1, 0) = 1.5F;
    data(1, 1) = 2.5F;
    data(1, 2) = 4.5F;

    const RelativeRiskLookup lookup{ages, values, data};

    EXPECT_EQ(6U, lookup.size());
    EXPECT_EQ(2U, lookup.rows());
    EXPECT_EQ(3U, lookup.columns());
    EXPECT_FALSE(lookup.empty());

    // Exact breakpoints.
    EXPECT_FLOAT_EQ(1.0F, lookup.at(0, 10.0F));
    EXPECT_FLOAT_EQ(2.0F, lookup(0, 20.0F));
    EXPECT_FLOAT_EQ(4.5F, lookup.at(1, 30.0F));

    // Clamped outside the value range, in both directions.
    EXPECT_FLOAT_EQ(1.0F, lookup.at(0, 5.0F));
    EXPECT_FLOAT_EQ(4.0F, lookup.at(0, 99.0F));

    // Linear between breakpoints: halfway from 20 to 30 is halfway from 2 to 4.
    EXPECT_FLOAT_EQ(3.0F, lookup.at(0, 25.0F));
    EXPECT_FLOAT_EQ(1.5F, lookup.at(0, 15.0F));
}

TEST(TestRelativeRiskLookup, ContainsAndUnknownAge) {
    const MonotonicVector<int> ages{{0, 1}};
    const MonotonicVector<float> values{{10.0F, 20.0F}};
    const RelativeRiskLookup lookup{ages, values, FloatArray2D{2, 2, 1.0F}};

    EXPECT_TRUE(lookup.contains(1, 20.0F));
    EXPECT_FALSE(lookup.contains(1, 15.0F));
    EXPECT_FALSE(lookup.contains(9, 20.0F));
    EXPECT_THROW(lookup.at(9, 20.0F), std::out_of_range);
}

TEST(TestRelativeRiskLookup, SizeMismatchThrows) {
    const MonotonicVector<int> ages{{0, 1}};
    const MonotonicVector<float> values{{10.0F, 20.0F, 30.0F}};

    EXPECT_THROW(RelativeRiskLookup(ages, values, FloatArray2D{2, 2}), std::out_of_range);
    EXPECT_THROW(RelativeRiskLookup(ages, values, FloatArray2D{3, 3}), std::out_of_range);
}

TEST(TestRelativeRiskLookup, ASingleCellTableIsValid) {
    // The baseline's CreateEmptyStorage: a 1x1 table is degenerate but well defined, and the
    // disease data contains them.
    const MonotonicVector<int> ages{{0}};
    const MonotonicVector<float> values{{0.0F}};
    const RelativeRiskLookup lookup{ages, values, FloatArray2D{1, 1}};

    EXPECT_EQ(1U, lookup.rows());
    EXPECT_EQ(1U, lookup.columns());
    EXPECT_EQ(1U, lookup.size());
    EXPECT_FLOAT_EQ(0.0F, lookup(0, 0.0F));
    // One breakpoint, so every value clamps onto it.
    EXPECT_FLOAT_EQ(0.0F, lookup(0, 99.0F));
}

TEST(TestRelativeRiskLookup, ReferenceDataLookup) {
    // Ported from the baseline with its expected values unchanged: 44 lookups against a real
    // 7x5 relative-risk table, which is what pins the interpolation arithmetic rather than just
    // its shape. Age 3's row is {1.0, 4.2, 7.3, 10.5, 13.5} over BMI {18, 25, 30, 35, 40}.
    const MonotonicVector<int> ages{{0, 1, 2, 3, 4, 5, 7}};
    const MonotonicVector<float> values{{18.0F, 25.0F, 30.0F, 35.0F, 40.0F}};
    const std::vector<float> data = {
        1.0F, 4.5F, 8.0F, 11.6F, 15.0F, // 0
        1.0F, 4.4F, 7.9F, 11.3F, 14.8F, // 1
        1.0F, 4.3F, 7.6F, 11.0F, 14.3F, // 2
        1.0F, 4.2F, 7.3F, 10.5F, 13.5F, // 3
        1.0F, 3.8F, 6.7F, 9.5F,  12.4F, // 4
        1.0F, 3.5F, 5.8F, 8.2F,  10.6F, // 5
        1.0F, 2.7F, 4.5F, 6.2F,  9.7F   // 7
    };

    const RelativeRiskLookup lookup{ages, values, FloatArray2D{7, 5, data}};

    const std::map<double, double> expected{
        {16.0, 1.00},                {16.5, 1.00},
        {17.0, 1.0},                 {17.5, 1.00},
        {18.0, 1.00},                {18.5, 1.2285714285714286},
        {19.0, 1.4571428571428573},  {19.5, 1.6857142857142859},
        {20.0, 1.9142857142857144},  {20.5, 2.1428571428571428},
        {21.0, 2.3714285714285719},  {21.5, 2.60},
        {22.0, 2.8285714285714287},  {22.5, 3.0571428571428574},
        {23.0, 3.2857142857142856},  {23.5, 3.5142857142857147},
        {24.0, 3.7428571428571433},  {24.5, 3.9714285714285715},
        {25.0, 4.2},                 {25.5, 4.51},
        {26.0, 4.82},                {26.5, 5.13},
        {27.0, 5.440},               {27.5, 5.75},
        {28.0, 6.06},                {28.5, 6.37},
        {29.0, 6.68},                {29.5, 6.99},
        {30.0, 7.30},                {30.5, 7.62},
        {31.0, 7.94},                {31.5, 8.26},
        {32.0, 8.58},                {32.5, 8.90},
        {33.0, 9.22},                {33.5, 9.54},
        {34.0, 9.86},                {34.5, 10.18},
        {35.0, 10.5},                {35.5, 10.80},
        {36.0, 11.10},               {36.5, 11.40},
        {37.0, 11.7},                {37.5, 12.00},
        {38.0, 12.3},                {38.5, 12.60},
        {39.0, 12.90},               {39.5, 13.20},
        {40.0, 13.5},                {40.5, 13.50},
        {41.0, 13.5},                {41.5, 13.50}};

    for (const auto &[value, relative_risk] : expected) {
        EXPECT_FLOAT_EQ(static_cast<float>(relative_risk),
                        lookup(3, static_cast<float>(value)))
            << "at BMI " << value;
    }
}

TEST(TestHealthGPS_Disease, DiseaseParameterAndDefinition) {
    const ParameterLookup prevalence{{0, {1.0, 1.0}}, {1, {0.5, 0.5}}, {2, {0.25, 0.25}}};
    const ParameterLookup survival{{0, {0.9, 0.95}}};
    const ParameterLookup deaths{{0, {0.1, 0.05}}};

    const DiseaseParameter parameter{2019, prevalence, survival, deaths};

    EXPECT_EQ(2019, parameter.time_year);
    // One past the highest time-since-onset in the prevalence distribution.
    EXPECT_EQ(3, parameter.max_time_since_onset);
    EXPECT_FALSE(parameter.empty());

    const DiseaseParameter incomplete{};
    EXPECT_TRUE(incomplete.empty());
    EXPECT_EQ(0, incomplete.max_time_since_onset);

    const DiseaseDefinition definition{make_table(), {}, {}, parameter};
    EXPECT_EQ(Identifier{"asthma"}, definition.identifier().code);
    EXPECT_EQ(4U, definition.table().rows());
    EXPECT_TRUE(definition.relative_risk_diseases().empty());
    EXPECT_TRUE(definition.relative_risk_factors().empty());
    EXPECT_EQ(2019, definition.parameters().time_year);
}

namespace {

/// An LMS reference over ages 0..20 with a mean BMI that grows with age.
LmsDefinition make_lms() {
    LmsDataset dataset;
    for (unsigned int age = 0; age <= 20; ++age) {
        for (const auto gender : {Gender::male, Gender::female}) {
            dataset[age][gender] =
                LmsRecord{.lambda = -1.0, .mu = 16.0 + 0.4 * age, .sigma = 0.1};
        }
    }
    return LmsDefinition{dataset};
}

Person person_aged(unsigned int age, double bmi) {
    Person person{Gender::male, 1};
    person.age = age;
    person.risk_factors[Identifier{"bmi"}] = bmi;
    return person;
}

} // namespace

TEST(WeightModelTest, RejectsAnEmptyOrTooNarrowLmsTable) {
    EXPECT_THROW(LmsDefinition(LmsDataset{}), std::invalid_argument);

    // A reference that stops before the child cut-off age would leave some children
    // unclassifiable, so it is rejected at construction rather than mid-run.
    LmsDataset narrow;
    for (unsigned int age = 0; age <= 5; ++age) {
        narrow[age][Gender::male] = LmsRecord{.lambda = -1.0, .mu = 16.0, .sigma = 0.1};
    }
    EXPECT_THROW(WeightModel(LmsDefinition{narrow}), std::invalid_argument);
}

TEST(WeightModelTest, ClassifiesAdultsByBmiCutOffs) {
    const WeightModel model{make_lms()};

    EXPECT_EQ(18U, model.child_cutoff_age());
    EXPECT_EQ(WeightCategory::normal, model.classify_weight(person_aged(40, 22.0)));
    EXPECT_EQ(WeightCategory::overweight, model.classify_weight(person_aged(40, 25.0)));
    EXPECT_EQ(WeightCategory::overweight, model.classify_weight(person_aged(40, 29.9)));
    EXPECT_EQ(WeightCategory::obese, model.classify_weight(person_aged(40, 30.0)));
}

TEST(WeightModelTest, ClassifiesChildrenByLmsZScore) {
    const auto lms = make_lms();
    const WeightModel model{lms};

    const unsigned int age = 10;
    const auto &record = lms.at(age, Gender::male);

    // Invert the LMS transform to build a BMI at an exact z-score.
    const auto bmi_at_zscore = [&record](double zscore) {
        return record.mu * std::pow(1.0 + record.lambda * record.sigma * zscore,
                                    1.0 / record.lambda);
    };

    EXPECT_EQ(WeightCategory::normal, model.classify_weight(person_aged(age, bmi_at_zscore(0.5))));
    EXPECT_EQ(WeightCategory::overweight,
              model.classify_weight(person_aged(age, bmi_at_zscore(1.5))));
    EXPECT_EQ(WeightCategory::obese, model.classify_weight(person_aged(age, bmi_at_zscore(2.5))));
}

TEST(WeightModelTest, AdjustsAChildsBmiToItsCategoryMidpoint) {
    const auto lms = make_lms();
    const WeightModel model{lms};

    const unsigned int age = 10;
    const auto &record = lms.at(age, Gender::male);
    const auto bmi_at_zscore = [&record](double zscore) {
        return record.mu * std::pow(1.0 + record.lambda * record.sigma * zscore,
                                    1.0 / record.lambda);
    };

    const auto bmi_key = Identifier{"bmi"};
    const auto child = person_aged(age, 0.0);

    EXPECT_DOUBLE_EQ(22.5,
                     model.adjust_risk_factor_value(child, bmi_key, bmi_at_zscore(0.0)));
    EXPECT_DOUBLE_EQ(27.5,
                     model.adjust_risk_factor_value(child, bmi_key, bmi_at_zscore(1.5)));
    EXPECT_DOUBLE_EQ(35.0,
                     model.adjust_risk_factor_value(child, bmi_key, bmi_at_zscore(2.5)));

    // An adult's BMI is untouched, and so is any other factor.
    const auto adult = person_aged(40, 0.0);
    EXPECT_DOUBLE_EQ(31.0, model.adjust_risk_factor_value(adult, bmi_key, 31.0));
    EXPECT_DOUBLE_EQ(9.0, model.adjust_risk_factor_value(child, Identifier{"energy"}, 9.0));
}

TEST(WeightModelTest, LmsWithZeroLambdaUsesTheLogForm) {
    LmsDataset dataset;
    for (unsigned int age = 0; age <= 20; ++age) {
        for (const auto gender : {Gender::male, Gender::female}) {
            dataset[age][gender] = LmsRecord{.lambda = 0.0, .mu = 18.0, .sigma = 0.1};
        }
    }

    const WeightModel model{LmsDefinition{dataset}};

    // z = log(bmi / mu) / sigma, so a z of 1.5 is mu * exp(1.5 * sigma).
    const double overweight_bmi = 18.0 * std::exp(1.5 * 0.1);
    EXPECT_EQ(WeightCategory::overweight, model.classify_weight(person_aged(10, overweight_bmi)));

    const double obese_bmi = 18.0 * std::exp(2.5 * 0.1);
    EXPECT_EQ(WeightCategory::obese, model.classify_weight(person_aged(10, obese_bmi)));
}

TEST(WeightModelTest, WeightCategoryNames) {
    EXPECT_EQ("normal", weight_category_to_string(WeightCategory::normal));
    EXPECT_EQ("overweight", weight_category_to_string(WeightCategory::overweight));
    EXPECT_EQ("obese", weight_category_to_string(WeightCategory::obese));
}

TEST(WeightModelTest, AMissingLmsRowForAChildIsAnInternalError) {
    // Every age up to the cut-off must be present for both sexes; a gap is a broken data
    // assumption rather than something to paper over with a nearby row.
    LmsDataset dataset;
    for (unsigned int age = 0; age <= 20; ++age) {
        dataset[age][Gender::male] = LmsRecord{.lambda = -1.0, .mu = 18.0, .sigma = 0.1};
    }

    const WeightModel model{LmsDefinition{dataset}};

    Person girl{Gender::female, 1};
    girl.age = 5;
    girl.risk_factors[Identifier{"bmi"}] = 17.0;

    EXPECT_THROW(model.classify_weight(girl), hgps::diag::InternalError);
}
