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

    if (requirements.demographics.region) {
        add("mean_region");
        add("std_region");
    }
    if (requirements.demographics.ethnicity) {
        add("mean_ethnicity");
        add("std_ethnicity");
    }

    // Sector is not a project requirement of its own; it is present when the static model assigns
    // it, which is exactly when the rural prevalence data exists. The mapping is the reliable
    // signal, because a factor is only in it if the config declared it.
    if (context.mapping().contains(core::Identifier{"sector"})) {
        add("mean_sector");
        add("std_sector");
    }

    if (requirements.income.enabled) {
        add("mean_income_category");
        add("std_income_category");
        add("mean_income");
        add("std_income");
    }

    if (requirements.physical_activity.enabled) {
        add("mean_physical_activity");
        add("std_physical_activity");
    }

    for (const auto &factor : context.mapping().entries()) {
        add("mean_" + factor.key().to_string());
        add("std_" + factor.key().to_string());
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
