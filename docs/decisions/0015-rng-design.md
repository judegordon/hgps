# 0015 — RNG: no default constructor, rejection sampling, explicit 53-bit doubles

## Status

Accepted, 2026-09-17. Partly ruled; the rest follows from ADR 0006 and ADR 0008.

## Context

The baseline's `MTRandom32` wraps `std::mt19937`. The audit found five distinct problems:

- Its **default constructor seeds from `std::random_device`** (`mtrandom.cpp:8-11`). A config with no
  `running.seed` therefore runs irreproducibly, silently, and the results file then records the seed
  as `0` — a provenance record that is actively wrong (B-06).
- `next_double()` uses **`std::generate_canonical<double, 53>`**, which consumes two 32-bit draws on
  this platform and has a history of returning exactly `1.0` on some implementations (LWG 2524,
  audit N-5). The baseline's integer sampler would return `max + 1` on an input of exactly 1.0.
- `next_int` is **documented half-open and implemented inclusive**:
  `min + (int)((max - min + 1) * next_double())` (B-07). All four call sites compensate by passing
  `size() - 1`.
- The Marsaglia polar normal draw rejects `p >= 1.0` but **not `p == 0.0`**, so `log(0)` can
  propagate `NaN` into a risk factor (B-15).
- `next_empirical_discrete` validates only that its two vectors match in size; **both empty is
  undefined behaviour** at `values.back()` (B-14).

The baseline's decision to hand-roll its distributions rather than use `std::normal_distribution` and
`std::uniform_int_distribution` is sound and is kept: the standard library versions are not specified
to produce identical sequences across implementations.

The earlier rewrite deleted the entropy-seeded constructor — the right shape of fix, turning a
runtime hazard into a compile error — but replaced the integer sampler with `% range`, trading an
out-of-range edge for modulo bias (audit §2).

## Decision

- **`rng::MtEngine`** wraps `std::mt19937`. **No default constructor**, no `seed()` setter:
  constructible only from a `std::uint32_t`. There is no path to `std::random_device` anywhere in the
  tree, and a test greps for it.
- **`rng::RandomSource`** is the only way to draw. Non-copyable, non-movable, no default
  constructor; every draw checks the parallel-region guard (ADR 0008 D3).
- **`next_double()`** builds the value explicitly from 53 random bits —
  `(((u64(a) << 32) | b) >> 11) * 0x1.0p-53` — giving `[0, 1)` by construction.
  `std::generate_canonical` is not used.
- **`next_int(count)`** returns `[0, count)` and samples by **rejection** from the engine's raw
  32-bit output, discarding the top partial bucket. No modulo bias, no out-of-range edge. The
  inclusive form is a separate, differently named function, `next_int_inclusive(lower, upper)`, so
  the contract is in the name.
- **`next_normal`** uses the polar method and rejects `p == 0.0` as well as `p >= 1.0`.
- **`next_empirical_discrete`** rejects empty input with an `InternalError`.
- **Run seeds are derived, not drawn**: `derive_run_seed(master, run_index)` is a documented
  bit-mixing function, so adding a trial run does not change the seeds of earlier runs (the baseline
  draws run seeds sequentially from a master engine, which couples them).

## Alternatives

- **Keep the baseline's draw sequence exactly**, to allow bit-exact comparison. Foreclosed by
  ADR 0006, and it would mean keeping `generate_canonical` and the inclusive `next_int`.
- **Use the standard distributions.** Not portable in their output; the baseline's own comments say
  so.
- **Modulo integer sampling**, as the earlier rewrite. Biased; rejection costs a loop that almost
  never iterates.
- **PCG or xoshiro instead of MT19937.** Better generators, and the ported tests' expected values are
  MT19937's. Not worth the coupling loss.

## Consequences

Every RNG draw in this implementation differs from the baseline's, so output can never be compared
draw for draw — which ADR 0006 already accepted, and which `docs/equivalence.md` handles by comparing
distributions. `derive_run_seed` means this implementation's run seeds also differ from the
baseline's for the same master seed; the equivalence harness compares distributions over ≥20 seeds
rather than per-seed values, so this is immaterial to it and is recorded in `docs/deviations.md`.
