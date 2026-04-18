#pragma once

#include <openssl/evp.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace hgps {

std::string compute_sha256_for_file(const std::filesystem::path &file_path,
                                    size_t buffer_size = static_cast<size_t>(1024 * 1024));

class SHA256Context {
  public:
    SHA256Context();
    ~SHA256Context();

    SHA256Context(const SHA256Context &) = delete;
    SHA256Context &operator=(const SHA256Context &) = delete;
    SHA256Context(SHA256Context &&) = delete;
    SHA256Context &operator=(SHA256Context &&) = delete;

    template <class R>
    void update(const R &range) {
        if (const auto size = std::distance(std::begin(range), std::end(range))) {
            if (EVP_DigestUpdate(ctx_, &*std::begin(range), static_cast<size_t>(size)) != 1) {
                throw std::runtime_error("Failed to update SHA256 digest.");
            }
        }
    }

    std::string finalise();

  private:
    EVP_MD_CTX *ctx_;
};

} // namespace hgps