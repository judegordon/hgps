// Ported from the baseline's src/HealthGPS.Tests/PredictorResolver.Test.cpp (suites
// TestHealthGPS_PredictorResolver, 12 tests, and TestHealthGPS_LinearModelEvaluator, 6), keeping
// their values. Two adaptations: HgpsException and std::out_of_range become diag::InternalError,
// and the linear model's coefficients are an ordered map so that the sum has a stated order.
#include "model/predictor_resolver.h"
#include "model/riskfactor/linear_model.h"

#include "diagnostics/internal_error.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

using hgps::core::Gender;
using hgps::core::Identifier;
using hgps::core::Sector;
using hgps::diag::InternalError;
using hgps::model::LinearModelEvalOptions;
using hgps::model::LinearModelParams;
using hgps::model::Person;

double expect_resolved(const Person &person, const std::string &key) {
    const auto value = hgps::model::resolve_derived_predictor(person, key);
    EXPECT_TRUE(value.has_value()) << key;
    return value.value_or(std::numeric_limits<double>::quiet_NaN());
}

} // namespace

TEST(TestHealthGPS_PredictorResolver, IncomePolynomialAndLog) {
    Person person;
    person.risk_factors[Identifier{"income"}] = 100.0;

    EXPECT_DOUBLE_EQ(100.0, expect_resolved(person, "income"));
    EXPECT_DOUBLE_EQ(10000.0, expect_resolved(person, "income2"));
    EXPECT_NEAR(std::log(100.0), expect_resolved(person, "log_income"), 1e-9);

    const double log_income = std::log(100.0);
    EXPECT_NEAR(log_income * log_income, expect_resolved(person, "log_income2"), 1e-9);
}

TEST(TestHealthGPS_PredictorResolver, RegionDummy) {
    Person person;
    person.region = "region2";

    EXPECT_DOUBLE_EQ(1.0, expect_resolved(person, "region2"));
    EXPECT_DOUBLE_EQ(0.0, expect_resolved(person, "region3"));
}

TEST(TestHealthGPS_PredictorResolver, GetRiskFactorValueUsesResolver) {
    Person person;
    person.risk_factors[Identifier{"income"}] = 10.0;

    EXPECT_DOUBLE_EQ(100.0, person.get_risk_factor_value(Identifier{"income2"}));
}

TEST(TestHealthGPS_PredictorResolver, UnknownPredictorThrows) {
    const Person person;
    EXPECT_THROW(person.get_risk_factor_value(Identifier{"not_a_real_predictor"}), InternalError);
}

TEST(TestHealthGPS_PredictorResolver, AgeEthnicityGenderSector) {
    Person person;
    person.age = 5;
    person.gender = Gender::male;
    person.ethnicity = "ethnicity3";
    person.region = "region1";
    person.sector = Sector::urban;

    EXPECT_DOUBLE_EQ(5.0, expect_resolved(person, "age"));
    EXPECT_DOUBLE_EQ(25.0, expect_resolved(person, "age2"));
    EXPECT_DOUBLE_EQ(1.0, expect_resolved(person, "ethnicity3"));
    EXPECT_DOUBLE_EQ(0.0, expect_resolved(person, "ethnicity2"));
    EXPECT_DOUBLE_EQ(static_cast<double>(person.gender_to_value()),
                     expect_resolved(person, "gender"));
    EXPECT_DOUBLE_EQ(static_cast<double>(person.sector_to_value()),
                     expect_resolved(person, "sector"));
    EXPECT_DOUBLE_EQ(std::pow(static_cast<double>(person.sector_to_value()), 2),
                     expect_resolved(person, "sector2"));

    LinearModelEvalOptions gender2_options;
    gender2_options.gender2_indicator = Gender::male;
    EXPECT_DOUBLE_EQ(
        1.0, hgps::model::get_linear_predictor_value(person, Identifier{"gender2"},
                                                     gender2_options));
}

TEST(TestHealthGPS_PredictorResolver, Gender2RegressionIndicator) {
    Person male;
    male.gender = Gender::male;
    Person female;
    female.gender = Gender::female;

    LinearModelEvalOptions male_indicator;
    male_indicator.gender2_indicator = Gender::male;
    LinearModelEvalOptions female_indicator;
    female_indicator.gender2_indicator = Gender::female;

    const auto value = [](const Person &person, const LinearModelEvalOptions &options) {
        return hgps::model::get_linear_predictor_value(person, Identifier{"gender2"}, options);
    };

    EXPECT_DOUBLE_EQ(1.0, value(male, male_indicator));
    EXPECT_DOUBLE_EQ(0.0, value(female, male_indicator));
    EXPECT_DOUBLE_EQ(0.0, value(male, female_indicator));
    EXPECT_DOUBLE_EQ(1.0, value(female, female_indicator));
}

TEST(TestHealthGPS_PredictorResolver, ParseGender2Indicator) {
    EXPECT_EQ(Gender::male, hgps::model::parse_gender2_indicator("male"));
    EXPECT_EQ(Gender::female, hgps::model::parse_gender2_indicator("Female"));
    EXPECT_THROW(hgps::model::parse_gender2_indicator("other"), InternalError);
}

TEST(TestHealthGPS_PredictorResolver, IncomeFromContinuousWhenRiskFactorMissing) {
    Person person;
    person.income_continuous = 42.0;

    EXPECT_DOUBLE_EQ(42.0, expect_resolved(person, "income"));
    EXPECT_DOUBLE_EQ(42.0 * 42.0, expect_resolved(person, "income2"));
}

TEST(TestHealthGPS_PredictorResolver, LogEnergyIntakeFromEnergyIntakeRiskFactor) {
    Person person;
    person.risk_factors[Identifier{"EnergyIntake"}] = 2000.0;

    EXPECT_NEAR(std::log(2000.0), expect_resolved(person, "log_energyintake"), 1e-9);
}

TEST(TestHealthGPS_PredictorResolver, EnergyIntakeDirectLookupCaseInsensitive) {
    Person person;
    // Identifiers lower-case on construction, so "EnergyIntake" and "energyintake" are the same
    // key — which is why the resolver only has to look for one spelling.
    person.risk_factors[Identifier{"EnergyIntake"}] = 1800.0;
    EXPECT_DOUBLE_EQ(1800.0, expect_resolved(person, "energyintake"));

    person.risk_factors[Identifier{"energyintake"}] = 1500.0;
    EXPECT_DOUBLE_EQ(1500.0, expect_resolved(person, "energyintake"));
}

TEST(TestHealthGPS_PredictorResolver, LogEnergyIntakeAliasAndIncomeContinuous) {
    Person person;
    person.risk_factors[Identifier{"EnergyIntake"}] = 100.0;
    EXPECT_NEAR(std::log(100.0), expect_resolved(person, "log_energyintake"), 1e-9);

    person.income_continuous = 99.0;
    EXPECT_DOUBLE_EQ(99.0, expect_resolved(person, "income_continuous"));

    person.region = "region2";
    EXPECT_DOUBLE_EQ(1.0, expect_resolved(person, "region2"));
}

TEST(TestHealthGPS_PredictorResolver, MetadataPredictorReturnsNullopt) {
    const Person person;

    EXPECT_FALSE(hgps::model::resolve_derived_predictor(person, "stddev").has_value());
    EXPECT_FALSE(hgps::model::resolve_derived_predictor(person, "Intercept").has_value());
    EXPECT_TRUE(hgps::model::is_metadata_predictor(std::string{"min"}));
    EXPECT_TRUE(hgps::model::is_metadata_predictor(Identifier{"lambda"}));
}

// Added here: the load-time check that makes docs/decisions/0018 enforceable. The earlier
// rewrite swallowed an unresolvable predictor and substituted an expected value, so a misspelled
// coefficient name produced plausible numbers (audit R-05).
TEST(TestHealthGPS_PredictorResolver, ResolvablePredictorNamesAreRecognisedAtLoadTime) {
    const std::vector<Identifier> factors{Identifier{"bmi"}, Identifier{"energy"},
                                          Identifier{"sodium"}};

    for (const auto *name : {"bmi", "BMI", "energy", "age", "age2", "age3", "gender", "gender2",
                             "income", "income2", "income_continuous", "log_energy", "ses",
                             "sector", "region1", "ethnicity2", "intercept", "stddev"}) {
        EXPECT_TRUE(hgps::model::is_resolvable_predictor(name, factors)) << name;
    }

    for (const auto *name : {"enegry", "bmii", "not_a_predictor", ""}) {
        EXPECT_FALSE(hgps::model::is_resolvable_predictor(name, factors)) << name;
    }
}

TEST(TestHealthGPS_PredictorResolver, NearestNamesSuggestTheLikelyTypo) {
    const std::vector<Identifier> factors{Identifier{"energy"}, Identifier{"sodium"},
                                          Identifier{"bmi"}};

    const auto suggestions = hgps::model::nearest_predictor_names("enegry", factors);
    ASSERT_FALSE(suggestions.empty());
    EXPECT_EQ("energy", suggestions.front());

    // Nothing close enough is no suggestion at all, rather than a misleading one.
    EXPECT_TRUE(hgps::model::nearest_predictor_names("totally_unrelated_name", factors).empty());
}

TEST(TestHealthGPS_LinearModelEvaluator, SkipsMetadataRows) {
    Person person;
    person.age = 20;
    person.gender = Gender::male;
    person.region = "region1";
    person.ethnicity = "ethnicity1";

    LinearModelParams model;
    model.intercept = 5.0;
    model.coefficients[Identifier{"age1"}] = 2.0;
    model.coefficients[Identifier{"stddev"}] = 99.0;
    model.coefficients[Identifier{"min"}] = 1.0;

    EXPECT_DOUBLE_EQ(5.0 + (2.0 * 20.0), hgps::model::evaluate_linear_model(person, model));
}

TEST(TestHealthGPS_LinearModelEvaluator, CappedAgeOption) {
    Person person;
    person.age = 100;

    LinearModelParams model;
    model.coefficients[Identifier{"age2"}] = 1.0;

    LinearModelEvalOptions options;
    options.capped_age = 50.0;

    EXPECT_DOUBLE_EQ(2500.0, hgps::model::evaluate_linear_model(person, model, options));
}

TEST(TestHealthGPS_LinearModelEvaluator, CappedAgePredictorDirect) {
    Person person;
    person.age = 100;

    LinearModelEvalOptions options;
    options.capped_age = 40.0;

    const auto value = [&](const char *name) {
        return hgps::model::get_linear_predictor_value(person, Identifier{name}, options);
    };

    EXPECT_DOUBLE_EQ(40.0, value("age"));
    EXPECT_DOUBLE_EQ(1600.0, value("age2"));
    EXPECT_DOUBLE_EQ(64000.0, value("age3"));
}

TEST(TestHealthGPS_LinearModelEvaluator, LogCoefficientsAndFallback) {
    Person person;
    person.risk_factors[Identifier{"foodcarbohydrate"}] = 4.0;

    LinearModelParams model;
    model.intercept = 1.0;
    model.log_coefficients[Identifier{"foodcarbohydrate"}] = 2.0;
    model.coefficients[Identifier{"missing_factor"}] = 3.0;

    LinearModelEvalOptions options;
    options.missing_predictor_fallback = [](const Identifier &name) -> std::optional<double> {
        if (name == Identifier{"missing_factor"}) {
            return 5.0;
        }
        return std::nullopt;
    };

    const double expected = 1.0 + (2.0 * std::log(4.0)) + (3.0 * 5.0);
    EXPECT_NEAR(expected, hgps::model::evaluate_linear_model(person, model, options), 1e-9);
}

TEST(TestHealthGPS_LinearModelEvaluator, LogCoefficientUsesFloorForNonPositive) {
    Person person;
    person.risk_factors[Identifier{"foodcarbohydrate"}] = 0.0;

    LinearModelParams model;
    model.log_coefficients[Identifier{"foodcarbohydrate"}] = 1.0;

    EXPECT_NEAR(std::log(1e-10), hgps::model::evaluate_linear_model(person, model), 1e-12);
}

TEST(TestHealthGPS_LinearModelEvaluator, Gender2InRegressionModel) {
    Person female;
    female.gender = Gender::female;

    LinearModelParams model;
    model.intercept = 10.0;
    model.coefficients[Identifier{"gender2"}] = 2.5;

    LinearModelEvalOptions options;
    options.gender2_indicator = Gender::female;

    EXPECT_NEAR(12.5, hgps::model::evaluate_linear_model(female, model, options), 1e-9);
}

TEST(TestHealthGPS_LinearModelEvaluator, AnUnresolvablePredictorThrowsRatherThanBeingSkipped) {
    // The earlier rewrite substituted an expected value here and carried on (audit R-05).
    Person person;

    LinearModelParams model;
    model.coefficients[Identifier{"nonsense_factor"}] = 1.0;

    EXPECT_THROW(hgps::model::evaluate_linear_model(person, model), InternalError);

    LinearModelParams log_model;
    log_model.log_coefficients[Identifier{"nonsense_factor"}] = 1.0;
    EXPECT_THROW(hgps::model::evaluate_linear_model(person, log_model), InternalError);
}

TEST(TestHealthGPS_LinearModelEvaluator, TheSumOrderIsTheCoefficientNameOrder) {
    // The baseline stores coefficients in an unordered_map and sums over its iteration order, so
    // the last bits of every linear model depend on the standard library's bucket layout. An
    // ordered map states the order. This checks the property the ordering is for: the same
    // coefficients inserted in a different sequence give the identical double.
    Person person;
    person.age = 33;
    person.ses = 0.25;
    person.risk_factors[Identifier{"bmi"}] = 27.5;

    LinearModelParams forward;
    forward.intercept = 1.0;
    forward.coefficients[Identifier{"age"}] = 1e-8;
    forward.coefficients[Identifier{"bmi"}] = 1e16;
    forward.coefficients[Identifier{"ses"}] = 1.0;

    LinearModelParams reverse;
    reverse.intercept = 1.0;
    reverse.coefficients[Identifier{"ses"}] = 1.0;
    reverse.coefficients[Identifier{"bmi"}] = 1e16;
    reverse.coefficients[Identifier{"age"}] = 1e-8;

    EXPECT_EQ(hgps::model::evaluate_linear_model(person, forward),
              hgps::model::evaluate_linear_model(person, reverse));
}
