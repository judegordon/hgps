// New here. The baseline delegates this to Eigen; docs/decisions/0023-own-matrix-and-cholesky.md
// explains why it is ours, and this is the test that earns that decision.
#include "core/matrix.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace {

hgps::core::Matrix make(std::size_t n, const std::vector<double> &values) {
    hgps::core::Matrix m{n, n};
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            m(i, j) = values[i * n + j];
        }
    }
    return m;
}

} // namespace

TEST(TestCore_Matrix, ShapeAndAccess) {
    using namespace hgps::core;

    Matrix m{2, 3};
    EXPECT_EQ(2U, m.rows());
    EXPECT_EQ(3U, m.columns());
    EXPECT_FALSE(m.empty());
    EXPECT_DOUBLE_EQ(0.0, m(1, 2));

    m(1, 2) = 4.5;
    EXPECT_DOUBLE_EQ(4.5, m(1, 2));

    EXPECT_THROW(m(2, 0), std::out_of_range);
    EXPECT_THROW(m(0, 3), std::out_of_range);
    EXPECT_THROW(Matrix(0, 3), std::invalid_argument);
}

TEST(TestCore_Matrix, CholeskyOfAKnownMatrix) {
    using namespace hgps::core;

    // A = [[4,12,-16],[12,37,-43],[-16,-43,98]], the textbook example whose factor is
    // L = [[2,0,0],[6,1,0],[-8,5,3]].
    const auto a = make(3, {4, 12, -16, 12, 37, -43, -16, -43, 98});
    const auto l = a.cholesky_lower();

    EXPECT_DOUBLE_EQ(2.0, l(0, 0));
    EXPECT_DOUBLE_EQ(0.0, l(0, 1));
    EXPECT_DOUBLE_EQ(0.0, l(0, 2));
    EXPECT_DOUBLE_EQ(6.0, l(1, 0));
    EXPECT_DOUBLE_EQ(1.0, l(1, 1));
    EXPECT_DOUBLE_EQ(0.0, l(1, 2));
    EXPECT_DOUBLE_EQ(-8.0, l(2, 0));
    EXPECT_DOUBLE_EQ(5.0, l(2, 1));
    EXPECT_DOUBLE_EQ(3.0, l(2, 2));
}

TEST(TestCore_Matrix, CholeskyFactorReproducesTheInput) {
    using namespace hgps::core;

    // A correlation matrix of the shape the risk-factor models actually decompose.
    const auto a = make(3, {1.0, 0.4, 0.2, 0.4, 1.0, -0.3, 0.2, -0.3, 1.0});
    const auto l = a.cholesky_lower();

    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                sum += l(i, k) * l(j, k);
            }
            EXPECT_NEAR(a(i, j), sum, 1e-12) << "at (" << i << ", " << j << ")";
        }
    }
}

TEST(TestCore_Matrix, CholeskyRejectsNonPositiveDefiniteAndNonSquare) {
    using namespace hgps::core;

    // Not positive definite: a correlation of 1.5 is not a correlation.
    const auto bad = make(2, {1.0, 1.5, 1.5, 1.0});
    EXPECT_THROW(bad.cholesky_lower(), std::invalid_argument);

    const Matrix oblong{2, 3};
    EXPECT_THROW(oblong.cholesky_lower(), std::invalid_argument);

    auto nan_matrix = make(2, {1.0, 0.0, 0.0, 1.0});
    nan_matrix(0, 1) = std::nan("");
    EXPECT_FALSE(nan_matrix.is_finite());
    EXPECT_THROW(nan_matrix.cholesky_lower(), std::invalid_argument);
}

TEST(TestCore_Matrix, MultiplyIsRowMajorAndChecksSize) {
    using namespace hgps::core;

    Matrix m{2, 3};
    m(0, 0) = 1.0;
    m(0, 1) = 2.0;
    m(0, 2) = 3.0;
    m(1, 0) = 4.0;
    m(1, 1) = 5.0;
    m(1, 2) = 6.0;

    const std::vector<double> v{1.0, 0.5, -1.0};
    const auto result = m.multiply(v);

    ASSERT_EQ(2U, result.size());
    EXPECT_DOUBLE_EQ(1.0 + 1.0 - 3.0, result[0]);
    EXPECT_DOUBLE_EQ(4.0 + 2.5 - 6.0, result[1]);

    const std::vector<double> wrong{1.0, 2.0};
    EXPECT_THROW(m.multiply(wrong), std::invalid_argument);
}

TEST(TestCore_Matrix, SymmetryCheck) {
    using namespace hgps::core;

    EXPECT_TRUE(make(2, {1.0, 0.5, 0.5, 1.0}).is_symmetric(1e-12));
    EXPECT_FALSE(make(2, {1.0, 0.5, 0.4, 1.0}).is_symmetric(1e-12));
    EXPECT_TRUE(make(2, {1.0, 0.5, 0.5 + 1e-15, 1.0}).is_symmetric(1e-12));
}
