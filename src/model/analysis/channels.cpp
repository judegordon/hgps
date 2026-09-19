// The output channel list: the columns of the result file, in order.
#include "analysis_module.h"

#include "core/string_util.h"
#include "model/runtime_context.h"

#include <algorithm>
#include <set>

namespace hgps::model {
namespace {

} // namespace

void AnalysisModule::initialise_output_channels(RuntimeContext &context) {
    if (!channels_.empty()) {
        return;
    }

    std::set<std::string> seen;
    const auto add = [this, &seen](const std::string &name) {
        if (seen.insert(core::to_lower(name)).second) {
            channels_.push_back(name);
        }
    };

    add("count");
    add("deaths");
    add("emigrations");

    add("mean_age");
    add("std_age");
    add("mean_age2");
    add("std_age2");
    add("mean_age3");
    add("std_age3");
    add("mean_gender");
    add("std_gender");

    // Which demographic dimensions to report comes from project_requirements, not from sampling
    // the population.
    //
    // The baseline inspects the first 1,000 people and adds a channel if any of them has a
    // region, an ethnicity, a sector, an income or a physical activity value. That makes the
    // column set of the output file depend on the contents of a sample of the cohort, so two runs
    // of the same config can produce files with different columns — and a small cohort can lose a
    // column entirely. The configuration already says which dimensions the project uses.
    const auto &requirements = context.inputs().project_requirements();

    // Sector is not a project requirement of its own; it is present when a model assigns it,
    // which is exactly when the rural prevalence data exists.
    if (assigned_.sector || context.mapping().contains(core::Identifier{"sector"})) {
        add("mean_sector");
        add("std_sector");
    }

    // A requirement being enabled is not enough: the defaults switch income and physical
    // activity on for every config, including the HLM ones, whose models assign neither. The
    // channel exists when the project asks for the dimension *and* a loaded model gives it to
    // people — otherwise the file gains columns of zeros, which is how the reference example's
    // output grew six of them.
    if (requirements.income.enabled && assigned_.income_category) {
        add("mean_income_category");
        add("std_income_category");
    }

    if (requirements.income.enabled && assigned_.income) {
        add("mean_income");
        add("std_income");
    }

    if (requirements.physical_activity.enabled && assigned_.physical_activity) {
        add("mean_physical_activity");
        add("std_physical_activity");
    }

    for (const auto &factor : context.mapping().entries()) {
        add("mean_" + factor.key().to_string());
        add("std_" + factor.key().to_string());
    }

    // Region and ethnicity, **after** the mapping and not before it, and the position is
    // load-bearing rather than tidy.
    //
    // Every configuration that has them — `KevinHall_FINCH` is the only one of the six — also
    // declares `Region` and `Ethnicity` as level-0 risk factors, so the loop above has already
    // added both columns at their mapping position and `add` deduplicates. Putting this block
    // before the loop, where the other demographic dimensions are, would move those two columns
    // and change every result file that has them.
    //
    // A configuration that assigns a region without naming it in its mapping gets the columns
    // here instead, which is the case these two lines exist for: until this run
    // `AssignedAttributes::region` and `::ethnicity` were never set by anything, so this condition
    // was dead and such a configuration silently had no region column at all. The second fixture
    // pack is now exactly that configuration (docs/SUMMARY.md).
    if (requirements.demographics.region && assigned_.region) {
        add("mean_region");
        add("std_region");
    }
    if (requirements.demographics.ethnicity && assigned_.ethnicity) {
        add("mean_ethnicity");
        add("std_ethnicity");
    }

    for (const auto &disease : context.diseases()) {
        add("prevalence_" + disease.code.to_string());
        add("incidence_" + disease.code.to_string());
    }

    add("normal_weight");
    add("over_weight");
    add("obese_weight");
    add("above_weight");
    add("mean_yll");
    add("std_yll");
    add("mean_yld");
    add("std_yld");
    add("mean_daly");
    add("std_daly");
}

} // namespace hgps::model
