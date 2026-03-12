#pragma once

#include <openssl/evp.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace hgps {

std::string compute_sha256_for_file(const std::filesystem::path &file_path,
                                    size_t buffer_size = static_cast<size_t>(1024 * 1024));

class SHA256Context {
  public:
    SHA256Context();
    ~SHA256Context();

    template <class R> void update(const R &range) {
        if (const auto size = std::distance(std::begin(range), std::end(range))) {
            EVP_DigestUpdate(ctx_, &*std::begin(range), size);
        }
    }

    std::string finalise();

  private:
    EVP_MD_CTX *ctx_;
};
} // namespace hgps
