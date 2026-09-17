# 0026 — Sequential by default; parallel only for RNG-free work, with fixed-order reductions

## Status

Accepted, 2026-09-17. Follows the determinism ruling (ADR 0008).

## Context

The baseline parallelises about twenty population sweeps with oneTBB. The dominant pattern is
*accumulate into a shared table under one `std::mutex`*, e.g.
`default_disease_model.cpp:68-70`. This is free of data races and **not** order-stable: floating-point
addition is not associative, so the accumulated sums can differ in their last bits between runs
(audit N-7). In `default_disease_model.cpp` those sums become `average_relative_risk`, which divides
into a probability that is compared against a random hazard — so a last-bit difference can in
principle flip a person's disease outcome.

The audit measured that this does not happen at realistic scale: experiments B, C and F found
bit-identical output at 125,000 people over 25 years and across a 10× change in thread count,
because a last-bit perturbation flips the comparison with probability around 10⁻¹⁶. It also measured
what the parallelism buys: **37 s at one thread versus 39 s at ten**, because the global mutex
serialises each loop body.

So the baseline's threading costs a determinism hazard and buys nothing measurable.

## Decision

- **Sequential by default.** One thread unless `--threads` says otherwise, and the default path is
  the tested path.
- **Parallelism is permitted only for work that is a pure function of person state** — no RNG draw
  (enforced as D3), no mutation of shared state, no dependence on iteration order.
- **Every reduction goes through `core::parallel::reduce_ordered`**, which decomposes `[0, n)` into
  blocks whose count and boundaries are a function of `n` and a fixed block size — **not** of the
  thread count — accumulates each block in ascending index order, and combines the per-block
  partials in ascending block order. The result is bit-identical for any thread count, which the
  reproducibility test checks at 1 and N threads.
- **No mutex-guarded accumulation exists in the tree.** A `+=` into shared state under a lock is the
  pattern this ADR exists to forbid.
- `core/parallel.h` is the only code that creates a thread.

## Alternatives

- **Keep oneTBB and its mutex pattern.** Inherits N-7 and depends on a large library for a
  measured 5% slowdown.
- **Fully sequential, no parallel helpers at all.** Tempting given the measurements. Rejected
  because the analysis module's per-person statistics are genuinely independent, and a deterministic
  parallel reduction is about forty lines — worth having once, correctly, rather than retrofitting
  under pressure later.
- **Atomic accumulation.** `fetch_add` on a `double` has the same order problem and is slower.

## Consequences

`reduce_ordered` allocates one partial per block, and its block size is a tuning parameter recorded
in the header with the reason for its value. Speed-up is capped by the block structure rather than
by the hardware, which is the intended trade. `docs/performance.md` reports whether the parallel
sites earn their place; if they do not, removing them is a one-line change per site.
