# 0006 — Validation: port the 471 tests, then statistical equivalence

## Status

Accepted, 2026-09-17. **Ruled by the project owner**, answering the audit's first blocking question.

## Context

The audit's central finding about the earlier rewrite is that it is **unverifiable by any means
currently available** (R-02): it deleted 471 passing tests, changed integer sampling so every RNG
draw shifted — meaning its output cannot be compared to the baseline's even in principle — and
changed the config format so no shared configuration exists either. It is demonstrably
deterministic; nothing demonstrates it is correct.

The options were laid out in `docs/audit/09-ideas-and-questions.md`: bit-exact reproduction of the
baseline (strongest, but it forecloses every improvement, because it requires preserving RNG draw
order and floating-point operation order); statistical equivalence (weaker, leaves the design free);
porting the tests (component-level only, says nothing end to end).

## Decision

Ruled by the project owner:

1. **Port the baseline's 471 tests** into `tests/`, adapting to the new API but preserving each
   test's intent and expected values. Tests are written **before** the code they exercise.
2. **Statistical equivalence** against the baseline on the reference example: both implementations
   run over ≥20 seeds, and per output variable per year per scenario per sex, the mean, SD and
   5th/50th/95th percentiles are compared within tolerances documented and justified in
   `docs/equivalence.md`.
3. **Bit-exact reproduction of baseline output is not required.** Cross-platform
   bit-reproducibility is **not** a requirement.
4. Baseline RNG draw order and floating-point operation order **need not** be preserved where a
   better design exists.
5. The cheap determinism wins are kept regardless: no distribution built by iterating an unordered
   container, no entropy-seeded RNG, fixed-order reductions, sequential scenario execution.
6. Determinism *of this implementation* is a hard contract and is tested byte for byte (ADR 0008).

## Alternatives

- **Bit-exact against the baseline.** Would require reproducing `std::generate_canonical`'s draw
  consumption, the inclusive-`next_int` float-multiply scheme, the mutex-ordered reductions, and the
  unordered-map CDF walk — i.e. reproducing four of the audit's confirmed defects on purpose.
- **Tests only.** 471 component tests would not have caught the baseline's own nondeterministic row
  order, which no test in that suite exercises.
- **Equivalence only.** Would leave every component unverified and give no signal about *where* a
  divergence comes from.

## Consequences

Divergences from the baseline are expected and are not automatically bugs; each one is recorded in
`docs/deviations.md` with the baseline finding ID and the evidence. Tolerances are a judgement call
and are argued for explicitly in `docs/equivalence.md`. Because equivalence needs the baseline
binary and the upstream data, it is not part of the default CTest run.
