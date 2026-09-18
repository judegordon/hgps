// The reduction the charting endpoint does, away from a socket.
//
// It is the one place the server computes rather than reports, and it has to agree with the rule
// docs/equivalence-method.md reduces by — a chart that disagreed with the harness about what
// `mean_bmi` means would be worse than no chart (ADR 0042).
#include "summary.h"

#include "support/test_paths.h"

#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using hgps::server::SummaryFilter;
using hgps::server::summarise_results;

std::filesystem::path write_csv(const std::string &name, const std::string &contents) {
    const auto path = hgps::test::scratch_dir("summary_" + name) / "result.csv";
    std::ofstream stream{path, std::ios::trunc};
    stream << contents;
    return path;
}

/// @brief Two age bands per (scenario, year, sex), with head counts that differ — so a weighted
///        mean and an unweighted one give different answers and the test can tell them apart.
constexpr const char *kTwoBands = R"(source,run,time,gender_name,index_id,count,deaths,mean_bmi
Baseline,1,2010,male,0,10,1,20
Baseline,1,2010,male,1,90,2,30
Baseline,1,2011,male,0,10,0,21
Baseline,1,2011,male,1,90,3,31
Intervention,1,2010,male,0,10,1,10
Intervention,1,2010,male,1,90,2,20
Intervention,1,2011,male,0,10,0,11
Intervention,1,2011,male,1,90,3,21
)";

const nlohmann::json *series_for(const nlohmann::json &document, const std::string &scenario,
                                 const std::string &variable) {
    for (const auto &series : document.at("series")) {
        if (series.at("scenario") == scenario && series.at("variable") == variable) {
            return &series;
        }
    }
    return nullptr;
}

} // namespace

TEST(SummaryReduction, AMeanIsWeightedByHeadCount) {
    const auto document = summarise_results(write_csv("weighted", kTwoBands), SummaryFilter{});
    const auto *bmi = series_for(document, "Baseline", "mean_bmi");
    ASSERT_NE(nullptr, bmi);

    // (10*20 + 90*30) / 100 = 29, not the unweighted 25. Getting this wrong is the single easiest
    // way to draw a chart that disagrees with the harness.
    EXPECT_DOUBLE_EQ(29.0, bmi->at("values").at(0).get<double>());
    EXPECT_DOUBLE_EQ(30.0, bmi->at("values").at(1).get<double>());
}

TEST(SummaryReduction, ACountIsSummedRatherThanAveraged) {
    const auto document = summarise_results(write_csv("counted", kTwoBands), SummaryFilter{});
    const auto *deaths = series_for(document, "Baseline", "deaths");
    ASSERT_NE(nullptr, deaths);
    EXPECT_DOUBLE_EQ(3.0, deaths->at("values").at(0).get<double>());
    EXPECT_DOUBLE_EQ(3.0, deaths->at("values").at(1).get<double>());

    const auto *count = series_for(document, "Baseline", "count");
    ASSERT_NE(nullptr, count) << "count is a variable a chart wants, not only a weight";
    EXPECT_DOUBLE_EQ(100.0, count->at("values").at(0).get<double>());
}

TEST(SummaryReduction, ValuesAreParallelToYears) {
    const auto document = summarise_results(write_csv("parallel", kTwoBands), SummaryFilter{});
    const auto years = document.at("years");
    EXPECT_EQ(nlohmann::json::array({2010, 2011}), years);
    for (const auto &series : document.at("series")) {
        EXPECT_EQ(years.size(), series.at("values").size())
            << series.at("variable").get<std::string>();
    }
}

TEST(SummaryReduction, BothScenariosAreReportedSeparately) {
    const auto document = summarise_results(write_csv("scenarios", kTwoBands), SummaryFilter{});
    EXPECT_EQ(nlohmann::json::array({"Baseline", "Intervention"}), document.at("scenarios"));
    EXPECT_DOUBLE_EQ(29.0, series_for(document, "Baseline", "mean_bmi")->at("values").at(0));
    EXPECT_DOUBLE_EQ(19.0, series_for(document, "Intervention", "mean_bmi")->at("values").at(0));
}

TEST(SummaryReduction, ARowEndingInACommaIsStillARow) {
    // `std::getline(stream, field, ',')` stops at the last separator, so a line ending in a comma
    // yields one field too few — the row's field count then disagrees with the header and the
    // whole row is skipped, silently. That is a missing year in a chart, not a wrong number, which
    // is why it is worth its own test.
    const auto path = write_csv("trailing", R"(source,run,time,gender_name,index_id,count,mean_bmi
Baseline,1,2010,male,0,50,
Baseline,1,2011,male,0,50,25
)");
    const auto document = summarise_results(path, SummaryFilter{});
    EXPECT_EQ(nlohmann::json::array({2010, 2011}), document.at("years"))
        << "the row with an empty last field was dropped";
}

TEST(SummaryReduction, AYearWithNothingInItIsNullAndNotZero) {
    // A flow variable in the first year, and an age band that has emptied: neither has a value,
    // and a zero would be a claim. A client plots a gap.
    const auto path = write_csv("gaps", R"(source,run,time,gender_name,index_id,count,mean_bmi
Baseline,1,2010,male,0,0,
Baseline,1,2011,male,0,50,25
)");
    const auto document = summarise_results(path, SummaryFilter{});
    const auto *bmi = series_for(document, "Baseline", "mean_bmi");
    ASSERT_NE(nullptr, bmi);
    EXPECT_TRUE(bmi->at("values").at(0).is_null());
    EXPECT_DOUBLE_EQ(25.0, bmi->at("values").at(1).get<double>());
}

TEST(SummaryReduction, TheSexFilterSelectsRatherThanPools) {
    const auto path = write_csv("sex", R"(source,run,time,gender_name,index_id,count,mean_bmi
Baseline,1,2010,male,0,50,30
Baseline,1,2010,female,0,50,20
)");
    EXPECT_DOUBLE_EQ(25.0, series_for(summarise_results(path, SummaryFilter{}), "Baseline",
                                      "mean_bmi")
                               ->at("values")
                               .at(0));

    SummaryFilter male;
    male.sex = "male";
    EXPECT_DOUBLE_EQ(30.0,
                     series_for(summarise_results(path, male), "Baseline", "mean_bmi")
                         ->at("values")
                         .at(0));

    SummaryFilter female;
    female.sex = "female";
    EXPECT_DOUBLE_EQ(20.0,
                     series_for(summarise_results(path, female), "Baseline", "mean_bmi")
                         ->at("values")
                         .at(0));
}

TEST(SummaryReduction, TheVariableFilterNarrowsToWhatWasAsked) {
    SummaryFilter filter;
    filter.variables = {"mean_bmi"};
    const auto document = summarise_results(write_csv("narrow", kTwoBands), filter);
    EXPECT_EQ(nlohmann::json::array({"mean_bmi"}), document.at("variables"));
}

/// @brief A band's weight-category columns, which are head counts rather than means.
constexpr const char *kWeightCategories =
    R"(source,run,time,gender_name,index_id,count,normal_weight,over_weight,obese_weight,above_weight
Baseline,1,2010,male,0,10,6,3,1,4
Baseline,1,2010,male,1,90,40,30,20,50
)";

TEST(SummaryReduction, TheWeightCategoriesAreSummedBecauseTheyAreHeadCounts) {
    // The defect docs/backlog.md item 2 recorded, in the half of it a user could see: the chart's
    // level. Count-weighting these gave (10*6 + 90*40) / 100 = 36.6 for `normal_weight` where the
    // population figure is 46 — the average band's count, which is a number with no meaning. The
    // shape of the series was right, which is why it never looked wrong.
    const auto document =
        summarise_results(write_csv("weight_categories", kWeightCategories), SummaryFilter{});

    for (const auto &[variable, expected] : std::vector<std::pair<std::string, double>>{
             {"normal_weight", 46.0}, {"over_weight", 33.0}, {"obese_weight", 21.0},
             {"above_weight", 54.0}}) {
        const auto *series = series_for(document, "Baseline", variable);
        ASSERT_NE(nullptr, series) << variable;
        EXPECT_DOUBLE_EQ(expected, series->at("values").at(0).get<double>()) << variable;
    }

    // Which is the population it partitions: 46 + 33 + 21 = 100 = the head count.
    const auto *count = series_for(document, "Baseline", "count");
    ASSERT_NE(nullptr, count);
    EXPECT_DOUBLE_EQ(100.0, count->at("values").at(0).get<double>());
}

TEST(SummaryReduction, TheSummedColumnsAreTheOnesTheHarnessSums) {
    // Two reductions that disagree would be worse than one that is wrong, because a client would
    // have no way to tell which it was looking at (docs/server-api.md). The harness's list is
    // `SUMMED_VARIABLES` in tests/equivalence/run.py; this is the same list.
    for (const auto *counted : {"count", "deaths", "emigrations", "normal_weight", "over_weight",
                                "obese_weight", "above_weight"}) {
        EXPECT_TRUE(hgps::server::is_counted_column(counted)) << counted;
    }
    for (const auto *weighted : {"mean_bmi", "std_bmi", "prevalence_diabetes", "mean_yll"}) {
        EXPECT_FALSE(hgps::server::is_counted_column(weighted)) << weighted;
    }
}

TEST(SummaryReduction, TheKeyColumnsAreNotVariables) {
    // `source`, `time`, `gender_name` and `index_id` say which cell a row is, not what it
    // measured. Plotting `time` against `time` is the sort of thing a generic reducer does.
    for (const auto *key : {"source", "run", "time", "gender_name", "index_id"}) {
        EXPECT_TRUE(hgps::server::is_key_column(key)) << key;
    }
    EXPECT_FALSE(hgps::server::is_key_column("mean_bmi"));
    // `count` is both: it weights the others and is worth plotting itself.
    EXPECT_TRUE(hgps::server::is_key_column("count"));
    EXPECT_TRUE(hgps::server::is_counted_column("count"));
}

TEST(SummaryReduction, AFileThatIsNotThereIsAnErrorRatherThanAnEmptyChart) {
    EXPECT_THROW(summarise_results("no-such-file.csv", SummaryFilter{}), std::runtime_error);
    EXPECT_THROW(summarise_results(write_csv("empty", ""), SummaryFilter{}), std::runtime_error);
}
