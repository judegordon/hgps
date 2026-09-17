# 0017 — Monotonic lifetime-unique person IDs, initial cohort `1..N` by index, O(1) free slots

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

## Context

The baseline's `Population` is a `std::vector<Person>` with slot recycling: dead and emigrated slots
are reused by newborns and immigrants, but **person IDs are not reused** — `allocate_next_person_id`
hands out a monotonically increasing value (`population.h:114`).

The earlier rewrite changed IDs to be slot-based and therefore **recycled**, while also shipping
per-person longitudinal tracking output. The audit rates this high severity (R-03): a recycled ID
silently conflates a dead person with the newborn that took their slot, in the one output where
identity is the whole point.

Separately, the baseline's `Population::add` calls `find_index_of_recyclables`, which **rescans from
index 0 on every call** — once per migrant per age per gender per year, i.e. quadratic in population
size (B-13). And `add` is marked `noexcept` while calling `emplace_back` and `at()`, so an
allocation failure becomes `std::terminate` (B-12).

## Decision

Ruled by the project owner:

- **Person IDs come from a monotonic lifetime-unique counter.** An ID is never reused within a run,
  even after the person's storage slot is.
- **The initial cohort takes IDs `1..N` by index**, so person *k* of the baseline scenario is person
  *k* of the intervention scenario. This is what makes per-person comparison between the two futures
  meaningful, and it is why IDs are assigned by index rather than in draw order.
- **Free slots are tracked in an explicit free-slot list**, giving O(1) allocation instead of a
  rescan.
- `Population::add` is **not** `noexcept`. It may allocate; if allocation fails the exception
  propagates.

## Alternatives

- **Slot-based IDs**, as the earlier rewrite. Rejected by ruling; the tracking output makes it
  actively wrong.
- **Keep the rescan.** Simple, and quadratic. The free-slot list is a `std::vector<std::size_t>` used
  as a stack — a dozen lines.
- **A monotonic ID per scenario, unrelated between scenarios.** Loses baseline/intervention
  alignment, which is the reason the initial cohort is numbered by index.

## Consequences

The free-slot list is state that must stay consistent with the per-person `is_alive` / `has_emigrated`
flags; `Population` owns both and the invariant is asserted in debug builds and covered by tests
ported from `Population.Test.cpp`. IDs grow without bound over a long run, which is what
`std::size_t` is for.
