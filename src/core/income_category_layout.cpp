#include "income_category_layout.h"

#include <stdexcept>
#include <utility>

#include <fmt/format.h>

namespace hgps::core {
namespace {

IncomeCategoryLayout make_layout(std::size_t count, std::vector<Income> strata,
                                 std::vector<std::string> labels) {
    return IncomeCategoryLayout{
        .count = count, .strata = std::move(strata), .labels = std::move(labels)};
}

} // namespace

IncomeCategoryLayout income_category_layout_from_config(std::string_view categories) {
    if (categories == "3") {
        return make_layout(3U, {Income::low, Income::middle, Income::high},
                           {"Low", "Middle", "High"});
    }
    if (categories == "4") {
        return make_layout(4U,
                           {Income::low, Income::lowermiddle, Income::uppermiddle, Income::high},
                           {"Low", "LowerMid", "UpperMid", "High"});
    }
    if (categories == "5") {
        return make_layout(
            5U,
            {Income::low, Income::lowermiddle, Income::middle, Income::uppermiddle, Income::high},
            {"Low", "LowerMid", "Middle", "UpperMid", "High"});
    }

    throw std::invalid_argument(
        fmt::format(R"(project_requirements.income.categories must be "3", "4", or "5". Got: "{}")",
                    categories));
}

std::size_t income_table_index(Income income, const IncomeCategoryLayout &layout) {
    for (std::size_t i = 0; i < layout.strata.size(); ++i) {
        if (layout.strata[i] == income) {
            return i;
        }
    }

    throw std::invalid_argument(
        fmt::format("Income category '{}' is not part of the configured {}-category layout",
                    income_name(income), layout.count));
}

Income income_from_equal_split_bucket(std::size_t bucket, const IncomeCategoryLayout &layout) {
    if (layout.strata.empty()) {
        throw std::invalid_argument("Income category layout is empty");
    }
    if (bucket >= layout.strata.size()) {
        return layout.strata.back();
    }
    return layout.strata[bucket];
}

double income_category_numeric(Income income, const IncomeCategoryLayout &layout) {
    // The 3- and 4-category encodings are not 1..count: the published outputs use 1, 2, 4 for
    // three categories and 1, 2, 3, 4 for four, so "high" is always 4. Kept as the baseline has
    // it, because changing it would silently change every income column in every result file.
    const std::size_t index = income_table_index(income, layout);

    if (layout.count == 3U) {
        switch (income) {
        case Income::low:
            return 1.0;
        case Income::middle:
            return 2.0;
        case Income::high:
            return 4.0;
        default:
            break;
        }
    }

    if (layout.count == 4U) {
        switch (income) {
        case Income::low:
            return 1.0;
        case Income::lowermiddle:
            return 2.0;
        case Income::uppermiddle:
            return 3.0;
        case Income::high:
            return 4.0;
        default:
            break;
        }
    }

    return static_cast<double>(index + 1U);
}

std::string_view income_name(Income income) noexcept {
    switch (income) {
    case Income::low:
        return "low";
    case Income::lowermiddle:
        return "lowermiddle";
    case Income::middle:
        return "middle";
    case Income::uppermiddle:
        return "uppermiddle";
    case Income::high:
        return "high";
    case Income::unknown:
        break;
    }
    return "unknown";
}

std::string_view income_file_name(Income income) noexcept {
    switch (income) {
    case Income::low:
        return "LowIncome";
    case Income::lowermiddle:
        return "LowerMiddleIncome";
    case Income::middle:
        return "MiddleIncome";
    case Income::uppermiddle:
        return "UpperMiddleIncome";
    case Income::high:
        return "HighIncome";
    case Income::unknown:
        break;
    }
    return "UnknownIncome";
}

} // namespace hgps::core
