// Ported from the baseline's src/HealthGPS.Tests/Sha256.Test.cpp, keeping its expected digests,
// and extended with the FIPS 180-4 vectors. The implementation is ours rather than OpenSSL's
// (docs/decisions/0014-minimal-dependency-set.md), so the vectors do real work here.
#include "io/sha256.h"

#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

using hgps::io::Sha256Context;

TEST(SHA256, SHA256Context) {
    {
        Sha256Context ctx;
        EXPECT_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                  ctx.finalise());
    }
    {
        Sha256Context ctx;
        ctx.update(std::string_view{"hello"});
        EXPECT_EQ("2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824",
                  ctx.finalise());
    }
    {
        // The same string in two chunks: the buffering path.
        Sha256Context ctx;
        ctx.update(std::string_view{"hel"});
        ctx.update(std::string_view{"lo"});
        EXPECT_EQ("2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824",
                  ctx.finalise());
    }
}

TEST(SHA256, FipsVectors) {
    using hgps::io::sha256_string;

    EXPECT_EQ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              sha256_string("abc"));
    EXPECT_EQ("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              sha256_string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));

    // 1,000,000 'a' characters: exercises the multi-block path and the 64-bit length field.
    EXPECT_EQ("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              sha256_string(std::string(1'000'000, 'a')));
}

TEST(SHA256, BoundaryLengthsAroundTheBlockSize) {
    using hgps::io::sha256_string;

    // 55, 56 and 64 bytes are where the padding logic changes: 56 forces a second block.
    EXPECT_EQ(sha256_string(std::string(55, 'x')),
              [] {
                  Sha256Context ctx;
                  ctx.update(std::string_view{std::string(55, 'x')});
                  return ctx.finalise();
              }());

    const auto fifty_six = sha256_string(std::string(56, 'x'));
    const auto sixty_four = sha256_string(std::string(64, 'x'));
    EXPECT_NE(fifty_six, sixty_four);
    EXPECT_EQ(64U, fifty_six.size());
    EXPECT_EQ(64U, sixty_four.size());

    Sha256Context chunked;
    for (int i = 0; i < 64; ++i) {
        chunked.update(std::string_view{"x"});
    }
    EXPECT_EQ(sixty_four, chunked.finalise());
}

TEST(SHA256, FinaliseIsSingleUse) {
    Sha256Context ctx;
    ctx.update(std::string_view{"data"});
    ctx.finalise();
    EXPECT_THROW(ctx.finalise(), std::logic_error);
    EXPECT_THROW(ctx.update(std::string_view{"more"}), std::logic_error);
}

TEST(SHA256, ComputeForFile) {
    const auto dir = hgps::test::scratch_dir("sha256_file");
    const auto path = dir / "example_file.txt";
    {
        std::ofstream stream{path, std::ios::binary};
        stream << "test";
    }

    // The baseline's expected digest for its own example_file.txt, whose contents are "test".
    EXPECT_EQ("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08",
              hgps::io::sha256_file(path));
}

TEST(SHA256, ComputeForFileInSmallChunks) {
    const auto dir = hgps::test::scratch_dir("sha256_file_chunked");
    const auto path = dir / "example_file.txt";
    {
        std::ofstream stream{path, std::ios::binary};
        stream << "test";
    }

    EXPECT_EQ("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08",
              hgps::io::sha256_file(path, 2));
    EXPECT_EQ("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08",
              hgps::io::sha256_file(path, 1));
}

TEST(SHA256, MissingFileThrows) {
    EXPECT_THROW(hgps::io::sha256_file("/definitely/not/here.bin"), std::runtime_error);
}
