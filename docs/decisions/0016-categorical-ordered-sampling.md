# 0016 — `Categorical<T>`: sampling from an ordered sequence, by construction

## Status

Accepted, 2026-09-17. **Ruled by the project owner.** Idea generalised from the earlier rewrite.

## Context

The baseline assigns a person's income category by softmax over per-category linear models and then
walks a cumulative distribution:

```cpp
// static_linear_model.cpp:1978-1983
double rand = random.next_double();
double cumulative_prob = 0.0;
for (const auto &[income, probability] : probabilities) {
    cumulative_prob += probability;
    if (rand < cumulative_prob) { person.income = income; …
```

`probabilities` is a `std::unordered_map<core::Income, double>`, seeded from another unordered map,
seeded from `income_models_` — also unordered. **The order in which categories are visited decides
which one a given `rand` selects**, and unordered-container iteration order is unspecified by the
standard. Within one build the result is reproducible, which is why the audit's experiments A–F did
not catch it; on a different standard library the same seed can give the same person a different
income, and income feeds risk factors, diseases and the income-stratified outputs (B-05, N-11).

This is the only place in the baseline where the same seed can produce different *model results* on
a different platform.

The earlier rewrite fixed this one site by deriving an explicitly ordered vector of categories. The
audit's recommendation was to generalise it into a type.

## Decision

Ruled by the project owner: a `rng::Categorical<T>` type, **constructible only from an ordered
sequence**, is the only way to sample from a discrete distribution.

```cpp
template <class T> class Categorical {
  public:
    static Categorical from_weights(std::span<const T> values, std::span<const double> weights);
    static Categorical from_pairs(std::span<const std::pair<T, double>> pairs);

    // Passing an unordered container is a compile error, not a latent portability bug.
    Categorical(const std::unordered_map<T, double> &) = delete;

    const T &sample(RandomSource &random) const;   // walks the CDF in construction order
};
```

- Weights are normalised at construction; a negative weight, a non-finite weight or a zero total is
  an `InternalError`.
- The CDF is built once, in the given order, and stored. Sampling is a binary search over it, so the
  result is a pure function of the ordered input and one `next_double()`.
- The deleted `std::unordered_map` constructor exists precisely so the mistake names itself at the
  call site.
- Income categories get their order from `core::IncomeCategoryLayout`, the same ordered layout the
  output writer already uses — so the ordering becomes part of the model definition rather than an
  artefact of a container.

## Alternatives

- **Fix each site by hand**, as the earlier rewrite did. Fixes today's sites; the next one starts
  from an `unordered_map` again.
- **`std::discrete_distribution`.** Not specified to be reproducible across implementations, and it
  takes weights without the values, so nothing ties an index back to a category.
- **Forbid `unordered_map` in the codebase entirely.** Overreach: unordered maps are fine as lookup
  tables. The defect is iteration, not existence.

## Consequences

Every discrete draw carries a small construction cost and an explicit order. `Categorical` is a
value type holding its CDF, so building it per person in a hot loop would be wasteful — models
construct one per category set and reuse it, which the code does and the comments say.
