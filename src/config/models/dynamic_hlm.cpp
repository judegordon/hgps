// The `EBHLM` dynamic model: per-age-band, per-sex equations on last year's values.
#include "model_loader.h"

#include "core/interval.h"
#include "core/string_util.h"
#include "io/json.h"

#include <map>
#include <string>

#include <fmt/format.h>

namespace hgps::config::models::detail {
namespace {

using diag::IssueCode;
using diag::IssueLocation;

} // namespace

std::unique_ptr<model::RiskFactorModel> load_ebhlm(const nlohmann::json &document,
                                                    const std::filesystem::path &path,
                                                    const LoadContext &context,
                                                    diag::IssueReport &report) {
    const auto before = report.error_count();
    const io::JsonCursor root{document, path.string(), "", report};

    root.reject_unknown_members({"$schema", "$comment", "ModelName", "Country",
                                 "BoundaryPercentage", "Variables", "Equations"});

    const auto boundary = root.number("BoundaryPercentage");
    if (!boundary.has_value()) {
        return nullptr;
    }
    if (!(*boundary > 0.0 && *boundary < 1.0)) {
        root.error("BoundaryPercentage", IssueCode::model_bad_value,
                   fmt::format("must be in (0, 1), found {}", *boundary));
    }

    // The delta variables: "dBMI" names the change in "bmi". Both sides are validated, because a
    // typo in either produces an equation that refers to nothing.
    auto variables = std::make_shared<std::map<core::Identifier, core::Identifier>>();
    for (const auto &entry : root.array("Variables")) {
        entry.reject_unknown_members({"Name", "Factor"});

        const auto name = entry.string("Name");
        const auto factor = entry.string("Factor");
        if (!name || !factor) {
            continue;
        }

        if (!context.mapping->contains(core::Identifier{*factor})) {
            entry.error("Factor", IssueCode::model_unknown_predictor,
                        fmt::format("'{}' is not a declared risk factor", *factor));
            continue;
        }

        variables->emplace(core::Identifier{core::to_lower(*name)},
                           core::Identifier{core::to_lower(*factor)});
    }

    auto equations =
        std::make_shared<std::map<core::IntegerInterval, model::AgeGroupGenderEquation>>();

    if (const auto by_age = root.object("Equations")) {
        for (const auto &age_member : by_age->node().items()) {
            const auto age_pointer = fmt::format("/Equations/{}", age_member.key());

            core::IntegerInterval band{};
            try {
                band = core::parse_integer_interval(age_member.key());
            } catch (const std::exception &) {
                report.error(IssueCode::model_bad_value,
                             IssueLocation{.file = path.string(), .field = age_pointer},
                             fmt::format("'{}' is not an age band of the form lower-upper",
                                         age_member.key()));
                continue;
            }

            model::AgeGroupGenderEquation group;
            group.age_group = band;

            const io::JsonCursor by_gender{age_member.value(), path.string(), age_pointer, report};
            by_gender.reject_unknown_members({"Male", "Female", "male", "female"});

            for (const auto &gender_member : by_gender.node().items()) {
                const auto gender_pointer =
                    fmt::format("{}/{}", age_pointer, gender_member.key());

                auto *target = core::case_insensitive::equals("male", gender_member.key())
                                   ? &group.male
                                   : core::case_insensitive::equals("female", gender_member.key())
                                         ? &group.female
                                         : nullptr;
                if (target == nullptr) {
                    report.error(IssueCode::model_bad_value,
                                 IssueLocation{.file = path.string(), .field = gender_pointer},
                                 fmt::format("unknown sex '{}'; expected Male or Female",
                                             gender_member.key()));
                    continue;
                }

                const io::JsonCursor list{gender_member.value(), path.string(), gender_pointer,
                                          report};
                if (!list.is_array()) {
                    report.error(IssueCode::config_wrong_type,
                                 IssueLocation{.file = path.string(), .field = gender_pointer},
                                 "expected an array of factor equations");
                    continue;
                }

                for (std::size_t i = 0; i < gender_member.value().size(); ++i) {
                    const auto equation_pointer = fmt::format("{}/{}", gender_pointer, i);
                    const io::JsonCursor equation{gender_member.value()[i], path.string(),
                                                  equation_pointer, report};

                    equation.reject_unknown_members(
                        {"Name", "Coefficients", "ResidualsStandardDeviation"});

                    const auto name = equation.string("Name");
                    if (!name.has_value()) {
                        continue;
                    }

                    if (!context.mapping->contains(core::Identifier{*name})) {
                        equation.error("Name", IssueCode::model_unknown_predictor,
                                       fmt::format("'{}' is not a declared risk factor", *name));
                        continue;
                    }

                    model::FactorDynamicEquation parsed;
                    parsed.name = *name;
                    parsed.residuals_standard_deviation =
                        equation.number("ResidualsStandardDeviation").value_or(0.0);

                    if (const auto coefficients = equation.object("Coefficients")) {
                        for (const auto &coefficient : coefficients->node().items()) {
                            const auto coefficient_pointer = fmt::format(
                                "{}/Coefficients/{}", equation_pointer, coefficient.key());

                            const auto key = core::Identifier{core::to_lower(coefficient.key())};

                            // A coefficient names either a predictor or a declared delta
                            // variable; anything else is a typo that would otherwise produce
                            // plausible numbers.
                            if (!variables->contains(key) &&
                                !validate_predictor_name(coefficient.key(), path,
                                                          coefficient_pointer, context, report)) {
                                continue;
                            }

                            if (!coefficient.value().is_number()) {
                                report.error(IssueCode::config_wrong_type,
                                             IssueLocation{.file = path.string(),
                                                           .field = coefficient_pointer},
                                             "a coefficient must be a number");
                                continue;
                            }

                            parsed.coefficients.emplace(key, coefficient.value().get<double>());
                        }
                    }

                    target->emplace(core::Identifier{core::to_lower(*name)}, std::move(parsed));
                }
            }

            // Both sexes need an equation for every factor the model moves, or a run would throw
            // part-way through the first year.
            for (const auto &entry : context.mapping->entries()) {
                if (entry.level() < 1) {
                    continue;
                }
                for (const auto &[sex, table] :
                     {std::pair{"Male", &group.male}, std::pair{"Female", &group.female}}) {
                    if (!table->contains(entry.key())) {
                        report.error(IssueCode::model_missing_key,
                                     IssueLocation{.file = path.string(), .field = age_pointer},
                                     fmt::format("no {} equation for risk factor '{}' in age band "
                                                 "{}, which the config declares at level {}",
                                                 sex, entry.name(), age_member.key(),
                                                 entry.level()));
                    }
                }
            }

            equations->emplace(band, std::move(group));
        }
    }

    if (variables->empty()) {
        root.error("Variables", IssueCode::model_missing_key,
                   "the dynamic model needs at least one variable");
    }
    if (equations->empty()) {
        root.error("Equations", IssueCode::model_missing_key,
                   "the dynamic model needs at least one age band of equations");
    }

    if (report.error_count() != before) {
        return nullptr;
    }

    // This model does not trend its expected values, but the adjustable base needs the tables to
    // exist; the baseline fills them with 1.0 and 0 for the same reason.
    auto trend = std::make_shared<std::map<core::Identifier, double>>();
    auto trend_steps = std::make_shared<std::map<core::Identifier, int>>();
    for (const auto &[variable, factor] : *variables) {
        trend->emplace(factor, 1.0);
        trend_steps->emplace(factor, 0);
    }

    return std::make_unique<model::DynamicHierarchicalLinearModel>(
        context.expected, std::move(trend), std::move(trend_steps), std::move(equations),
        std::move(variables), *boundary);
}

} // namespace hgps::config::models::detail
