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
except for a **lattice-valued** series, where four of the five are replaced by one exact test of
the whole distribution (below). That is 31,468 comparisons on HLM_France and 22,679 on
KevinHall_FINCH.

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

**Where normal theory does not apply: lattice-valued series.** Every standard error above assumes
the seeds are a sample from something like a normal distribution. For a large minority of the
series — **2,264 of HLM_France's 7,652 and 625 of KevinHall_FINCH's 4,924** — that is plainly
false. Two shapes, with one consequence:

- a population aggregate that calibration pins, where the seed moves nothing except whether one
  particular person happened to die: the same value in most seeds, with rare jumps;
- a count over a denominator — the incidence or prevalence of a rare disease — which can only be
  0, one case, two cases: values on a **lattice**.

In both, every **quantile** of the sample is a lattice point, so a quantile comparison has a
resolution of one whole lattice step. And the normal-theory allowance shrinks as 1/√n while the
lattice step does not, so such a comparison gets *worse* with more seeds. That is not a theoretical
worry: it is what the 60-seed FINCH confirmation found, and it is set out below under *the eighteen
medians*.

So a series is treated as lattice-valued when **either** the two implementations' samples pooled
take at most **six distinct values at the baseline's printed precision**, **or** one value covers
more than half of either sample. For such a series:

- the **mean** is compared exactly as before. It is not a lattice point, its allowance shrinks
  correctly, and for a rare-disease series it is the summary that carries the content;
- the standard deviation and all three quantiles — every one of which is a function of the same
  counts — are replaced by **one exact test of those counts**: a two-sided Fisher exact test per
  distinct value, "this value against every other", Bonferroni-corrected for the number of values
  tested. That tests the whole shape of the discrete distribution rather than three points of it.

Bucketing at printed precision is part of the rule and not a detail. `0.00029274` and
`0.000292741` are one value that the baseline cannot print apart, and counting them as two was
enough to hide a lattice series from an earlier version of this rule.

**How strong that test is, exactly.** It is a real test rather than a waiver, but it is blunt at
twenty seeds, and the numbers are worth stating rather than assuming. Against the family-wide
α = 10⁻⁵, for a two-valued series:

| | n = 20 | n = 60 |
|---|---|---|
| baseline never leaves one value; this build leaves it in *k* seeds | fails at k = 14 | fails at k = 17 |
| baseline at 0 in half its seeds; this build at 0 in *k* | never fails, even at k = n | fails at k = 54 |

So **a rare-event rate is barely testable at twenty seeds and properly testable at sixty**. That is
a second reason for the 60-seed confirmation, independent of the one the standard deviation gives.
`tests/equivalence/run_test.py` pins both rows.

**What is skipped.** A burden, death, emigration or incidence variable in the first simulated year,
where the quantity is not defined yet: 56 comparisons on HLM_France and 136 on KevinHall_FINCH.
Nothing else is excluded.

## The result — HLM_France

**31,468 comparisons over 20 seeds. Zero out of tolerance.**

| Statistic | Failed | Compared | Worst excursion that passed |
| --- | ---: | ---: | --- |
| mean | **0** | 7,652 | 0.82× the allowance (`prevalence_osteoarthritisknee`, baseline 2029 male, 0.06607 against 0.06186) |
| median | **0** | 5,388 | 0.78× (`mean_age`, intervention 2046 male, 43.956 against 44.216) |
| 5th percentile | **0** | 5,388 | 0.74× (`normal_weight`, baseline 2044 male, 13.687 against 14.124) |
| 95th percentile | **0** | 5,388 | 0.74× (`above_weight`, baseline 2044 male, 20.813 against 20.375) |
| standard deviation | **0** | 5,388 | 0.87× (`mean_bmi`, intervention 2024 female, 0.000858 against 0.003127) |
| distribution | **0** | 2,264 | p = 1 for every one of them |

Nothing sits on the edge: **the worst comparison in the whole run uses 87% of its allowance.** That
is a different kind of result from "everything passes", because a set of comparisons clustered at
0.99× would mean the thresholds were doing the work rather than the code.

The distribution row deserves its plain reading. `p = 1` on all 2,264 means that for every
lattice-valued series in this example, the two implementations' counts are either identical or
close enough that the corrected exact test reaches its ceiling. These are France's calibrated band
aggregates, which are seed-independent by construction, so that is the expected answer — and it is
the answer the test gives rather than one assumed.

## The result — KevinHall_FINCH

**22,679 comparisons over 20 seeds. Zero out of tolerance.**

| Statistic | Failed | Compared | Worst excursion that passed |
| --- | ---: | ---: | --- |
| mean | **0** | 4,924 | 0.85× the allowance (`obese_weight`, intervention 2032 male, 9.1290 against 9.4144) |
| median | **0** | 4,277 | 0.90× (`obese_weight`, intervention 2031 male, 9.0486 against 9.4425) |
| 5th percentile | **0** | 4,277 | 0.86× (`mean_fat`, intervention 2027 male, 107.954 against 108.315) |
| 95th percentile | **0** | 4,277 | 0.76× (`incidence_esophaguscancer`, intervention 2032 male, 0.000314 against 0.000851) |
| standard deviation | **0** | 4,277 | 0.74× (`mean_fruit`, baseline 2031 female, 0.6642 against 0.2750) |
| distribution | **0** | 647 | p = 0.081 against a threshold of 10⁻⁵ (`incidence_kidneycancer`, baseline 2030 female, modal share 0.60 against 0.50) |

The worst numeric comparison uses 90% of its allowance, and the smallest distribution p-value is
0.081 — nearly four orders of magnitude clear of its threshold. Nor is there a direction to what
does not agree exactly: over the 567 (variable, statistic) groups, this build's worst case is above
the baseline's in 55 and below it in 59.

### Getting there

FINCH did not pass first time. At 20 seeds the count of out-of-tolerance comparisons went 1,542 →
1,326 → 1,202 → 269 → 52 → 19 → **0**, and every step was a defect in this implementation rather
than a loosened threshold. (Those counts are against the comparison as it then was; the lattice
rule below changed both the count and which comparisons exist, and the last entry is zero under
either.) The last two steps are worth recording because both were the same mistake in different
clothes — **calibrating onto the wrong target**:

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
`Energy` becomes `EnergyIntake`, which is FINCH's name for it.

### On the FINCH surface, an intervention scenario does nothing at all

That is not a defect here and it is not a defect in the comparison; it is how the baseline is
built, and this build reproduces it. In the whole of the baseline, `Scenario::apply` — the call
that hands a person and a risk factor to the active policy — has **exactly one call site**:

```
hgps_main/src/HealthGPS/dynamic_hierarchical_linear_model.cpp:110
```

The `StaticLinear` and `KevinHall` models never call it. So the six intervention scenarios reach
the **HLM** surface and nothing else, and on `KevinHall_FINCH` all six — including
`food_labelling`'s energy adjustments and `fiscal`'s impact types — are inert. This build has the
same single call site, in `src/model/riskfactor/hlm_model.cpp`.

It is measurable rather than inferred. Running FINCH with `marketing` active and with `simple`
active gives, for the same seed, **byte-identical reduced output in both scenarios and in both
implementations** — 2,530 of 2,530 series identical on each side.

That explains something the config looked odd about: `KevinHall_FINCH` ships `simple` with an empty
impact list because filling it would change nothing. And it is why FINCH's policy lives somewhere
else entirely, in `policy_start_year` and the S1 policy-effect coefficients.

So what the five extra FINCH runs establish is narrower than it looks, and the narrow thing is
worth having: **both implementations agree that these policies are inert on this surface**, for
each policy separately, which is what catches an implementation that wired `apply` into a model the
baseline does not. What they do not do is exercise the five policies' own rules — that is
`HLM_France`'s job, where the definitions are upstream's own and the model does consult them.

Whether an intervention scenario *should* reach the Kevin Hall surface is an upstream design
question, and it is in [docs/backlog.md](backlog.md) as one.

### The results

On `HLM_France` each policy produces a genuinely different future, which is the precondition for
the comparison meaning anything and is checked rather than assumed. Against the same seed, the
intervention scenario's reduced series differ from `simple`'s in:

| Policy | Series that differ, of 3,854 |
|---|---:|
| `marketing` | 2,102 |
| `food_labelling` | 2,180 |
| `physical_activity` | 2,171 |
| `fiscal` | 2,108 |

And every one of those futures agrees with the baseline's:

| Example | Policy | Comparisons | Out of tolerance |
|---|---|---:|---:|
| HLM_France | `simple` | 31,468 | **0** |
| HLM_France | `marketing` | 31,468 | **0** |
| HLM_France | `dynamic_marketing` | 31,468 | **0** |
| HLM_France | `food_labelling` | 31,552 | **0** |
| HLM_France | `physical_activity` | 31,552 | **0** |
| HLM_France | `fiscal` | 31,363 | **0** |
| KevinHall_FINCH | `simple` | 22,679 | **0** |
| KevinHall_FINCH | `marketing` | 22,679 | **0** |
| KevinHall_FINCH | `dynamic_marketing` | 22,679 | **0** |
| KevinHall_FINCH | `food_labelling` | 22,679 | **0** |
| KevinHall_FINCH | `physical_activity` | 22,679 | **0** |
| KevinHall_FINCH | `fiscal` | 22,679 | **0** |

The comparison counts differ a little between policies on HLM_France because a different policy
empties a slightly different set of age bands, and the excluded set is derived from the runs. They
are identical across the FINCH rows for the reason the next section gives.


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

### The eighteen medians, and why more seeds made a test worse

The 60-seed FINCH confirmation failed where the 20-seed run had passed: **18 comparisons out of
tolerance, every one of them a median, every one of them a rare cancer.** That is the wrong way
round for a test — more evidence should not produce more failures of a correct implementation —
and working out why is what produced the lattice rule above.

`incidence_esophaguscancer` at (intervention, 2025, male) is a count over a denominator: zero
cases, one case, or two. Over sixty seeds the two implementations' counts were

| | 0 cases | 1 case | 2 cases |
|---|---:|---:|---:|
| the baseline | 26 | 28 | 6 |
| this build | 33 | 21 | 6 |

Fisher's exact test on those cannot tell them apart — p = 0.27 — and the means agree to well inside
their allowance. But the zero share crosses one half between them (43% against 55%), so the
**median** jumps from one case to zero: a whole lattice step, 0.000293, because a quantile of a
lattice-valued sample is itself a lattice point.

And the allowance at sixty seeds is 0.0002. **It is smaller than one lattice step**, so that
comparison cannot pass unless the two medians are identical. At twenty seeds the allowance was
0.00035 — larger than a step — and it passed. The allowance shrinks as 1/√n; the lattice step does
not. Every extra seed made the test more likely to fail on an implementation that is right.

That is a defect in the test, exactly as the seven below were, and the fix is the lattice rule:
those four statistics are functions of the counts, so compare the counts. Re-scored from the same
stored runs, with neither implementation re-run, the 18 failures become 0 and the smallest
distribution p-value in the whole FINCH comparison is 0.055 against a threshold of 10⁻⁵.

The cost is stated rather than hidden: the comparison count falls, from 33,732 to 31,468 on
HLM_France and from 23,432 to 22,679 on KevinHall_FINCH, because a lattice series that was
compared five ways is now compared two. What went were four statistics measuring the same counts
under an assumption that did not hold; what replaced them is an exact test of those counts and a
mean that was always the informative summary.

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

Twice over, no. The standard-deviation test is the loosest of the five — the standard error of a
sample standard deviation at n = 20 is 16% of the standard deviation itself — and the exact test on
a rare-event rate is barely able to fire at twenty seeds at all (see *how strong that test is*,
above). So each comparison was repeated at **60 seeds**, against a reference stored outside the
repository because 60 seeds of reduced baseline output is larger than belongs in git:

```bash
tests/equivalence/run.py --example KevinHall_FINCH --seeds 60 \
    --reference-dir /tmp/hgps-ref60 --refresh-reference
```

| | HLM_France, 20 | HLM_France, 60 | KevinHall_FINCH, 20 | KevinHall_FINCH, 60 |
|---|---:|---:|---:|---:|
| Comparisons | 31,468 | 31,468 | 22,679 | 22,745 |
| Out of tolerance | **0** | **0** | **0** | **0** |
| Age bands excluded | 785 | 923 | 692 | 711 |
| Worst numeric excursion | 0.87× | 0.91× | 0.90× | 0.96× |
| Smallest distribution p | 1 | 1 | 0.081 | 0.055 |

Tripling the seeds tightens every allowance by √3, so a difference that was hiding inside the
allowance at 20 seeds surfaces at 60. **Something did**, and it was a defect in the test rather
than in either implementation: eighteen medians of lattice-valued series, above. Once that was
fixed, nothing else did. The excluded-band set grows with the seed count, because more seeds empty
more bands — which is the mechanism behaving as described rather than a new one appearing.

The 60-seed runs are what the evidence rests on where the two disagree, because at sixty seeds both
of the weak tests become sharp: the standard deviation's standard error falls to 9%, and the exact
rate test can separate 30-in-60 from 54-in-60, which at twenty seeds it could not do at any
difference at all.

## Verdict

Two examples, one per model family. Over 20 seeds and again over 60, every scenario and both sexes:

- **every comparison is within tolerance** — 0 of 31,468 on HLM_France and 0 of 22,679 on
  KevinHall_FINCH, at both seed counts;
- the worst numeric comparison anywhere uses 96% of its allowance and the great majority sit far
  below, so nothing is passing by a hair and nothing suggests the thresholds are doing the work;
- **each of the six interventions is compared on its own**, 20 seeds each, on both examples;
- the mechanism behind the previous run's 54 residual failures has been measured, attributed to the
  baseline, recorded as deviation B-21, and excluded from the reduction by a rule derived from the
  data rather than declared;
- the residual failures that survived that were, twice, defects in the **test** rather than in
  either implementation — a normal-theory allowance applied first to a point mass and then to a
  quantile of a lattice — and both are now compared by an exact test of the counts, with the
  harness's own 26 tests pinning the rules.

That is equivalence in the sense [ADR 0006](decisions/0006-validation-strategy.md) asked for. It is
not, and was never going to be, bit-exactness: [docs/deviations.md](deviations.md) lists the places
where this implementation deliberately computes or reports something differently.

**What would make this stronger**, in order: a second *country* for the FINCH surface, so that
evidence is not one data pack — which needs `KevinHall_India` to be runnable at all, and it is not
([docs/examples.md](examples.md)); more of the output compared at the band level rather than only
after reduction; and a CI runner, because two stored references that nothing exercises
automatically are a document rather than a check.

## Reproducing this

### There is no failure budget

The previous run of this project gave the harness a `--max-failures` budget of 60, to keep its 54
documented residual failures from leaving a permanently red check nobody reads. That budget is
**gone**. `--max-failures` still exists and still defaults to **zero**, and `scripts/check.sh`
passes nothing, so any out-of-tolerance comparison fails the build.

What replaced it is three things, each of which is stricter than a budget rather than looser:

- the emptying bands are **excluded from the reduction on both sides** by a rule derived from the
  data, so the comparison no longer includes a quantity the two implementations do not both report;
  and a run that finds an empty band outside the recorded set **fails**, rather than widening the
  exclusion by itself;
- the series for which normal theory does not hold are compared by an **exact test of their
  counts**, which is a real test with a real threshold, and one whose power is measured and written
  down above rather than assumed;
- **the harness has its own tests** — `tests/equivalence/run_test.py`, 26 of them, run by CTest as
  `EquivalenceHarness.Rules` and therefore by `scripts/check.sh`. That matters more here than
  anywhere else in the repository: a mistake in the harness does not produce a wrong number, it
  produces the word PASS. Two of its rules have now been wrong once each, and both times what found
  it was a twenty-minute run of the real thing. The tests check Fisher's exact test against the
  lady-tasting-tea table and against 2/C(20,10), the type-7 quantiles against numpy's, the
  printed-precision bucketing, the count-weighted reduction and its band exclusion, and — directly
  — that the eighteen medians which failed now pass while a rate that really differs still fails.

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
