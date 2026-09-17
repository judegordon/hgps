// The `HLM` static model: a fitted regression per factor per level, plus the level matrices.
#include "model_loader.h"

#include "core/string_util.h"
#include "io/json.h"

#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace hgps::config::models::detail {
namespace {

using diag::IssueCode;
using diag::IssueLocation;

/// Reads a {rows, cols, data} matrix.
std::optional<core::DoubleArray2D> read_matrix(const io::JsonCursor &cursor,
                                               const std::string &field) {
    const auto matrix = cursor.object(field);
    if (!matrix.has_value()) {
        return std::nullopt;
    }

    matrix->reject_unknown_members({"rows", "cols", "data"});

    const auto rows = matrix->integer("rows");
    const auto columns = matrix->integer("cols");
    const auto data = matrix->number_array("data");
    if (!rows || !columns || !data) {
        return std::nullopt;
    }

    if (*rows <= 0 || *columns <= 0) {
        matrix->error("rows", IssueCode::model_dimension_mismatch,
                      fmt::format("a matrix needs positive dimensions, found {}x{}", *rows,
                                  *columns));
        return std::nullopt;
    }

    const auto expected = static_cast<std::size_t>(*rows) * static_cast<std::size_t>(*columns);
    if (data->size() != expected) {
        matrix->error("data", IssueCode::model_dimension_mismatch,
                      fmt::format("{}x{} needs {} values, found {}", *rows, *columns, expected,
                                  data->size()));
        return std::nullopt;
    }

    return core::DoubleArray2D{static_cast<std::size_t>(*rows), static_cast<std::size_t>(*columns),
                               *data};
}

} // namespace

std::unique_ptr<model::RiskFactorModel> load_hlm(const nlohmann::json &document,
                                                  const std::filesystem::path &path,
                                                  const LoadContext &context,
                                                  diag::IssueReport &report) {
    const auto before = report.error_count();
    const io::JsonCursor root{document, path.string(), "", report};

    root.reject_unknown_members({"$schema", "$comment", "ModelName", "models", "levels"});

    auto models = std::make_shared<std::map<core::Identifier, model::LinearEquation>>();
    auto levels = std::make_shared<std::map<int, model::HierarchicalLevel>>();

    if (const auto equations = root.object("models")) {
        for (const auto &member : equations->node().items()) {
            const auto factor = core::Identifier{member.key()};
            const auto pointer = fmt::format("/models/{}", member.key());
            const io::JsonCursor equation{member.value(), path.string(), pointer, report};

            // The member names are the fitted-model files' own spelling, which is what the
            // upstream data uses and is therefore not ours to tidy. `residuals` and
            // `fittedValues` are per-observation diagnostics from the R fit — 40,000 numbers each
            // in the France model — that the simulation does not read; they are listed so the
            // file is accepted, not so the values are used.
            equation.reject_unknown_members({"formula", "coefficients", "residuals",
                                             "fittedValues", "residualsStandardDeviation",
                                             "rSquared"});

            model::LinearEquation result;
            result.residuals_standard_deviation =
                equation.number("residualsStandardDeviation").value_or(0.0);
            result.rsquared = equation.node().value("rSquared", 0.0);

            if (const auto coefficients = equation.object("coefficients")) {
                for (const auto &coefficient : coefficients->node().items()) {
                    const auto coefficient_pointer =
                        fmt::format("{}/coefficients/{}", pointer, coefficient.key());

                    // Every name, before anything is built (ADR 0018). "Intercept" is a metadata
                    // row and resolves as one.
                    if (!validate_predictor_name(coefficient.key(), path, coefficient_pointer,
                                                 context, report)) {
                        continue;
                    }

                    const io::JsonCursor entry{coefficient.value(), path.string(),
                                               coefficient_pointer, report};
                    entry.reject_unknown_members({"value", "stdError", "tValue", "pValue"});

                    model::Coefficient parsed;
                    parsed.value = entry.number("value").value_or(0.0);
                    parsed.std_error = entry.node().value("stdError", 0.0);
                    parsed.tvalue = entry.node().value("tValue", 0.0);
                    parsed.pvalue = entry.node().value("pValue", 0.0);

                    result.coefficients.emplace(core::Identifier{core::to_lower(coefficient.key())},
                                                parsed);
                }
            }

            models->emplace(factor, std::move(result));
        }
    }

    if (const auto level_set = root.object("levels")) {
        for (const auto &member : level_set->node().items()) {
            const auto pointer = fmt::format("/levels/{}", member.key());
            const io::JsonCursor level{member.value(), path.string(), pointer, report};

            int number = 0;
            try {
                number = std::stoi(member.key());
            } catch (const std::exception &) {
                // A boundary conversion into a located issue, as ADR 0018 permits.
                level.error("", IssueCode::model_bad_value,
                            fmt::format("'{}' is not a level number", member.key()));
                continue;
            }

            // `m`, `w` and `s` are the fitted files' names for the transition matrix, its
            // inverse and the residual-distribution matrix. They are one letter because the R
            // script that writes them calls them that; the meaning is recorded here and in
            // model::HierarchicalLevel rather than renamed in the data.
            level.reject_unknown_members(
                {"variables", "s", "w", "m", "correlation", "variances"});

            model::HierarchicalLevel result;

            const auto variables = level.string_array("variables");
            if (variables.has_value()) {
                for (std::size_t i = 0; i < variables->size(); ++i) {
                    const auto &name = (*variables)[i];
                    if (!validate_predictor_name(name, path, fmt::format("{}/variables/{}", pointer,
                                                                          i),
                                                  context, report)) {
                        continue;
                    }
                    result.variables.emplace(core::Identifier{name}, i);
                }
            }

            if (const auto matrix = read_matrix(level, "m")) {
                result.transition = *matrix;
            }
            if (const auto matrix = read_matrix(level, "w")) {
                result.inverse_transition = *matrix;
            }
            if (const auto matrix = read_matrix(level, "s")) {
                result.residual_distribution = *matrix;
            }
            if (const auto matrix = read_matrix(level, "correlation")) {
                result.correlation = *matrix;
            }

            result.variances = level.number_array("variances").value_or(std::vector<double>{});

            // The matrices are indexed by the level's own variable order, so their dimensions
            // have to match how many variables there are. The baseline checks none of this and
            // reads out of bounds if a file disagrees.
            const auto count = result.variables.size();
            if (count > 0) {
                if (result.transition.rows() != count || result.transition.columns() != count) {
                    level.error("m", IssueCode::model_dimension_mismatch,
                                fmt::format("{0} variables need a {0}x{0} transition matrix 'm', "
                                            "found {1}x{2}",
                                            count, result.transition.rows(),
                                            result.transition.columns()));
                }
                if (result.residual_distribution.columns() != count) {
                    level.error("s", IssueCode::model_dimension_mismatch,
                                fmt::format("{} variables need {} residual columns in 's', found "
                                            "{}",
                                            count, count,
                                            result.residual_distribution.columns()));
                }
            }

            levels->emplace(number, std::move(result));
        }
    }

    // Every factor at level 1 and above needs an equation, or generation would throw part-way
    // through the first person.
    for (const auto &entry : context.mapping->entries()) {
        if (entry.level() < 1) {
            continue;
        }
        if (!models->contains(entry.key())) {
            report.error(IssueCode::model_missing_key,
                         IssueLocation{.file = path.string(), .field = "/models"},
                         fmt::format("no equation for risk factor '{}', which the config declares "
                                     "at level {}",
                                     entry.name(), entry.level()));
        }
    }

    if (report.error_count() != before) {
        return nullptr;
    }

    return std::make_unique<model::StaticHierarchicalLinearModel>(std::move(models),
                                                                  std::move(levels));
}

} // namespace hgps::config::models::detail
