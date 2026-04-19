#include "sha256.h"

#include <fmt/format.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace hgps {

std::string compute_sha256_for_file(const std::filesystem::path &file_path, size_t buffer_size) {
    std::ifstream ifs{file_path, std::ios::binary};
    if (!ifs) {
        throw std::runtime_error(fmt::format("Could not read file: {}", file_path.string()));
    }

    std::vector<unsigned char> buffer(buffer_size);
    SHA256Context ctx;

    do {
        ifs.read(reinterpret_cast<char *>(buffer.data()),
                 static_cast<std::streamsize>(buffer_size));
        if (!ifs) {
            buffer.resize(static_cast<size_t>(ifs.gcount()));
        }

        ctx.update(buffer);
    } while (ifs);

    return ctx.finalise();
}

SHA256Context::SHA256Context() : ctx_{EVP_MD_CTX_new()} {
    if (ctx_ == nullptr) {
        throw std::runtime_error("Failed to create SHA256 context.");
    }

    if (EVP_DigestInit_ex(ctx_, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx_);
        throw std::runtime_error("Failed to initialise SHA256 context.");
    }
}

SHA256Context::~SHA256Context() { EVP_MD_CTX_free(ctx_); }

std::string SHA256Context::finalise() {
    std::array<unsigned char, 32> hash{};
    unsigned int len = 0;
    if (EVP_DigestFinal_ex(ctx_, hash.data(), &len) != 1) {
        throw std::runtime_error("Failed to finalise SHA256 digest.");
    }

    std::stringstream ss;
    for (unsigned int i = 0; i < len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

} // namespace hgps