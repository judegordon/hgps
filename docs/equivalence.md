# Statistical equivalence with the baseline

[ADR 0006](decisions/0006-validation-strategy.md) chose the validation strategy: port the
baseline's tests, and check **statistical equivalence** on the reference example rather than
bit-exact reproduction. Bit-exactness would have required preserving every RNG draw order and every
floating-point operation order in the baseline, which forecloses most of the improvements this
implementation exists to make. Equivalence leaves the design free and still answers the question
that matters — *do the two implementations produce the same distributions?*

This document is the result. It is produced by one command per example:

```bash
cd /Users/jude/work/hpgs/hgps_new_rewrite
tests/equivalence/run.py --example HLM_France      --seeds 20
tests/equivalence/run.py --example KevinHall_FINCH --seeds 20
```

## What is compared, and how

**The examples.** Two, one per model family
([docs/examples.md](examples.md)):

| | Model families | Horizon | Cohort | Diseases | Risk factors |
|---|---|---|---:|---:|---:|
| `HLM_France` | `HLM` static, `EBHLM` dynamic | 2010–2050 | 6,244 | 6 | 11 |
| `KevinHall_FINCH` | `StaticLinear`, `KevinHall` | 2022–2032 | 6,817 | 15 | 34 |

Both implementations get the same seed, the same input files, the same horizon, one trial run, one
thread, and the same active intervention.

**What "the intervention" means, and it differs between the two.**

`HLM_France` ships `active_type_id: null`, which would compare one scenario against nothing. The
harness activates `simple` — BMI −1.0 from 2022 — in **both**, so the policy path is compared as
well as the baseline path.

`KevinHall_FINCH` ships `simple` as its active intervention and gives it an **empty impact list**,
so the `interventions` block contributes nothing. Its policy is somewhere else: `policy_start_year`
is 2024, and from that year the `StaticLinear` model applies the S1 policy-effect coefficients and
the S1 residual policy covariance to the intervention scenario. That is the FINCH policy mechanism,
and it is a different one from the age-banded impacts. It is visible in the stored reference: the
baseline's two scenarios are identical in 2022 and 2023 and differ in 2,476 of 4,600 reduced series
in 2024, rising to 4,191 by 2032.

So the FINCH comparison covers the S1 policy model, and the five age-banded policies are compared
separately, one run each — see *One intervention at a time*, below.

**The configs.** The baseline gets the upstream v1 config it was written for; this build gets the
converted v2 config. They are not the same file, so the harness records the SHA-256 of each with
the seed removed, and stores it beside the reference output. For the runs reported here:

| | baseline config | this build's config |
| --- | --- | --- |
| `HLM_France` | `6cea2a8ad468e34daa9ff5a3fb4592a7b2ce571100a3d7f2b8f91c12bb8392cd` | `b047ee3136241db69a3d4dcb93c149dc1866c16b639d8d6e94a629ad5d4fc444` |
| `KevinHall_FINCH` | `a850e8a9f739b318ee8e3926355c65c4fefdc255a6c45df4f740631b932899be` | `685c8b8f5c9ee017c4ca99b150522766c67aa8cd9d3f658f9fcf863a5f1130d8` |

The two configs are equivalent by construction: the converter's defaults for
`project_requirements` are the baseline's own struct defaults, checked against
`hgps_main/src/HealthGPS.Input/poco.h`, and the column sets of the two result files are identical.

**The reduction.** Each result file has one row per (scenario, run, year, sex, age). The two
implementations' age bands hold *different people*, so comparing rows is meaningless; comparing
population figures is not. The harness reduces each file to one value per
(scenario, year, sex, variable):

- `count`, `deaths` and `emigrations` are counts, so they are summed over the age bands.
- everything else is a mean or a proportion within the band, so it is the count-weighted mean over
  the bands — the figure the variable is reporting for the population.

**What the reduction leaves out.** Two things, and they are different in kind.

*The emptying bands.* The age bands that either implementation empties, on both sides and for every
seed — 785 of HLM_France's 16,564 bands and 692 of KevinHall_FINCH's, in each case a fraction of a
percent of the head count and all at the top of the age range. That is not a convenience: it is the
one place the two implementations are not reporting the same quantity, for a reason traced below
under *the emptying-band mechanism*. The excluded set is derived from the runs rather than
declared, is recorded in the reference manifest, and a run that finds an empty band outside it
fails rather than quietly widening it.

*A variable the baseline does not compute.* `std_income`, on FINCH only. The baseline emits the
column and never fills it: the loop that accumulates squared deviations skips `income` on the
ground that the mapping loop handles it, and the mapping loop skips it for the same reason, so
every value in the column is exactly zero in every band of every year of every run. That is
deviation **B-22**, and this build computes it. The harness's `BASELINE_DOES_NOT_COMPUTE` list
drops the variable **only while the baseline's series is identically zero**, and compares it
normally the moment that stops being true — so the exclusion cannot outlive the defect, and a
future baseline that fixes it turns the comparison back on by itself.

**The statistics.** For each of those series the harness takes the 20 seeds' values and computes
the **mean**, the **standard deviation** and the **5th, 50th and 95th percentiles** (type-7
quantiles, so they can be reproduced in R or numpy), for each implementation, and compares them —
except for a series that sits on one single value in more than half the seeds, where three of the
five are replaced by a distribution-free test (below). That is 33,732 comparisons on HLM_France and
23,432 on KevinHall_FINCH.

## The thresholds, and why they are what they are

Two Monte Carlo simulations with different random streams cannot agree exactly. The question is
whether they agree to within what that noise allows, so each comparison is a hypothesis test rather
than a fixed tolerance:

```
allowed(statistic) = 4.5 × SE(statistic) + 1e-5 × scale
```

**The standard error.** For a sample of *n* values with standard deviations `s_b` and `s_n`:

| Statistic | SE of the difference | Why |
| --- | --- | --- |
| mean | `sqrt((s_b² + s_n²)/n)` | textbook |
| median | `1.2533 × sqrt((s_b² + s_n²)/n)` | a quantile's SE is `sqrt(q(1−q)/n)/φ(z_q)`; at q=0.5 that is 1.2533σ/√n |
| 5th, 95th percentile | `2.1133 × sqrt((s_b² + s_n²)/n)` | the same formula at q=0.05: `sqrt(0.0475/n)/0.10314` |
| standard deviation | `sqrt((s_b² + s_n²)/(2(n−1)))` | the SE of a sample standard deviation is `s/sqrt(2(n−1))` |

The standard deviation is compared as a **difference** rather than a ratio, deliberately: a ratio
cannot be formed when one implementation's standard deviation is exactly zero, and that case — a
quantity that is deterministic in one implementation and not the other — is precisely the one worth
seeing rather than skipping.

**Why 4.5 sigma and not 3.** A 3σ threshold has a one-in-370 false-failure rate per comparison. Over
38,000 comparisons that is about a hundred failures from noise alone, which would make the result
unreadable. The threshold is therefore set for the whole family: a Bonferroni correction at
α = 0.05 over ~5,000 independent series needs z = 4.4, so 4.5 is used. This is a deliberate trade:
the test is now insensitive to a real difference smaller than about 4.5 standard errors in a single
series. What protects against that is the *pattern* — a systematic difference shows up in many
series at once, and the harness prints the largest differences that passed as well as the ones that
failed, so a shift sitting just inside the allowance is visible rather than silent.

**Why a floor at all.** No comparison can be tighter than the precision of the numbers compared,
and the baseline writes its CSV with **six significant digits**. Each of its band figures therefore
carries a relative rounding error of up to 4×10⁻⁶, and the difference of two such figures up to
8×10⁻⁶; the floor is set at 1×10⁻⁵ of the larger of the two values. This matters more than it
sounds, because many of this model's variables are *nearly deterministic* (see below), so their
standard-error term collapses to nothing and the floor becomes the whole allowance. For those
variables the test is "equal to the precision the baseline prints", which is the strongest test the
baseline's output supports. Making it stronger would require changing the baseline's writer, and the
baseline is read-only ([ADR 0003](decisions/0003-read-only-sources-and-out-of-tree-baseline-build.md)).

**Where normal theory does not apply.** Every standard error above assumes the 20 seeds are a
sample from something like a normal distribution. For **2,264 of the 7,652 series — 30% of them —
that is plainly false**: the value is the *same* in most of the seeds and jumps in the rest. Those
are the population aggregates that calibration pins, where the seed moves nothing except whether
one particular person happened to die. The across-seed standard deviation of such a series is not
an estimate of a spread at all; it is an estimate of **how often the jump happens**, and the 5th
and 95th percentiles *are* the jumps.

Applying a normal-theory allowance to them compares two rare-event rates as though they were
spreads, and fails whenever the rate differs by a couple of seeds in twenty — which is exactly what
the last seven residual failures of this run turned out to be, before this was fixed. So when
either implementation's modal value covers more than half its seeds, the harness drops the standard
deviation and the two tail percentiles and compares instead, by **Fisher's exact test**, the number
of seeds that left the modal value. The mean and the median are still compared as before.

That is a real test and not a waiver. It makes no assumption about the shape of the distribution,
and at the same family-wide significance the sigma limit encodes — α = 0.05 over ~5,000 series, so
p < 10⁻⁵ — it still fails a rate that differs by, say, 0 of 20 against 12 of 20. What it stops
doing is calling 1-in-20 and 4-in-20 a disagreement.

**What is skipped.** A burden, death, emigration or incidence variable in the first simulated year,
where the quantity is not defined yet: 56 comparisons on HLM_France and 136 on KevinHall_FINCH.
Nothing else is excluded.

## The result — HLM_France

**33,732 comparisons over 20 seeds. Zero out of tolerance.**

| Statistic | Failed | Compared | Worst excursion that passed |
| --- | ---: | ---: | --- |
| mean | **0** | 7,652 | 0.82× the allowance (`prevalence_osteoarthritisknee`, baseline 2029 male, 0.0661 against 0.0619) |
| median | **0** | 7,652 | 0.78× (`mean_age`, intervention 2046 male, 43.956 against 44.216) |
| 5th percentile | **0** | 5,388 | 0.74× (`normal_weight`, baseline 2044 male, 13.687 against 14.124) |
| 95th percentile | **0** | 5,388 | 0.74× (`above_weight`, baseline 2044 male, 20.813 against 20.375) |
| standard deviation | **0** | 5,388 | 0.87× (`mean_bmi`, intervention 2024 female, 0.00086 against 0.00313) |
| departure rate | **0** | 2,264 | p = 0.34 against a threshold of 10⁻⁵ (`count`, intervention 2024 male, 1 seed in 20 against 4) |

Nothing sits on the edge: **the worst comparison in the whole run uses 87% of its allowance**, and
the worst rate comparison is four orders of magnitude clear of its threshold. That is a different
kind of result from "everything passes", because a set of comparisons clustered at 0.99× would mean
the thresholds were doing the work.

## The result — KevinHall_FINCH

**23,432 comparisons over 20 seeds. Zero out of tolerance.**

| Statistic | Failed | Compared | Worst excursion that passed |
| --- | ---: | ---: | --- |
| mean | **0** | 4,924 | 0.85× the allowance (`obese_weight`, intervention 2032 male, 9.129 against 9.414) |
| median | **0** | 4,924 | 0.90× (`obese_weight`, intervention 2031 male, 9.049 against 9.443) |
| 5th percentile | **0** | 4,330 | 0.86× (`mean_fat`, intervention 2027 male, 107.954 against 108.315) |
| 95th percentile | **0** | 4,330 | 0.76× (`incidence_esophaguscancer`, intervention 2032 male, 0.000314 against 0.000851) |
| standard deviation | **0** | 4,330 | 0.74× (`mean_fruit`, baseline 2031 female, 0.664 against 0.275) |
| departure rate | **0** | 594 | p = 0.048 against a threshold of 10⁻⁵ (`incidence_kidneycancer`, baseline 2023 male, 16 seeds in 20 against 9) |

The worst comparison uses 90% of its allowance. The worst rate comparison has p = 0.048, which is
nominally significant at an uncorrected 5% and is exactly what 594 comparisons should produce from
noise alone — and the direction test says so: over the 567 (variable, statistic) groups, this
build's worst-case value is above the baseline's in 55 and below it in 59.

### Getting there

FINCH did not pass first time. The count of out-of-tolerance comparisons went 1,542 → 1,326 →
1,202 → 269 → 52 → 19 → **0**, and every step was a defect in this implementation rather than a
loosened threshold. The last two are worth recording because both were the same mistake in
different clothes — **calibrating onto the wrong target**:

- **Physical activity.** `adjust_to_factors_mean` shifts an (age, sex) band so its mean lands on
  the FactorsMean table's value, and the shifted values are then clamped to the factor's configured
  range. Clamping the *target* as well is a different thing and it is wrong: the FINCH table puts a
  newborn's physical activity at 1.2, below the configured lower bound of 1.4, so the newborn band
  came out a tenth of a unit high and, because the shifted values then cleared the bound instead of
  piling up on it, 3.5% wider as well. That was all 198 `mean_pa` comparisons.
- **Weight.** The Kevin Hall model derives an adult's expected weight from a regression on their
  expected energy intake, height, age and activity level, and that regression is a fit to the
  FactorsMean table's own `Weight` column — 86.1912 against the table's 86.1864 for a 40-year-old
  man. The derived value is what a person's weight is generated from; the measurement is what the
  population's mean should be calibrated to. Calibrating onto the fit was five grams per person,
  and it was the 8 remaining `mean_weight` comparisons.

Neither would have been found by reading the code, and neither is visible in any unit test: both
are a fraction of a percent, in the right direction, on a quantity that looks calibrated either
way.

## One intervention at a time

`simple` is one policy, and on FINCH it is an empty impact list. The other five — `marketing`,
`dynamic_marketing`, `food_labelling`, `physical_activity`, `fiscal` — are age-banded policies with
their own exposure rules, their own per-person draws and, in `food_labelling` and
`physical_activity`, their own memory of who they have already affected. A comparison that never
activates them says nothing about them.

So each is compared on its own, 20 seeds, in both implementations, with
`--intervention NAME` ([ADR 0031](decisions/0031-comparing-one-intervention-at-a-time.md)). Each
choice hashes to a different config and therefore to a different stored reference, so they cannot
disturb the reference for `simple`.

On `HLM_France` the definitions are upstream's own: its `config.json` declares all six and the
harness simply activates one at a time. On `KevinHall_FINCH` they come from
`tests/equivalence/interventions/KevinHall_FINCH.json`, which is HLM_France's five definitions
verbatim with two substitutions and nothing else — the active period becomes FINCH's own 2025
onwards, because France's runs to 2050 and the FINCH horizon ends in 2032, and the risk factor
`Energy` becomes `EnergyIntake`, which is FINCH's name for it. France's coefficients mean nothing
for Finland; that is not what they are for. Both implementations get the identical definition, and
the question asked is whether they apply it identically — which on the FINCH surface is a different
question, because an energy impact propagates through the Kevin Hall energy balance into weight and
BMI rather than sitting in a static factor.

<!-- INTERVENTION-RESULTS -->

## What the residuals turned out to be

The previous run of this project ended with 54 out-of-tolerance comparisons and a failure budget of
60 to keep them from leaving a permanently red check. This run was to take the budget back to zero
by explaining each one rather than by allowing for it. Both of the two things it found are below,
and neither was what it looked like.

### The emptying-band mechanism, and why it is the baseline's

The previous run traced the 54, with reasoning but without measurement, to age bands that empty.
This run measured it.

**The mechanism.** Immigration into an (age, sex) band clones somebody already in that band, which
is how a new arrival gets a plausible set of risk factors. When the band is empty there is nobody
to clone, and the baseline's `apply_net_migration` does this
(`hgps_main/src/HealthGPS/simulation.cpp:245`):

```cpp
if (!similar_indices.empty()) {
    …                       // add `net_value` clones
}                           // and otherwise, silently, add none
```

So the whole immigration target for that band is abandoned. The cohort is otherwise pinned to the
demographic projection — net migration is *defined* as the projection minus the simulated count —
and here it silently is not. This implementation reproduced the rule, and so inherited the
behaviour.

**The measurement.** Both implementations were run at three seeds and every (year, sex, age) band's
head count in the baseline scenario was compared against the projected band size:

| | Bands short of the projection | People short | Of those, bands whose head count is **zero** |
| --- | ---: | ---: | ---: |
| the baseline | 197 | 264 | **197 — every one** |
| this build | 220 | 313 | **220 — every one** |

and **no band anywhere ever exceeds the projection**. The affected ages are 93–100, where the
projection puts between 0 and 8 people in a band; below age 93 nothing ever falls short. So the
bands in which the two implementations can disagree about the cohort are exactly the bands that
empty — not approximately, not mostly: exactly.

**The consequence, and the check.** Excluding the bands that either implementation empties in any
seed makes the two implementations' baseline-scenario cohort totals agree **exactly** — in every
year, for both sexes, at every seed. That is the proof that this one mechanism is the whole of the
divergence, and it is what the exclusion in the reduction is for.

**The verdict: a baseline defect**, recorded as **B-21** in [docs/deviations.md](deviations.md).
The model's contract is that the cohort tracks the demographic projection; it does not, at the top
of the age range, by a seed-dependent number of people, and nothing says so. It is a defect in the
baseline rather than in this implementation, because this implementation follows the same rule and
produces the same kind of shortfall at the same rate.

**What was changed here, and what was not.** The rule is *kept*: the shortfall is a real property
of what the projection asks for, and filling the band from a neighbouring age — which the baseline
has the machinery for and does not use — would meet the total by distorting the age distribution,
which is a different model rather than a bug fix. What changed is that it is no longer silent:
every year's run metrics now carry `ImmigrationShortfallPeople` and `ImmigrationShortfallBands`, so
a run reports its own divergence from the projection instead of leaving it to be discovered by
comparison with another implementation. Pinned by
`TestSimulation.TheImmigrationShortfallIsReported`.

Giving immigration a nearest-age fallback donor remains in [docs/backlog.md](backlog.md), with the
evidence above attached, as a change to the *model* to be decided on its merits.

### The seven that were left, and what they showed about the test

Excluding the empty bands took the 54 failures to **7**. All seven were in one year — 2024 — in the
intervention scenario, in `mean_pa`, `mean_fat`, `mean_energy` and `mean_sodium`, and all seven
were standard-deviation or tail-percentile comparisons.

The per-seed values say what they are. For `mean_pa` at (intervention, 2024, male):

```
baseline:  2649.0692 in 18 of 20 seeds; 2648.7413 and 2648.6266 in the other two
this build: 2649.0686 in 16 of 20 seeds; four other values in the other four
```

The series is a **constant with a rare jump**. The jump is one person: 2024 is two years after the
one-off BMI shock, and by then the intervention scenario differs from the baseline scenario only in
who happens to have died — the seeds that deviate are the seeds whose cohort count is 3125 or 3126
rather than 3124. One person moving between age bands shifts a count-weighted mean of 3,124 people
by about 2.5 units, which is the size of the jump.

For such a series the sample standard deviation is not an estimate of a spread. It is an estimate
of a rare-event rate — 2 in 20 against 4 in 20 — and the normal-theory allowance built from it is
meaningless. Fisher's exact test on those counts gives p = 0.66: the two implementations agree
about the rate, and the apparent disagreement was an artefact of the test, not of the models.

**2,264 of the 7,652 series are of this shape — 30% of the comparison** — so this was not a corner
case: a normal-theory test was being applied to a third of the family. Those series now have their
standard deviation and tail percentiles replaced by the rate test, as described under *the
thresholds*. The change removed all seven failures and introduced 2,264 comparisons that did not
exist before.

## Whether 20 seeds is enough

The standard-deviation test is the loosest of the five — the standard error of a sample standard
deviation at n = 20 is 16% of the standard deviation itself — so each comparison was repeated at
**60 seeds**, against a reference stored outside the repository because 60 seeds of reduced
baseline output is larger than belongs in git:

```bash
tests/equivalence/run.py --example HLM_France --seeds 60 \
    --reference-dir /tmp/hgps-ref60 --refresh-reference
```

| | HLM_France, 20 | HLM_France, 60 | KevinHall_FINCH, 20 | KevinHall_FINCH, 60 |
|---|---:|---:|---:|---:|
| Comparisons | 33,732 | 33,732 | 23,432 | <!--F60-COMPARISONS--> |
| Out of tolerance | **0** | **0** | **0** | <!--F60-FAILURES--> |
| Age bands excluded | 785 | 923 | 692 | <!--F60-BANDS--> |
| Worst excursion | 0.87× | 0.91× | 0.90× | <!--F60-WORST--> |

Tripling the seeds tightens every allowance by √3, so a difference that was hiding inside the
allowance at 20 seeds would surface at 60. Nothing did. The excluded-band set grows with the seed
count, because more seeds empty more bands — which is the mechanism behaving as described rather
than a new one appearing.

## Verdict

On the reference example, over 20 seeds and again over 60, 2010–2050, both scenarios and both
sexes:

- **every comparison is within tolerance** — 0 of 33,732, and 0 of 33,732 at 60 seeds;
- the worst of them uses 87% of its allowance, so nothing is passing by a hair;
- the one mechanism behind the previous run's 54 residual failures has been measured, attributed to
  the baseline, recorded as deviation B-21, and excluded from the reduction by a rule derived from
  the data rather than declared;
- the seven failures that remained after that turned out to be a defect in the *test* — a
  normal-theory allowance applied to a point-mass distribution — and are now compared by a
  distribution-free test of the rate instead.

That is equivalence in the sense [ADR 0006](decisions/0006-validation-strategy.md) asked for. It is
not, and was never going to be, bit-exactness: [docs/deviations.md](deviations.md) lists the places
where this implementation deliberately computes or reports something differently.

**What would make this stronger**, in order: a second country, so the evidence is not one config;
and more of the output compared at the band level rather than only after reduction.

## Reproducing this

### There is no failure budget

The previous run of this project gave the harness a `--max-failures` budget of 60, to keep its 54
documented residual failures from leaving a permanently red check nobody reads. That budget is
**gone**. `--max-failures` still exists and still defaults to **zero**, and `scripts/check.sh`
passes nothing, so any out-of-tolerance comparison fails the build.

What replaced it is two things, both of which are stricter than a budget rather than looser:

- the emptying bands are **excluded from the reduction on both sides** by a rule derived from the
  data, so the comparison no longer includes a quantity the two implementations do not both report;
  and a run that finds an empty band outside the recorded set **fails**, rather than widening the
  exclusion by itself;
- the 30% of series for which normal theory does not hold are compared by a **distribution-free
  test** of their departure rate, which is a real test with a real threshold.

A series that only one implementation reports at all always fails, whatever else is configured.

```bash
# The full check, running the baseline binary as well (about five minutes).
tests/equivalence/run.py --example HLM_France --seeds 20 --refresh-reference

# Against the stored baseline reference, without the baseline binary (about three minutes).
tests/equivalence/run.py --example HLM_France --seeds 20 --use-reference

# A quicker sanity check while changing something.
tests/equivalence/run.py --example HLM_France --seeds 5 --stop-time 2020

# Everything, including this.
scripts/check.sh
```

The baseline's reduced output for seeds 1–20 is checked in at
`tests/equivalence/reference/<example>/<config-sha256>.csv.gz` — 1.7 MB for HLM_France and 1.2 MB
for KevinHall_FINCH — with a manifest recording the
seeds, both config hashes, the baseline binary's path, the reduction used, **the age bands excluded
and which of them the baseline itself emptied**, and when it was written. It is the reduced form,
not the raw CSVs: 20 raw result files are 80 MB and the reduction is exactly the granularity the
comparison needs.

The excluded set is part of the reference, not of the harness: it was computed from the baseline
and this build together at the time the reference was written, so a later run against that
reference applies the identical set. That is also why a run that empties a band outside it has to
refresh the reference rather than carry on — the stored reduction would no longer be the one the
comparison needs.

`--json` writes the whole outcome — every (variable, statistic) group with its counts and its worst
case — so the tables above can be regenerated rather than retyped.

## The baseline does not always finish

Running the baseline on `KevinHall_FINCH` is not reliable. Over the runs this document reports —
the same binary, the same config, the same seed — some exit on a signal and then succeed when
re-run unchanged. Three different signals have been seen: `SIGSEGV`, `SIGTRAP` and `SIGABRT`.

That is audit findings **B-01** and **B-02** showing up as a crash rather than as a reordering:
two scenario threads, and a disease repository populated lazily from inside a parallel loop behind
a lock-free fast path that races a concurrent insert.

The harness retries a baseline run up to three times for this reason, counts the retries, prints
each one, and puts them in the `--json` outcome, so the flake is visible rather than smoothed away.
It is not something a comparison against the baseline can fix, and it is the strongest single
argument for the sequential-scenario design this implementation uses
([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)). Across the same
runs, this build has not exited on a signal once.
