// New here. Baseline finding B-03: eleven <cctype> call sites pass a plain signed char, which is
// undefined behaviour for any byte >= 0x80 — reachable from a single accented character in a CSV
// header, a country name or an identifier. These wrappers are the only place that calls those
// functions, and this test exercises them over the whole byte range including the high half.
#include "core/chars.h"
#include "core/identifier.h"
#include "core/string_util.h"

#include <gtest/gtest.h>

#include <string>

TEST(TestCore_Chars, AsciiClassification) {
    using namespace hgps::core::chars;

    EXPECT_TRUE(is_digit('0'));
    EXPECT_TRUE(is_digit('9'));
    EXPECT_FALSE(is_digit('a'));

    EXPECT_TRUE(is_alpha('a'));
    EXPECT_TRUE(is_alpha('Z'));
    EXPECT_FALSE(is_alpha('_'));

    EXPECT_TRUE(is_alnum('7'));
    EXPECT_TRUE(is_alnum('q'));
    EXPECT_FALSE(is_alnum('-'));

    EXPECT_TRUE(is_space(' '));
    EXPECT_TRUE(is_space('\t'));
    EXPECT_TRUE(is_space('\n'));
    EXPECT_FALSE(is_space('x'));

    EXPECT_EQ('a', to_lower('A'));
    EXPECT_EQ('a', to_lower('a'));
    EXPECT_EQ('Z', to_upper('z'));
    EXPECT_EQ('_', to_lower('_'));
}

TEST(TestCore_Chars, HighBytesAreHandledWithoutUndefinedBehaviour) {
    using namespace hgps::core::chars;

    // Every byte, including 0x80..0xFF, which is where the baseline's version is undefined.
    // ASan+UBSan runs this too; the assertions state the contract, the sanitizer catches the UB.
    for (int byte = 0; byte < 256; ++byte) {
        const auto c = static_cast<char>(byte);
        const bool digit = is_digit(c);
        const bool alpha = is_alpha(c);

        EXPECT_EQ(digit || alpha, is_alnum(c)) << "byte " << byte;
        EXPECT_FALSE(digit && alpha) << "byte " << byte;

        // The "C" locale classifies nothing above 0x7F.
        if (byte >= 0x80) {
            EXPECT_FALSE(digit) << "byte " << byte;
            EXPECT_FALSE(alpha) << "byte " << byte;
            EXPECT_FALSE(is_space(c)) << "byte " << byte;
            EXPECT_EQ(c, to_lower(c)) << "byte " << byte;
            EXPECT_EQ(c, to_upper(c)) << "byte " << byte;
        }
    }
}

TEST(TestCore_Chars, StringHelpersSurviveNonAsciiInput) {
    using namespace hgps::core;

    // UTF-8 for "Côte d'Ivoire" — a real country name from the data store's countries.csv.
    const std::string name = "C\xC3\xB4te d'Ivoire";

    const auto lower = to_lower(name);
    EXPECT_EQ(name.size(), lower.size());
    EXPECT_EQ("c\xC3\xB4te d'ivoire", lower);

    const auto upper = to_upper(name);
    EXPECT_EQ(name.size(), upper.size());
    EXPECT_EQ("C\xC3\xB4TE D'IVOIRE", upper);

    EXPECT_EQ(name, trim("  " + name + "\t\n"));
    EXPECT_TRUE(case_insensitive::equals(name, upper));
}

TEST(TestCore_Chars, IdentifierValidationRejectsHighBytes) {
    using namespace hgps::core;
    // A high byte is not [a-z0-9_], so it must be rejected rather than read off the end of a
    // <cctype> lookup table.
    EXPECT_THROW(Identifier{"caf\xC3\xA9"}, std::invalid_argument);
}
