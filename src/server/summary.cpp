#include "summary.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include <fmt/format.h>

namespace hgps::server {
namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// @brief Splits on commas, keeping empty fields — including a trailing one.
///
/// Written out rather than done with `std::getline(stream, field, ',')`, which stops at the last
/// separator and so returns one field too few for a line ending in a comma. The consequence was
/// not a wrong value but a missing row: the field count would not match the header and the whole
/// row was skipped, silently, which is the worst shape a parsing bug can take.
std::vector<std::string> split_row(const std::string &line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto comma = line.find(',', start);
        if (comma == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    return fields;
}

/// @brief The accumulator for one (scenario, variable, year): a weighted sum and its weight, or a
///        plain total for a counted column.
struct Cell {
    double weighted{};
    double weight{};
    double total{};
    bool counted{false};
    bool seen{false};
};

} // namespace

bool is_key_column(const std::string &name) noexcept {
    const auto key = lower(name);
    return key == "source" || key == "run" || key == "time" || key == "gender_name" ||
           key == "index_id" || key == "count";
}

/// @brief Is this column a head count, to be summed over the bands rather than averaged over them?
///
/// The four weight categories are head counts and were averaged here until this run: the analysis
/// module increments one of them per person per band and neither implementation divides them by
/// anything. The chart's level was therefore the average band's count — 15.3 for `normal_weight` on
/// `HLM_France` at (baseline, 2030, male), where the population figure is about 1,550 — while its
/// shape still followed the underlying quantity, which is why it did not look wrong.
///
/// This list and the equivalence harness's `SUMMED_VARIABLES` are the same rule, and they have to
/// stay the same rule: docs/server-api.md says the server reduces the way the harness does so that
/// a client and the comparison cannot disagree about what a series means.
bool is_counted_column(const std::string &name) noexcept {
    const auto key = lower(name);
    return key == "deaths" || key == "emigrations" || key == "count" || key == "normal_weight" ||
           key == "over_weight" || key == "obese_weight" || key == "above_weight";
}

nlohmann::json summarise_results(const std::filesystem::path &csv, const SummaryFilter &filter) {
    std::ifstream stream{csv};
    if (!stream) {
        throw std::runtime_error(fmt::format("could not read the result file {}", csv.string()));
    }

    std::string line;
    if (!std::getline(stream, line)) {
        throw std::runtime_error(fmt::format("the result file {} has no header", csv.string()));
    }
    const auto header = split_row(line);
    if (header.empty()) {
        throw std::runtime_error(
            fmt::format("the result file {} has an empty header", csv.string()));
    }

    const std::set<std::string> wanted{filter.variables.begin(), filter.variables.end()};
    const auto sex_filter = lower(filter.sex);

    std::map<std::size_t, std::string> columns; // index -> variable name
    std::size_t source_at = header.size();
    std::size_t time_at = header.size();
    std::size_t sex_at = header.size();
    std::size_t count_at = header.size();

    for (std::size_t i = 0; i < header.size(); ++i) {
        const auto key = lower(header[i]);
        if (key == "source") {
            source_at = i;
        } else if (key == "time") {
            time_at = i;
        } else if (key == "gender_name") {
            sex_at = i;
        } else if (key == "count") {
            count_at = i;
        }
        if (is_key_column(header[i]) && key != "count") {
            continue;
        }
        if (!wanted.empty() && !wanted.contains(header[i])) {
            continue;
        }
        columns.emplace(i, header[i]);
    }

    if (source_at == header.size() || time_at == header.size() || count_at == header.size()) {
        throw std::runtime_error(
            fmt::format("the result file {} is missing source, time or count", csv.string()));
    }

    std::map<std::tuple<std::string, std::string, int>, Cell> cells;
    std::set<std::string> scenarios;
    std::set<int> years;
    std::set<std::string> variables;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split_row(line);
        if (fields.size() != header.size()) {
            continue;
        }
        if (sex_filter != "all" && sex_at < fields.size() && lower(fields[sex_at]) != sex_filter) {
            continue;
        }

        const auto &scenario = fields[source_at];
        const int year = std::stoi(fields[time_at]);
        const double head_count = std::stod(fields[count_at]);

        scenarios.insert(scenario);
        years.insert(year);

        for (const auto &[index, name] : columns) {
            const auto &text = fields[index];
            if (text.empty()) {
                continue;
            }
            double value = 0.0;
            try {
                value = std::stod(text);
            } catch (const std::exception &) {
                continue;
            }

            auto &cell = cells[{scenario, name, year}];
            cell.counted = is_counted_column(name);
            cell.seen = true;
            cell.total += value;
            cell.weighted += value * head_count;
            cell.weight += head_count;
            variables.insert(name);
        }
    }

    auto series = nlohmann::json::array();
    for (const auto &scenario : scenarios) {
        for (const auto &variable : variables) {
            auto values = nlohmann::json::array();
            bool any = false;
            for (const int year : years) {
                const auto found = cells.find({scenario, variable, year});
                if (found == cells.end() || !found->second.seen) {
                    values.push_back(nullptr);
                    continue;
                }
                const auto &cell = found->second;
                if (cell.counted) {
                    values.push_back(cell.total);
                    any = true;
                } else if (cell.weight > 0.0) {
                    values.push_back(cell.weighted / cell.weight);
                    any = true;
                } else {
                    // Every band empty: there is no mean, and a zero would be a claim.
                    values.push_back(nullptr);
                }
            }
            if (!any) {
                continue;
            }
            series.push_back({{"scenario", scenario},
                              {"variable", variable},
                              {"sex", filter.sex},
                              {"values", values}});
        }
    }

    return {
        {"reduction",
         "count-weighted mean over age bands; head counts — count, deaths, emigrations and the "
         "four weight categories — summed, the same rule docs/equivalence-method.md reduces by"},
        {"sex", filter.sex},
        {"scenarios", std::vector<std::string>{scenarios.begin(), scenarios.end()}},
        {"years", std::vector<int>{years.begin(), years.end()}},
        {"variables", std::vector<std::string>{variables.begin(), variables.end()}},
        {"series", series},
    };
}

} // namespace hgps::server
