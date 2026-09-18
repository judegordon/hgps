# 0037 — A person's risk factors are keyed by index, not by name

## Status

Accepted, 2026-09-18.

## Context

Both of the previous run's profiles found the same thing, and the FINCH one said it louder:

| | Share of samples |
|---|---:|
| `HLM_France` | **39.5%** in `_platform_memcmp`, nearly all of it `std::map` lookups keyed by `core::Identifier` |
| `KevinHall_FINCH` | **52.3%** in string comparison and identifier handling |

The single largest named function in the FINCH profile was
`DiseaseModelBase::relative_risk_for_risk_factors` at 13% of all samples, and the 195 MiB of
resident memory has the same root: `Person::risk_factors` was a `std::map<core::Identifier, double>`,
one red-black tree node per factor per person, and every read walked it comparing strings.

`core::Identifier` compares by string deliberately. Comparing by the cached 64-bit hash is audit
finding **B-04** — the baseline's `operator==` did exactly that while its defaulted `operator<=>`
compared the string, so `a == b && a < b` could both hold — and
[ADR 0025](0025-identifier-string-equality.md) is the decision not to repeat it. So the name comparison
cannot be made cheap; what can be made cheap is not comparing names on the hot path at all.

[docs/backlog.md](../backlog.md) has carried this for two runs, with a warning attached: an
index-keyed store is exactly the kind of change that can reorder a reduction without anyone noticing.

## Decision

**A name is resolved to a dense index once, while the run is being built, and the hot paths use the
index.**

`model::FactorIndex` is an append-only table mapping `Identifier` to `std::uint32_t`.
`model::FactorValues` replaces the map on `Person`: a flat `std::vector` of (index, value) sorted by
index, with the surface of a map — `find`, `at`, `operator[]`, `contains`, `size`, iteration — so the
change was a change of representation rather than of two hundred call sites.

**Lookups are a linear scan of 32-bit integers.** Not a binary search: these vectors hold eleven
entries on `HLM_France` and fifty-five on `KevinHall_FINCH`, they are contiguous, and the comparison is
an integer, so the whole scan is one or two cache lines with no mispredicted branch worth the name. A
binary search wins at hundreds of entries, which no configuration has. And it is one allocation per
person instead of one per factor, which is where the memory goes.

**The index table is process-wide.** A `Person` is copied and cloned constantly — every immigrant is a
clone — and a back-pointer on each of a million people would cost more than the change saves. Two
simulations must not run at once in one process anyway: the worker pool and the parallel-region guard
are also process-wide, and [docs/api.md](../api.md) says so. The table is append-only, so a second run
in the same process simply interns its own names and every index that was ever handed out still means
what it meant.

**The one order-sensitive site does not iterate the person at all.**
`relative_risk_for_risk_factors` multiplies a relative risk per factor, and floating-point
multiplication is not associative, so the order is part of the result. It used to iterate
`person.risk_factors` — name order, because that was a `std::map` — and look up each factor's table.
It now iterates a **vector built once per disease model from `relative_risk_factors()`**, which is
itself a `std::map` and so is already in name order, and looks up the *person's* value by index. The
set multiplied is the same set and the order is the same order, so **the arithmetic is unchanged**.

That is the point of doing it this way round, and it is checkable rather than argued:
`docs/performance.md` records that a full `HLM_France` run before and after this change writes a
**byte-identical** result CSV, and both equivalence references were re-run.

**`FactorValues` iteration is in index order, which is not name order**, and that is the one
observable difference. Nothing else in the tree iterates a person's factors — checked, it was one
call site — so nothing else is affected. The contract is that the order is *stated and identical for
every person*, which `FactorValues.IterationOrderIsTheSameForTwoPeopleWithTheSameFactors` asserts,
because a sum over a person's factors must not depend on which person it is.

## Alternatives

- **Compare identifiers by their cached hash.** The obvious fix, and it is audit finding B-04. A
  collision makes two different factors equal, silently, in a program whose output is a number.
- **Use `unordered_map` for the per-person store.** Bucket order would reach the one product that
  iterates, which is the same defect class as B-05 (an income CDF built by iterating an
  `unordered_map`). The index table *is* an `unordered_map`, and that is safe for a different reason:
  nothing iterates it, so its bucket order cannot reach a result, and a collision is resolved by
  comparing the keys with `Identifier::operator==`, which compares the string.
- **Intern the strings and compare pointers.** Makes `operator==` cheap and leaves `operator<` — which
  is what `std::map::find` uses — still comparing strings. No help where the time is.
- **Assign indices in name order, so index order and name order coincide.** Then `FactorValues`
  iteration would be name order and nothing would need to know about this decision. It needs the full
  set of names before the first index is handed out, i.e. a sealed table — and tests construct people
  directly, with names nothing has interned. A seal that tests have to work around is a seal that gets
  bypassed. Making the one order-sensitive site iterate its own name-ordered list is smaller, and it
  puts the requirement where the requirement is.
- **Keep the map and cache a per-model index alongside it.** Two sources of truth for one person's
  factor values, and the caching would have to be invalidated by every write. The store either is
  index-keyed or is not.
- **Do nothing.** Defensible: this build is already faster than the baseline on both examples. It is
  also 40–52% of its own time in string comparison, `KevinHall_FINCH` holds 195 MiB for 6,817 people,
  and `HLM_India` needs 2.4 GiB and 42 minutes — which is what makes the India comparison expensive
  enough to be a scope question.

## Consequences

The measured effect is in [docs/performance.md](../performance.md), before and after, for both
examples, with the profiles re-taken.

`Person` gains a dependency on `model/factor_values.h`, and `FactorValues` is the only type in the tree
that presents a map's surface without being one. The iterator returns a proxy pair — a `first` that is
a reference to the interned name and a `second` that is a reference to the value — so `found->second =
x` and `const auto &[name, value] : values` both work as they did. That is a little machinery to hide a
representation, and hiding it is what kept the change from touching two hundred call sites.

`factor_index()` is a function-local static, so it is initialised on first use and never destroyed
before the last `Person`. It is also the first process-wide mutable state in the tree that is not the
worker pool, which is a real cost: it is documented here, in
[docs/api.md](../api.md) under threading, and in the header.
