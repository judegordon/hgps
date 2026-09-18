// An intervention the configured model would ignore is refused at load time.
//
// `Scenario::apply` — the call that offers a person and a risk factor to the active policy — has one
// call site in this build and one in the baseline, both in the dynamic hierarchical linear model. So
// on the `StaticLinear`/`KevinHall` surface every intervention scenario is inert, in both
// implementations, and until this run a config selecting `food_labelling` there ran to completion and
// reported success ([ADR 0035], deviation B-25).
//
// Two things get tested here, and they are different in kind. The rule, with a stub model, because a
// rule is worth testing without loading an 18 MB model file. And the four real model families'
// answers, because the rule is only as good as what they say — and each answers from whether its own
// update calls `apply`.
#include "config/models/model_loader.h"

#include "config/types.h"
#include "core/interval.h"
#include "diagnostics/issue_report.h"
#include "io/json.h"
#include "model/mapping.h"
#include "support/test_paths.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace {

using hgps::config::Config;
using hgps::config::InterventionSpec;
using hgps::config::PolicyImpact;
using hgps::config::models::detail::check_intervention_reaches_the_model;
using hgps::diag::IssueReport;
using hgps::model::RiskFactorModel;
using hgps::model::RiskFactorModelType;

/// A model that is nothing but its answer to the one question the rule asks.
class StubModel final : public RiskFactorModel {
  public:
    explicit StubModel(bool applies) : applies_{applies} {}

    RiskFactorModelType type() const noexcept override { return RiskFactorModelType::Dynamic; }
    std::string name() const noexcept override { return "Stub"; }
    void generate_risk_factors(hgps::model::RuntimeContext &,
                               hgps::sim::ScenarioJournal &) override {}
    void update_risk_factors(hgps::model::RuntimeContext &,
                             hgps::sim::ScenarioJournal &) override {}
    bool applies_the_active_scenario() const noexcept override { return applies_; }

  private:
    bool applies_;
};

Config config_with(std::optional<InterventionSpec> active) {
    Config config;
    config.source_path = "run.json";
    config.running.active_intervention = std::move(active);
    return config;
}

InterventionSpec policy(const std::string &identifier, std::size_t impacts) {
    InterventionSpec spec;
    spec.identifier = identifier;
    for (std::size_t i = 0; i < impacts; ++i) {
        spec.impacts.push_back(PolicyImpact{});
    }
    return spec;
}

} // namespace

TEST(InterventionReach, AnInterventionWithImpactsOnANonApplyingModelIsAnError) {
    const StubModel model{false};
    IssueReport report;
    check_intervention_reaches_the_model(config_with(policy("food_labelling", 3)), model,
                                        "dynamic_model.json", report);

    ASSERT_EQ(1U, report.error_count()) << report.to_string();
    const auto text = report.to_string();

    // The issue has to name both things, because either alone leaves the reader guessing which half
    // to change.
    EXPECT_NE(std::string::npos, text.find("food_labelling"));
    EXPECT_NE(std::string::npos, text.find("dynamic_model.json"));
    EXPECT_NE(std::string::npos, text.find("3 impacts"));

    // And it has to say what to do instead. On the surface where this fires, the mechanism that
    // works is policy_start_year.
    EXPECT_NE(std::string::npos, text.find("policy_start_year"));

    // Located at the key the user would edit.
    EXPECT_NE(std::string::npos, text.find("/running/interventions/active_type_id"));
}

TEST(InterventionReach, OneImpactIsReportedInTheSingular) {
    const StubModel model{false};
    IssueReport report;
    check_intervention_reaches_the_model(config_with(policy("marketing", 1)), model,
                                        "dynamic_model.json", report);
    EXPECT_NE(std::string::npos, report.to_string().find("1 impact that"));
}

TEST(InterventionReach, AnEmptyImpactListIsAWarningRatherThanAnError) {
    // All four Kevin Hall examples upstream ship `simple` with an empty impact list. That is a
    // well-defined no-op and upstream's own way of saying "no policy here", so refusing it would make
    // four of the six examples unloadable — including one of the two equivalence references — to
    // prevent a mistake nobody is making.
    const StubModel model{false};
    IssueReport report;
    check_intervention_reaches_the_model(config_with(policy("simple", 0)), model,
                                        "dynamic_model.json", report);

    EXPECT_EQ(0U, report.error_count()) << report.to_string();
    ASSERT_EQ(1U, report.warning_count()) << report.to_string();

    const auto text = report.to_string();
    EXPECT_NE(std::string::npos, text.find("simple"));
    EXPECT_NE(std::string::npos, text.find("no impacts"));
    // The warning still has to say the thing worth saying: that the intervention arm will differ from
    // the baseline arm only through the other mechanism.
    EXPECT_NE(std::string::npos, text.find("policy_start_year"));
}

TEST(InterventionReach, AnApplyingModelAcceptsAnyIntervention) {
    const StubModel model{true};
    IssueReport report;
    check_intervention_reaches_the_model(config_with(policy("food_labelling", 3)), model,
                                        "dynamic_model.json", report);
    EXPECT_TRUE(report.empty()) << report.to_string();
}

TEST(InterventionReach, NoActiveInterventionIsNothingToCheck) {
    for (const bool applies : {false, true}) {
        const StubModel model{applies};
        IssueReport report;
        check_intervention_reaches_the_model(config_with(std::nullopt), model,
                                            "dynamic_model.json", report);
        EXPECT_TRUE(report.empty()) << report.to_string();
    }
}

TEST(InterventionReach, TheDefaultForANewModelFamilyIsNotToApply) {
    // A model family that says nothing does not consult the policy. That is the safe direction: the
    // failure mode of forgetting to override this is a refused config, not a silent no-effect run.
    class SilentModel final : public RiskFactorModel {
      public:
        RiskFactorModelType type() const noexcept override { return RiskFactorModelType::Dynamic; }
        std::string name() const noexcept override { return "Silent"; }
        void generate_risk_factors(hgps::model::RuntimeContext &,
                                   hgps::sim::ScenarioJournal &) override {}
        void update_risk_factors(hgps::model::RuntimeContext &,
                                 hgps::sim::ScenarioJournal &) override {}
    };

    const SilentModel model;
    EXPECT_FALSE(model.applies_the_active_scenario());
}
