#include "zip_builder.h"

#include <array>
#include <fstream>
#include <stdexcept>

namespace hgps::test {
namespace {

std::uint32_t crc32_of(const std::string &data) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();

    std::uint32_t crc = 0xFFFFFFFFU;
    for (const char ch : data) {
        crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

void put16(std::string &out, std::uint16_t value) {
    out += static_cast<char>(value & 0xFFU);
    out += static_cast<char>((value >> 8) & 0xFFU);
}

void put32(std::string &out, std::uint32_t value) {
    out += static_cast<char>(value & 0xFFU);
    out += static_cast<char>((value >> 8) & 0xFFU);
    out += static_cast<char>((value >> 16) & 0xFFU);
    out += static_cast<char>((value >> 24) & 0xFFU);
}

} // namespace

void write_stored_zip(const std::filesystem::path &path,
                      const std::vector<std::pair<std::string, std::string>> &entries) {
    std::string body;
    std::string directory;
    std::uint32_t offset = 0;

    for (const auto &[name, content] : entries) {
        const auto crc = crc32_of(content);
        const auto size = static_cast<std::uint32_t>(content.size());

        std::string local;
        put32(local, 0x04034B50U); // local file header
        put16(local, 20);          // version needed
        put16(local, 0);           // flags
        put16(local, 0);           // stored
        put16(local, 0);           // time
        put16(local, 0x21);        // date (1980-01-01)
        put32(local, crc);
        put32(local, size);
        put32(local, size);
        put16(local, static_cast<std::uint16_t>(name.size()));
        put16(local, 0); // extra length
        local += name;
        local += content;

        put32(directory, 0x02014B50U); // central directory header
        put16(directory, 20);          // version made by
        put16(directory, 20);          // version needed
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0x21);
        put32(directory, crc);
        put32(directory, size);
        put32(directory, size);
        put16(directory, static_cast<std::uint16_t>(name.size()));
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, 0);
        put32(directory, offset);
        directory += name;

        offset += static_cast<std::uint32_t>(local.size());
        body += local;
    }

    std::string end;
    put32(end, 0x06054B50U);
    put16(end, 0);
    put16(end, 0);
    put16(end, static_cast<std::uint16_t>(entries.size()));
    put16(end, static_cast<std::uint16_t>(entries.size()));
    put32(end, static_cast<std::uint32_t>(directory.size()));
    put32(end, offset);
    put16(end, 0);

    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        throw std::runtime_error("could not write the test zip archive");
    }
    stream << body << directory << end;
}

} // namespace hgps::test
