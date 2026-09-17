# 0027 — The equivalence comparison excludes the age bands immigration cannot fill, and tests a rate where normal theory does not hold

## Status

Accepted, 2026-09-18. Refines [ADR 0006](0006-validation-strategy.md), which chose statistical
equivalence over bit-exactness but did not say what to do about a quantity the two implementations
do not both produce.

## Context

The previous run of this project ended with **54 out-of-tolerance comparisons** on the reference
example and a `--max-failures 60` budget in `scripts/check.sh`. The 54 were attributed, by
reasoning rather than by measurement, to age bands that empty: immigration into an (age, sex) band
clones somebody already in it, so an empty band cannot be refilled and the cohort falls short of
the demographic projection it is otherwise pinned to.

A budget is a poor answer. It cannot distinguish a known residual from a new one of the same size,
and a permanently non-zero failure count is a number nobody reads. This run was asked to take it
to zero, with the cause handled explicitly rather than absorbed.

Two findings came out of measuring it.

**The first is about the model.** Over three seeds of HLM_France, in the baseline scenario, 197 of
the baseline's band counts and 220 of this build's fall short of the projection — and *every single
one of them* is a band whose head count is exactly zero. No band anywhere exceeds the projection.
The affected ages are 93–100, where the projection puts between 0 and 8 people. So the bands in
which the two implementations can disagree about the cohort are exactly the bands that empty. It is
a baseline defect, recorded as B-21 in [docs/deviations.md](../deviations.md).

**The second is about the test.** Excluding those bands took the 54 failures to 7, all in one year,
all standard-deviation or tail-percentile comparisons. Their per-seed values explain them: the
series takes one single value in 18 of the baseline's 20 seeds and 16 of this build's, and jumps by
one person's worth in the rest. For such a series the sample standard deviation is not an estimate
of a spread; it is an estimate of how often the jump happens, and the 5th and 95th percentiles
*are* the jumps. **2,264 of the 7,652 series — 30% of the comparison — are that shape**, so a
normal-theory test was being applied to a third of the family.

## Decision

Two rules, both in `tests/equivalence/run.py`, both checked rather than asserted.

**1. The reduction leaves out the age bands either implementation empties**, on both sides and for
every seed. The set is derived from the runs, recorded in the reference manifest beside the stored
baseline output, and applied identically to both. A run that finds an empty band outside the
recorded set **fails**, saying which bands and telling the reader to refresh the reference — it
does not widen the exclusion by itself.

For HLM_France at 20 seeds that is 785 of 16,564 bands: 0.12% of the head count, all at ages 91 and
above. With it, the two implementations' baseline-scenario cohort totals agree **exactly**, in
every year, for both sexes, at every seed.

**2. A series whose modal value covers more than half its seeds has its standard deviation and its
two tail percentiles replaced by Fisher's exact test** on the number of seeds that left the modal
value, at the same family-wide significance the 4.5σ limit encodes (α = 0.05 over ~5,000 series, so
p < 10⁻⁵). The mean and the median are still compared as before.

**3. A variable the baseline emits and never fills is not compared at all**, and is named with the
deviation that records why. `std_income` is the only one (B-22). The exclusion applies **only while
the baseline's series is identically zero**; a baseline that starts filling the column is compared
normally, and fails if it disagrees.

`--max-failures` keeps its default of zero and `scripts/check.sh` passes nothing.

## Alternatives

- **Keep the budget.** Rejected by this run's scope, and rightly: it hid the fact that seven of the
  54 had a completely different cause from the other 47.
- **Fix the model** — give immigration a nearest-age fallback donor, which the baseline has the
  machinery for and does not use. It would meet the projected total by distorting the age
  distribution, which is a different model rather than a bug fix. It stays in
  [docs/backlog.md](../backlog.md) with the evidence attached.
- **Exclude a fixed age cutoff** instead of the empty bands. Simpler to state, but the projected
  band size is a continuum — 27 people at age 80 falling smoothly to 2 at age 100 — so any cutoff
  is arbitrary, and a conservative one throws away 8% of the cohort instead of 0.12%.
- **Exclude the affected (year, sex) cells.** Useless: almost every cell has an empty band
  somewhere in the top of its age range, so this excludes nearly everything.
- **Widen the allowance for the degenerate series** rather than changing the test. It would need a
  floor expressed in "one person's worth", which the reduction does not carry, and it would weaken
  the test for the 70% of series where normal theory does hold.
- **More seeds.** Tightens every allowance, so it makes the degenerate case worse rather than
  better. The 60-seed run confirms this: the same rules pass there too.

## Consequences

The comparison is 33,732 tests, of which 2,264 are now distribution-free, and **all of them pass**
at 20 seeds and again at 60. The worst comparison in the 20-seed run uses 87% of its allowance and
the worst in the 60-seed run 91%, so nothing is passing by a hair.

The reference file is keyed by the baseline config's hash as before, and its manifest now carries
the excluded bands and which of them the baseline itself emptied. A model change that alters which
bands empty invalidates the stored reduction and the harness says so — which is stricter than the
budget it replaces, and is what noticed that the FINCH model had changed while this run's work was
in progress.
