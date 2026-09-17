#include "sha256.h"

#include <bit>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <fmt/format.h>

namespace hgps::io {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t big_endian_word(const std::byte *bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

} // namespace

Sha256Context::Sha256Context()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
             0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256Context::compress(const std::byte *block) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = big_endian_word(block + i * 4);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 =
            std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 =
            std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256Context::update(std::span<const std::byte> data) {
    if (finalised_) {
        throw std::logic_error("Sha256Context::update after finalise()");
    }

    total_bits_ += static_cast<std::uint64_t>(data.size()) * 8;

    std::size_t offset = 0;
    if (buffered_ > 0) {
        const std::size_t take = std::min(data.size(), buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, data.data(), take);
        buffered_ += take;
        offset = take;

        if (buffered_ == buffer_.size()) {
            compress(buffer_.data());
            buffered_ = 0;
        }
    }

    while (data.size() - offset >= buffer_.size()) {
        compress(data.data() + offset);
        offset += buffer_.size();
    }

    if (offset < data.size()) {
        const std::size_t rest = data.size() - offset;
        std::memcpy(buffer_.data() + buffered_, data.data() + offset, rest);
        buffered_ += rest;
    }
}

void Sha256Context::update(std::string_view data) {
    update(std::span{reinterpret_cast<const std::byte *>(data.data()), data.size()});
}

std::string Sha256Context::finalise() {
    if (finalised_) {
        throw std::logic_error("Sha256Context::finalise called twice");
    }
    finalised_ = true;

    const std::uint64_t bit_count = total_bits_;

    // Padding: 0x80, then zeros, then the length as a 64-bit big-endian value.
    std::array<std::byte, 72> tail{};
    std::size_t tail_size = 0;
    tail[tail_size++] = std::byte{0x80};
    while ((buffered_ + tail_size) % 64 != 56) {
        tail[tail_size++] = std::byte{0};
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        tail[tail_size++] = static_cast<std::byte>((bit_count >> shift) & 0xFF);
    }

    // update() would add these padding bytes to the length, so feed the blocks directly.
    std::vector<std::byte> block;
    block.reserve(buffered_ + tail_size);
    block.insert(block.end(), buffer_.begin(), buffer_.begin() + static_cast<long>(buffered_));
    block.insert(block.end(), tail.begin(), tail.begin() + static_cast<long>(tail_size));
    for (std::size_t offset = 0; offset < block.size(); offset += 64) {
        compress(block.data() + offset);
    }

    std::string result;
    result.reserve(64);
    for (const std::uint32_t word : state_) {
        result += fmt::format("{:08x}", word);
    }
    return result;
}

std::string sha256_file(const std::filesystem::path &path, std::size_t buffer_size) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error(fmt::format("Cannot open file for hashing: {}", path.string()));
    }

    Sha256Context context;
    std::vector<char> buffer(buffer_size == 0 ? 1 : buffer_size);
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = static_cast<std::size_t>(stream.gcount());
        if (read > 0) {
            context.update(
                std::span{reinterpret_cast<const std::byte *>(buffer.data()), read});
        }
    }

    if (stream.bad()) {
        throw std::runtime_error(fmt::format("Error reading file for hashing: {}", path.string()));
    }

    return context.finalise();
}

std::string sha256_string(std::string_view data) {
    Sha256Context context;
    context.update(data);
    return context.finalise();
}

} // namespace hgps::io
