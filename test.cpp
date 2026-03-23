#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct Location {
    std::string file;
    std::string function;
    int line{0};
};

struct Log {
    std::string level;
    int code;
    Location location;
    std::string message;
};

std::string trim_edges(const std::string& text) {
    const std::string whitespace = " \t\n\r";

    const std::size_t start = text.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }

    const std::size_t end = text.find_last_not_of(whitespace);
    return text.substr(start, end - start + 1);
}

std::string format_log(const Log& log) {
    std::ostringstream out;

    out << "[" << log.level << "]";
    out << "[" << log.code << "]";

    bool has_location = !log.location.file.empty()
                     || !log.location.function.empty()
                     || log.location.line != 0;

    if (has_location) {
        out << " ";

        bool printed_something = false;

        if (!log.location.file.empty()) {
            out << log.location.file;
            printed_something = true;
        }

        if (!log.location.function.empty()) {
            if (printed_something) {
                out << ":";
            }
            out << log.location.function;
            printed_something = true;
        }

        if (log.location.line != 0) {
            if (printed_something) {
                out << ":";
            }
            out << log.location.line;
        }
    }

    if (!log.message.empty()) {
        out << " - " << log.message;
    }

    return out.str();
}

bool parse_log_line(const std::string& line, Log& log) {
    std::istringstream stream(line);
    std::vector<std::string> fields;
    std::string field;

    while (std::getline(stream, field, ',')) {
        fields.push_back(trim_edges(field));
    }

    if (fields.size() != 6) {
        return false;
    }

    log.level = fields[0];
    log.code = std::stoi(fields[1]);
    log.location.file = fields[2];
    log.location.function = fields[3];
    log.location.line = fields[4].empty() ? 0 : std::stoi(fields[4]);
    log.message = fields[5];

    return true;
}

int main() {
    std::ifstream file("logs.txt");

    if (!file) {
        std::cerr << "Could not open logs.txt\n";
        return 1;
    }

    std::string line;

    while (std::getline(file, line)) {
        Log log;

        if (parse_log_line(line, log)) {
            std::cout << format_log(log) << '\n';
        } else {
            std::cout << "Bad line: " << line << '\n';
        }
    }

    return 0;
}