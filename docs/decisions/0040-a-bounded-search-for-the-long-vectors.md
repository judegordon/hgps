# 0040 — A bounded binary search for the long vectors, and a scan for the short ones

## Status

Accepted, 2026-09-18. Amends [ADR 0037](0037-index-keyed-risk-factor-store.md), which chose the
linear scan this replaces, and **corrects two numbers it stated**.

## Context

[ADR 0037](0037-index-keyed-risk-factor-store.md) replaced a `std::map<Identifier, double>` per person
with a flat vector sorted by index, and chose a **linear scan** to search it, with the reason written
down: "eleven entries on `HLM_France` and fifty-five on `KevinHall_FINCH`, they are contiguous, and
the comparison is a 32-bit integer — so the whole scan is one or two cache lines… A binary search
wins at hundreds of entries, which no configuration has."

The profile taken after that change then put `FactorValues::position_of` at **18.0% of all samples**
on `KevinHall_FINCH` — the single largest item, ahead of everything the model computes
([docs/performance.md](../performance.md)) — and [docs/backlog.md](../backlog.md) has carried it as
the top performance item since.

## What the measurement said, and what it contradicted

The obvious change was a **direct probe**: `entries_` is sorted by index with distinct indices, so
entry *k* has an index of at least *k*, and if a person's indices are dense from zero then
`entries_[index].index == index` is the answer in one load and one compare. That argument was
written, implemented, and was about to be committed with the sentence "every configuration measured
interns its factor names before any person is built and gives every person the same set, so the set
is 0..n−1 and the probe hits every time."

**It was not measured, and it is false.** Instrumenting `position_of` for one whole run of each
example:

| | Largest vector a person holds | Probe hit rate |
|---|---:|---:|
| `HLM_France` | **6** | 22.1% |
| `KevinHall_FINCH` | **121** | **0.0%** |

Two things follow, and both matter more than the change itself.

1. **ADR 0037's "eleven" and "fifty-five" are wrong.** They were the count of factors a config
   *declares*. What a person actually carries is 6 on France and 121 on FINCH — a fifth of the
   declared number on one and more than double it on the other. The store is keyed by a process-wide
   index table that interns every name any part of the run uses, and a person holds a sparse subset
   of it. Nothing downstream was wrong, but the number the design was argued from was.
2. **The probe is worthless here.** It hits 0.0% of 122 million lookups on the example that needed
   the change. Had it shipped on the strength of its argument, it would have made `HLM_France` about
   5% slower and been credited with `KevinHall_FINCH`'s improvement, which came from somewhere else
   entirely.

So this ADR records a rejected design that looked right, because the reason it was rejected is the
only reason the accepted one is trustworthy.

## Decision

**Scan a short vector; binary-search a long one, over a bounded prefix.**

```
if (count <= 16) linear scan            // HLM_France, always: its longest vector is 6
else             binary search [0, min(index+1, count))
```

The bound is the same ordering fact the probe rested on, used for the thing it is actually good for:
a position *p* holding `index` needs *p* ≤ `index`, so everything above `index` cannot hold it and
the search need not look there.

**16 is a round number chosen between the two measured sizes, not a swept crossover.** France's
longest vector is 6 and FINCH's is 121 — a factor of twenty apart — so any threshold between about 8
and 60 gives both examples the same behaviour, and sweeping for a precise crossover would be
measuring the machine rather than the code. What the threshold guarantees is the part worth
guaranteeing: **France's lookup is character-for-character the scan it was**, so that example cannot
regress.

**The acceptance test is byte-for-byte, not statistical.** `position_of` decides *where* a value is
and never what it is, so no path through it can change a number — but that is an argument, and the
thing that catches a mistake in an argument is a diff. Both examples were run before and after, same
seed, one thread, and every result file's SHA-256 is unchanged: 4 CSVs on `HLM_France`, 5 on
`KevinHall_FINCH`. A twenty-seed statistical comparison would call a last-bit difference agreement.

## Alternatives

- **Keep the linear scan.** Right for France and wrong for FINCH, where it is 18% of the profile in a
  function that decides nothing, and where the vector is 121 entries rather than the 55 the previous
  decision assumed.
- **The direct probe, with no threshold.** Rejected on its own measurement, above: 0.0% hit rate on
  FINCH. It is the design this ADR exists to record the rejection of.
- **Binary search always, with no threshold.** Six unpredictable branches against a six-element scan
  on `HLM_France` — and France is the shape `HLM_India` has, which is the example where a constant
  factor is paid 1.24 million times a year.
- **A per-person dense array indexed by factor index, plus a present-index list** — what
  [docs/backlog.md](../backlog.md) proposed. The measurement is what rules it out: a FINCH person
  holds 121 values out of a process-wide table that is larger again, so the dense array is mostly
  holes, and it would be allocated per person on an example with 1.24 million of them.
- **Sort the vector by something other than index, or seal the index table so the sets are dense.**
  [ADR 0037](0037-index-keyed-risk-factor-store.md) rejected sealing — tests construct people with
  names nothing has interned — and the sort order is what iteration order is defined by.

## Consequences

`KevinHall_FINCH` is **1.44× faster** on CPU time; `HLM_France` runs the same code as before and is
unchanged within measurement error. Both figures, and the conditions they were taken under, are in
[docs/performance.md](../performance.md).

The correctness of the bound depends on `entries_` staying **sorted by index with distinct indices**,
which is the invariant `operator[]` maintains with `lower_bound` and which
`FactorValues.IterationOrderIsTheSameForTwoPeopleWithTheSameFactors` pins from the other side.
`FactorValues.FindsAnEntryWhoseIndexIsNotItsPosition` pins the branch that the examples now exercise
constantly and that the rejected design assumed away: a person holding a sparse set, where the entry
for an index is never at that index.

The threshold is one number in one place with the two measurements beside it. If a third example
turns up whose people carry between 8 and 60 factors, it is worth re-measuring rather than assuming
this still divides them correctly.
