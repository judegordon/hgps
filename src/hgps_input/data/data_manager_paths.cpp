#include "data_manager.h"

#include <cstdlib>
#include <regex>
#include "io/json_parser.h"

namespace hgps::input {

std::string DataManager::replace_string_tokens(const std::string &source,
                                               const std::vector<std::string> &tokens) {
    std::string output = source;
    std::size_t tk_end = 0;
    for (const auto &tk : tokens) {
        auto tk_start = output.find_first_of('{', tk_end);
        if (tk_start != std::string::npos) {
            tk_end = output.find_first_of('}', tk_start + 1);
            if (tk_end != std::string::npos) {
                output = output.replace(tk_start, tk_end - tk_start + 1, tk);
                tk_end = tk_start + tk.length();
            }
        }
    }

    return output;
}

std::filesystem::path DataManager::construct_pif_path(const std::string &disease_code,
                                                      const nlohmann::json &pif_config) const {
    auto data_root_path = pif_config["data_root_path"].get<std::string>();
    auto risk_factor = pif_config["risk_factor"].get<std::string>();
    auto scenario = pif_config["scenario"].get<std::string>();

    data_root_path = expand_environment_variables(data_root_path);

    return std::filesystem::path(data_root_path) / "diseases" / disease_code / "PIF" / risk_factor /
           scenario;
}

std::string DataManager::expand_environment_variables(const std::string &path) const {
    std::string result = path;

    std::regex env_var_regex(R"(\$\{([^}]+)\})");
    std::smatch match;

    while (std::regex_search(result, match, env_var_regex)) {
        std::string var_name = match[1].str();
        const char *env_value = std::getenv(var_name.c_str());

        if (env_value) {
            result.replace(match.position(), match.length(), env_value);
        } else {
            notify_warning(
                fmt::format("Environment variable {} not found, using placeholder", var_name));
        }
    }

    return result;
}

} // namespace hgps::input