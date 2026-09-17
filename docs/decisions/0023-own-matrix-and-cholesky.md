# 0023 — A small dense matrix with an explicit Cholesky, instead of Eigen

## Status

Accepted, 2026-09-17. A consequence of ADR 0014, recorded separately because the numerics matter.

## Context

The baseline uses Eigen for exactly one piece of linear algebra, in the static linear model: the
risk-factor correlation matrix and the intervention policy covariance matrix are Cholesky-decomposed
(`Eigen::LLT<Eigen::MatrixXd>{correlation}.matrixL()`, `model_parser.cpp:1546` and `:1593`), and the
lower factor then multiplies a vector of standard normal draws to produce correlated residuals
(`static_linear_model.cpp:1858-1877`).

That is one decomposition and one matrix-vector product, on a matrix of about 21×21 for FINCH.

Two details are load-bearing for determinism. The **order of the random draws** filling the vector
is part of the result, and the baseline fills it via `Eigen::VectorXd::NullaryExpr`, whose evaluation
order is Eigen's business rather than the caller's. The **summation order** of the matrix-vector
product likewise decides the last bits.

## Decision

Implement `core::Matrix` — dense, row-major, `double` — in `core/matrix.*`, with:

- `cholesky_lower()`: the Cholesky–Banachiewicz algorithm, computing `L` row by row in ascending
  index order. A non-positive pivot is an `InternalError` naming the row, which is the useful
  diagnostic for a correlation matrix that is not positive definite — the baseline instead checks
  `allFinite()` after the fact.
- `multiply(std::span<const double>)`: row-major, each row summed in **ascending column order**.
- `is_finite()`, `rows()`, `columns()`, `operator()(i, j)` with bounds-checked access.

The vector of standard normal draws is filled by an explicit `for` loop in ascending index order, so
the draw order is stated in the code that owns it rather than delegated to a library's expression
evaluator.

## Alternatives

- **Keep Eigen.** A large dependency for one decomposition, and its evaluation order is not the
  caller's to state. Worth revisiting if real linear algebra is ever needed — this ADR would be
  superseded.
- **LAPACK / Accelerate.** Faster for large matrices, platform-specific, and the blocked algorithms
  do not promise a fixed summation order.
- **Store the Cholesky factor in the model files** instead of decomposing at load time. Removes the
  numerics entirely; changes the input format and moves the burden onto whoever prepares the data.

## Consequences

The decomposition is ours to test: `tests/core/matrix_test.cpp` covers a known 3×3 factorisation, a
non-positive-definite rejection, symmetry and the multiply order. Performance is irrelevant at this
size — the decomposition happens twice per run, at load time. Residual values will differ from the
baseline's in the last bits, which ADR 0006 permits and `docs/equivalence.md` accounts for.
