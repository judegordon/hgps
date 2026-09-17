// Ported from the baseline's src/HealthGPS.Tests/Core.Array2DTest.cpp.
#include "core/array2d.h"

#include <gtest/gtest.h>

#include <vector>

TEST(TestCore_Array2D, CreateEmptyStorage) {
    using namespace hgps::core;
    const auto d3x2 = DoubleArray2D(3, 2);

    ASSERT_EQ(3U, d3x2.rows());
    ASSERT_EQ(2U, d3x2.columns());
    ASSERT_EQ(6U, d3x2.size());
}

TEST(TestCore_Array2D, CreateEmptyWithDefaultValue) {
    using namespace hgps::core;
    const std::size_t rows = 3;
    const std::size_t cols = 4;
    const auto data = 5.0;

    const auto d2x3v5 = DoubleArray2D(rows, cols, data);

    ASSERT_EQ(rows, d2x3v5.rows());
    ASSERT_EQ(cols, d2x3v5.columns());
    ASSERT_EQ(rows * cols, d2x3v5.size());

    for (std::size_t i = 0; i < rows; i++) {
        for (std::size_t j = 0; j < cols; j++) {
            ASSERT_EQ(data, d2x3v5(i, j));
        }
    }
}

TEST(TestCore_Array2D, CreateFullFromVector) {
    using namespace hgps::core;
    const std::vector<int> n = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    const auto i3x4v = IntegerArray2D(3, 4, n);

    ASSERT_EQ(3U, i3x4v.rows());
    ASSERT_EQ(4U, i3x4v.columns());
    ASSERT_EQ(12U, i3x4v.size());
}

TEST(TestCore_Array2D, CreateWithZeroSizeThrows) {
    using namespace hgps::core;
    ASSERT_THROW(IntegerArray2D(0, 5), std::invalid_argument);
    ASSERT_THROW(IntegerArray2D(5, 0), std::invalid_argument);
}

TEST(TestCore_Array2D, CreateWithSizeMismatchThrows) {
    using namespace hgps::core;
    const std::vector<int> n = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    ASSERT_THROW(IntegerArray2D(3, 3, n), std::invalid_argument);
}

TEST(TestCore_Array2D, AccessViaColumnIndex) {
    using namespace hgps::core;
    const std::vector<int> data = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    const std::vector<int> row0 = {2, 1, 5, 7};
    const std::vector<int> row1 = {8, 9, 7, 3};
    const std::vector<int> row2 = {5, 4, 2, 9};
    const auto i3x4v = IntegerArray2D(3, 4, data);

    for (std::size_t i = 0; i < 4; i++) {
        ASSERT_EQ(row0[i], i3x4v(0, i));
        ASSERT_EQ(row1[i], i3x4v(1, i));
        ASSERT_EQ(row2[i], i3x4v(2, i));
    }
}

TEST(TestCore_Array2D, AccessViaConstColumnIndex) {
    using namespace hgps::core;
    const std::vector<int> data = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    const std::vector<int> row1 = {8, 9, 7, 3};
    const auto i3x4v = IntegerArray2D(3, 4, data);

    for (std::size_t i = 0; i < 4; i++) {
        ASSERT_EQ(row1[i], i3x4v(1, i));
    }
}

TEST(TestCore_Array2D, AccessViaRowIndex) {
    using namespace hgps::core;
    const std::vector<int> data = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    const std::vector<int> col0 = {2, 8, 5};
    const std::vector<int> col1 = {1, 9, 4};
    const std::vector<int> col2 = {5, 7, 2};
    const std::vector<int> col3 = {7, 3, 9};
    const auto i3x4v = IntegerArray2D(3, 4, data);

    for (std::size_t i = 0; i < 3; i++) {
        ASSERT_EQ(col0[i], i3x4v(i, 0));
        ASSERT_EQ(col1[i], i3x4v(i, 1));
        ASSERT_EQ(col2[i], i3x4v(i, 2));
        ASSERT_EQ(col3[i], i3x4v(i, 3));
    }
}

TEST(TestCore_Array2D, AccessViaConstRowIndex) {
    using namespace hgps::core;
    const std::vector<int> data = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    const std::vector<int> col3 = {7, 3, 9};
    const auto i3x4v = IntegerArray2D(3, 4, data);

    for (std::size_t i = 0; i < 3; i++) {
        ASSERT_EQ(col3[i], i3x4v(i, 3));
    }
}

TEST(TestCore_Array2D, AccessViaRowColumnIndex) {
    using namespace hgps::core;
    const std::vector<int> data = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    const auto i3x4v = IntegerArray2D(3, 4, data);

    for (std::size_t i = 0; i < 3; i++) {
        for (std::size_t j = 0; j < 4; j++) {
            ASSERT_EQ(data[i * 4 + j], i3x4v(i, j));
        }
    }
}

TEST(TestCore_Array2D, AccessOutOfRangeThrows) {
    using namespace hgps::core;
    const std::vector<int> data = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    auto i3x4v = IntegerArray2D(3, 4, data);

    ASSERT_THROW(i3x4v(1, 5), std::out_of_range);
    ASSERT_THROW(i3x4v(1, 10), std::out_of_range);
    ASSERT_THROW(i3x4v(5, 2), std::out_of_range);
    ASSERT_THROW(i3x4v(10, 2), std::out_of_range);
}

TEST(TestCore_Array2D, ExportStorageToVector) {
    using namespace hgps::core;
    const std::vector<int> data = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    const auto i3x4v = IntegerArray2D(3, 4, data);

    ASSERT_EQ(data, i3x4v.to_vector());
}

TEST(TestCore_Array2D, PrintDataToString) {
    using namespace hgps::core;
    const std::vector<int> data = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    const auto i3x4v = IntegerArray2D(3, 4, data);
    const auto i3str = i3x4v.to_string();

    ASSERT_TRUE(i3str.size() > 10);
    EXPECT_EQ(0U, i3str.find_first_of("Array"));
    EXPECT_TRUE(i3str.find_first_of(':') > 0);
    EXPECT_EQ('\n', i3str.back());
}

TEST(TestCore_Array2D, UpdateStorageValue) {
    using namespace hgps::core;
    const std::vector<int> data = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    auto i3x4v = IntegerArray2D(3, 4, data);

    for (std::size_t i = 0; i < 3; i++) {
        for (std::size_t j = 0; j < 4; j++) {
            ASSERT_EQ(data[i * 4 + j], i3x4v(i, j));
            i3x4v(i, j) += 5;
            ASSERT_EQ(data[i * 4 + j] + 5, i3x4v(i, j));
        }
    }
}

TEST(TestCore_Array2D, ClearStorageValue) {
    using namespace hgps::core;
    const std::vector<int> data = {2, 1, 5, 7, 8, 9, 7, 3, 5, 4, 2, 9};
    auto i3x4v = IntegerArray2D(3, 4, data);

    i3x4v.clear();
    for (std::size_t i = 0; i < 3; i++) {
        for (std::size_t j = 0; j < 4; j++) {
            ASSERT_EQ(0, i3x4v(i, j));
        }
    }
}
