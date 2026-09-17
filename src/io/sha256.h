#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace hgps::io {

/// @brief SHA-256, FIPS 180-4.
///
/// Implemented here rather than linked from OpenSSL: openssl was one of the two dependencies that
/// stopped the baseline building on macOS at all (audit section 4.2), and this is the only thing
/// the program used it for. The standard test vectors are in tests/io/sha256_test.cpp.
class Sha256Context {
  public:
    Sha256Context();

    void update(std::span<const std::byte> data);
    void update(std::string_view data);

    /// @brief The hash as 64 lower-case hexadecimal characters. The context is single-use: calling
    ///        this twice on the same object is a programmer error and throws.
    std::string finalise();

  private:
    std::array<std::uint32_t, 8> state_{};
    std::array<std::byte, 64> buffer_{};
    std::size_t buffered_{0};
    std::uint64_t total_bits_{0};
    bool finalised_{false};

    void compress(const std::byte *block);
};

/// @brief The SHA-256 of a file's contents, read in chunks.
/// @throws std::runtime_error if the file cannot be opened or read.
std::string sha256_file(const std::filesystem::path &path,
                        std::size_t buffer_size = std::size_t{1} << 20);

/// @brief The SHA-256 of a string, for tests and small inputs.
std::string sha256_string(std::string_view data);

} // namespace hgps::io
