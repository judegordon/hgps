# 0029 — One shape for the five age-banded interventions, and a parameter struct for the model families

## Status

Accepted, 2026-09-18. A consequence of [ADR 0019](0019-split-the-monolith-translation-units.md),
applied to the two pieces this run added.

## Context

Two places in the baseline repeat a structure rather than name it.

**The interventions.** `marketing`, `dynamic_marketing`, `fiscal`, `physical_activity` and
`food_labelling` are five classes, 1,183 lines including headers, and four of them are the same
program. Each validates that its impact bands are ordered and non-overlapping, with the same loop
written out five times and the exception type differing between copies for no stated reason
(`std::invalid_argument` in two, `std::out_of_range` in three). Each keeps an
`interventions_book_` of who it has already affected. Each applies the *difference* between bands
when a person ages out of one, with the same three-branch `if` — never exposed, same band, moved
up — written out five times. What actually differs between them is one thing: the rule for deciding
whether a person is exposed at all.

**The model constructors.** `StaticLinearModel`'s constructor takes **31 parameters**, eleven of
them defaulted `shared_ptr`s, in an order no caller can check. Its definition class takes the same
31 and forwards them. Adding a parameter means editing four signatures and one call site of 31
positional arguments; getting two of the same type the wrong way round compiles silently.
`KevinHallModel`'s takes eleven.

## Decision

**`BandedInterventionScenario`** holds the shape: the ordered-band validation, the exposure book,
`band_of(age)`, `impact_value(index)`, the active-period and risk-factor gates, and the
`clear()` that forgets the book between runs. Each of the five derives from it and implements one
virtual function, `impact_for`, which is its own exposure rule and nothing else. The five
implementations are 30 to 60 lines each and read as the policy they describe.

`simple` stays separate: it is a flat shift with no memory, and forcing it into the banded shape
would add a book it does not need.

**`StaticLinearParameters` and `KevinHallParameters`** are plain structs, passed as one
`shared_ptr<const …>`. Every field is named at the call site, documented where it is declared, and
default-initialised, so a new one does not touch a signature. The constructors validate what a
constructor should: that the per-factor vectors are all the same length, that the Cholesky factors
are square and of that size, and that the information speed is a fraction. Those checks exist
because every one of those vectors is indexed by the same position, so a length mismatch is a
silent misalignment of coefficients to factors rather than an out-of-range access.

## Alternatives

- **Port the five classes as they are.** Faithful, and five copies of a validation loop is five
  places for the sixth policy to be written slightly differently. The three-branch difference rule
  is subtle enough that having one copy of it, with one test, is worth more than the fidelity.
- **A builder for the model parameters.** More machinery than a struct with designated
  initialisers gives, for the same result.
- **Keep the positional constructors and add a static factory.** The factory has the same 31
  arguments.

## Consequences

`tests/sim/interventions_test.cpp` tests the shared shape once — bands out of order, a factor the
policy does not name, a time outside the active period, a person below the first band — and then
tests each policy's own rule. That is 32 tests where the baseline has a handful, and the property
that matters most is checked for all of them: a person who moves up a band ends on the *new*
band's effect rather than on the sum of the two.

The `impact_for` signature takes the random source whether or not a policy uses it, which is what
lets `dynamic_marketing`, `physical_activity` and `food_labelling` draw exactly once per person per
year on every path. That is not tidiness: the two scenarios of a trial run share a seed, so a draw
count that depended on which branch a person took would separate the streams and stop the
difference between the futures being attributable to the policy.
`DynamicMarketingScenario.OneDrawPerPersonPerYearWhateverHappens` checks it.
