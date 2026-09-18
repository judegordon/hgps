// Ported from the baseline's src/HealthGPS.Tests/PredictorResolver.Test.cpp (suites
// TestHealthGPS_PredictorResolver, 12 tests, and TestHealthGPS_LinearModelEvaluator, 6), keeping
// their values. Two adaptations: HgpsException and std::out_of_range become diag::InternalError,
// and the linear model's coefficients are an ordered map so that the sum has a stated order.
#include "model/predictor_resolver.h"
#include "model/factor_values.h"
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

// --- resolving the coefficient names once ---------------------------------------------------

namespace {

/// A model with one of every kind of predictor in it: a stored factor, a derived one the named
/// table answers, a derived one only the string resolver answers, a metadata row, an age term and
/// the gender2 dummy.
LinearModelParams every_kind_of_predictor() {
    LinearModelParams model;
    model.intercept = 1.5;
    model.coefficients[Identifier{"bmi"}] = 2.0;       // stored on the person
    model.coefficients[Identifier{"age2"}] = 1e-3;     // the indexed dispatcher
    model.coefficients[Identifier{"ses"}] = 0.5;       // the indexed dispatcher
    model.coefficients[Identifier{"income2"}] = 0.25;  // only the string resolver
    model.coefficients[Identifier{"gender2"}] = 3.0;   // the evaluator itself
    model.coefficients[Identifier{"stddev"}] = 99.0;   // metadata, skipped
    model.log_coefficients[Identifier{"bmi"}] = 0.75;
    return model;
}

Person a_person() {
    Person person;
    person.age = 41;
    person.gender = Gender::female;
    person.ses = 0.125;
    person.income = hgps::core::Income::middle;
    person.risk_factors[Identifier{"bmi"}] = 26.25;
    return person;
}

} // namespace

TEST(LinearModelResolution, AResolvedModelEvaluatesToTheSameBitsAsAnUnresolvedOne) {
    // The whole safety argument for `resolve_predictors` in one assertion: the index it stores is
    // exactly what the evaluator would have looked up, so hoisting the lookup out of the per-person
    // loop cannot change an answer. Not `EXPECT_NEAR` — the same bits.
    const auto person = a_person();
    LinearModelEvalOptions options;
    options.gender2_indicator = Gender::female;

    const auto unresolved = every_kind_of_predictor();
    auto resolved = every_kind_of_predictor();
    hgps::model::resolve_predictors(resolved);

    EXPECT_EQ(hgps::model::evaluate_linear_model(person, unresolved, options),
              hgps::model::evaluate_linear_model(person, resolved, options));
}

TEST(LinearModelResolution, ItResolvesOneIndexPerCoefficientInTheMapsOwnOrder) {
    auto model = every_kind_of_predictor();
    hgps::model::resolve_predictors(model);

    ASSERT_EQ(model.coefficients.size(), model.resolved_coefficients.size());
    ASSERT_EQ(model.log_coefficients.size(), model.resolved_log_coefficients.size());

    // Positional, because the map's order is the summation order and a second map keyed by name
    // would put back the lookup this removes.
    std::size_t position = 0;
    for (const auto &[name, coefficient] : model.coefficients) {
        const auto &resolved = model.resolved_coefficients[position];
        EXPECT_EQ(hgps::model::factor_index().find(name), resolved.index) << name.to_string();
        EXPECT_EQ(hgps::model::is_metadata_predictor(name.to_string()), resolved.metadata)
            << name.to_string();
        ++position;
    }

    // And the three name-shaped questions, answered once here instead of per person per year.
    const auto of = [&model](const char *name) {
        std::size_t at = 0;
        for (const auto &[key, coefficient] : model.coefficients) {
            if (key.to_string() == name) {
                return model.resolved_coefficients[at];
            }
            ++at;
        }
        ADD_FAILURE() << name << " is not in the model";
        return LinearModelParams::ResolvedPredictor{};
    };
    EXPECT_EQ(2, of("age2").age_power);
    EXPECT_TRUE(of("gender2").gender2);
    EXPECT_TRUE(of("stddev").metadata);
    EXPECT_EQ(0, of("bmi").age_power);
    EXPECT_FALSE(of("bmi").gender2);
    EXPECT_FALSE(of("bmi").metadata);
}

TEST(LinearModelResolution, ItIsIdempotentAndSurvivesTheCoefficientsChanging) {
    auto model = every_kind_of_predictor();
    hgps::model::resolve_predictors(model);
    const auto first = model.resolved_coefficients.front().index;
    hgps::model::resolve_predictors(model);
    EXPECT_EQ(first, model.resolved_coefficients.front().index);

    // A coefficient added afterwards leaves the vector the wrong length, and the evaluator falls
    // back to resolving names rather than trusting indices that are not this model's. Adding one
    // and evaluating must still be right.
    const auto person = a_person();
    const auto before = hgps::model::evaluate_linear_model(person, model);
    model.coefficients[Identifier{"age"}] = 1.0;
    EXPECT_NE(model.coefficients.size(), model.resolved_coefficients.size());
    EXPECT_DOUBLE_EQ(before + 41.0, hgps::model::evaluate_linear_model(person, model));
}

TEST(LinearModelResolution, AModelResolvedBeforeAnyPersonExistsStillFindsTheirFactors) {
    // The order a model is loaded in: resolve first, meet the people afterwards. A risk-factor name
    // is often interned by nothing at all when its model is built, and a name the index table has
    // not seen can only be answered by the string resolver — so resolving with `find` would freeze
    // the model into that path and the value would come from the fallback, or from nowhere.
    //
    // The name here is one nothing else in this binary uses, so the ordering is the test rather
    // than an accident of which test ran first. A whole `KevinHall_FINCH` run was byte-identical
    // with the defect present, because something else had interned every name it happened to use.
    const Identifier factor{"a_factor_no_other_test_names"};

    LinearModelParams model;
    model.coefficients[factor] = 2.0;
    hgps::model::resolve_predictors(model);

    Person person;
    person.age = 30;
    person.risk_factors[factor] = 11.0;

    EXPECT_DOUBLE_EQ(22.0, hgps::model::evaluate_linear_model(person, model));
}

TEST(LinearModelResolution, ResolvingBeforeTheDerivedNamesExistWouldBeTheOldDefect) {
    // `age` is one of the nineteen derived-predictor names, and a name the index table has never
    // interned can only be answered by the string resolver. `resolve_predictors` interns them
    // first, so `age` gets a real index rather than `unknown` — which is what stops the resolution
    // freezing a predictor into the slow branch for the life of the model.
    LinearModelParams model;
    model.coefficients[Identifier{"age"}] = 1.0;
    hgps::model::resolve_predictors(model);

    ASSERT_EQ(1U, model.resolved_coefficients.size());
    EXPECT_EQ(hgps::model::factor_index().find(Identifier{"age"}),
              model.resolved_coefficients[0].index);
    EXPECT_NE(hgps::model::FactorIndex::unknown, model.resolved_coefficients[0].index);
}
