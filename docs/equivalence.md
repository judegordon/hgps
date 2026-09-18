# Statistical equivalence with the baseline

[ADR 0006](decisions/0006-validation-strategy.md) chose the validation strategy: port the
baseline's tests, and check **statistical equivalence** on the reference example rather than
bit-exact reproduction. Bit-exactness would have required preserving every RNG draw order and every
floating-point operation order in the baseline, which forecloses most of the improvements this
implementation exists to make. Equivalence leaves the design free and still answers the question
that matters — *do the two implementations produce the same distributions?*

This document is the result. It is produced by one command per example:

```bash
tests/equivalence/run.py --example HLM_France      --seeds 20
tests/equivalence/run.py --example KevinHall_FINCH --seeds 20
```

**Since this run, every comparison runs this build with `--baseline-compat all`** — it reproduces
the baseline's deliberate deviations, so the comparison tests everything *except* them and an
out-of-tolerance cell means something is wrong rather than something is different on purpose
([ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md)). The harness then runs this
build once more with the flags **off** and reports the difference as a **deviation impact** section,
per variable per year, signed — reported, never graded.

That changes two results below, and both changes are the same change: the `mean_bmi` cluster on
`HLM_India` and the one isolated `HLM_France` residual were **B-24**, and with the flag on B-24 is
not there. The sections describing them are kept as written, because how that cluster was
identified is the more interesting half, and [§ Measured directly](#the-deviation-measured-directly)
below has what the flag then said about it.

## The deviation, measured directly

The three runs made after the compatibility flag existed, 20 seeds each:

| Example | Intervention | Comparisons | Out of tolerance | Before the flag |
|---|---|---:|---:|---:|
| `HLM_France` | `simple` | 31,468 | **0** | 0 |
| `HLM_France` | `food_labelling` | 31,552 | **0** | 1 |
| `HLM_India` *(reduced cohort)* | `food_labelling` | 68,041 | **0** | 3 |

**131,061 comparisons, zero out of tolerance.** The previous residuals are gone, and they are gone
for a stated reason rather than because a threshold moved.

And the impact of turning the flags off — this build's own output minus the baseline-compatible
one, averaged over the same 20 seeds, so there is no Monte Carlo noise in it at all:

| | mean BMI, males, intervention | When | Relative |
|---|---:|---:|---:|
| `HLM_France` | **+0.0531** | 2037 | **+0.209%** |
| `HLM_India` *(reduced)* | **+0.0313** | 2050 | **+0.160%** |

This document has been quoting "about +0.2% of mean BMI" for B-24, inferred from which
out-of-tolerance cells looked like it. **The direct measurement agrees.** That is the good case,
and the reason to build the mechanism is the case where it would not have.

**It reaches further than mean BMI.** On `HLM_India`, **194 series differ** and 12,532 agree to the
printed precision — years of life lost, disability-adjusted life years, head counts, and the
prevalence and incidence of eleven diseases. A BMI that is wrong changes incidence, which changes
mortality, which changes the cohort. Nothing here said that before, because nothing could measure
it.

One consequence is worth stating because it bit on the first attempt: **the excluded-band set
depends on the compatibility flags**, because it is partly derived from this build's own runs and a
flag changes which bands empty. A stored reference is therefore tied to the flag setting it was
written with, and the harness's own check caught the mismatch and refused rather than comparing
against the wrong reduction. The `HLM_India` + `food_labelling` reference was refreshed; the others
were untouched, because on them no deviation reaches the run.

## What is compared, and how

The **method** — the reduction, the exclusions, the allowance and its derivation, the lattice
rule, and the self-consistency suite — is in [docs/equivalence-method.md](equivalence-method.md),
in one place, so that a reviewer can check what the comparison does without reading the script or
this document. What follows is what the method was applied *to*, and then what it produced.

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
the seed removed **and its input paths left relative**, and stores it beside the reference output.

The paths are left relative deliberately, and they were not always: until this run the hash was
taken over the absolutised config, so it carried the checkout's directory and a stored reference
could be found only on the machine that wrote it. Nothing noticed until CI ran the harness for the
first time, recomputed a different hash, found no reference and went looking for a baseline binary
that CI does not build. The four checked-in references were renamed to their new keys — the
contents are the baseline's reduced output and do not depend on any path — and
[docs/build-notes.md](build-notes.md) records it among the CI failures.

| | baseline config | this build's config |
| --- | --- | --- |
| `HLM_France` | `fda785fed5bc0b9636bbbf52cd2924080b698b0ec6378bc82f2a8e491d4b5bf7` | `8278eaac…` |
| `KevinHall_FINCH` | `abe1f3a0f07f76125d5bc791bd9da73ac48d16f03a9fb725eaa9e50117fc7a8f` | `4654ee92…` |

The two configs are equivalent by construction: the converter's defaults for
`project_requirements` are the baseline's own struct defaults, checked against
`hgps_main/src/HealthGPS.Input/poco.h`, and the column sets of the two result files are identical.

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

56 further comparisons are skipped, all of them a death, emigration, incidence or burden variable in
the first simulated year, where the quantity is not defined yet
([docs/equivalence-method.md](equivalence-method.md) §3.3). Nothing else is left out but the emptying
bands, below, and `std_income`, which does not arise on this example.

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

136 further comparisons are skipped as not defined in the first simulated year, and `std_income` is
excluded for as long as the baseline's series stays identically zero
([docs/equivalence-method.md](equivalence-method.md) §3).

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

**Since this run these five FINCH comparisons can no longer be produced by this build, and that is
deliberate.** A config whose active intervention declares impacts the configured dynamic model would
never apply is now refused at load time
([ADR 0035](decisions/0035-refuse-an-intervention-no-model-applies.md), deviation B-25), because a
run that reports success and no effect is the shape of thing somebody mistakes for a result. The
harness's FINCH overlay supplies non-empty impacts, so this build refuses exactly the five runs that
produced the paragraphs above.

That is a real cost of the decision, paid knowingly:

- what those runs showed is **unchanged and recorded here**, and it was never more than "both
  implementations agree these policies are inert on this surface";
- they cannot be re-run without changing the overlay to declare no impacts, at which point the
  comparison would be of two runs of the baseline scenario and would show nothing;
- the five policies' own rules are still compared against the baseline on `HLM_France`, which is
  where the definitions are real and the model consults them, and nothing about that changed;
- the *property* the five runs were evidence for — that this build does not wire `apply` into a model
  the baseline leaves alone — is now asserted directly instead, by
  `ModelLoader.OnlyTheDynamicHierarchicalModelConsultsTheActiveScenario`,
  `StaticLinearLoader.TheStaticLinearModelDoesNotConsultTheActiveScenario` and
  `KevinHallLoader.TheKevinHallModelDoesNotConsultTheActiveScenario`, in milliseconds rather than in
  an hour of runs.

`KevinHall_FINCH` with its own `simple` — the primary comparison, and the one the checked-in reference
is keyed to — is untouched: an empty impact list is accepted, with a warning saying what it means.

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
| HLM_France | `marketing` | 31,462 | **1** — see below |
| HLM_France | `dynamic_marketing` | 31,552 | **0** |
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
are identical across every FINCH row for the reason given just above: on that surface the policy
changes nothing, so every one of those runs is the same pair of futures.

### The one comparison in 347,768 that did not pass

Adding the 60-seed confirmations, the twelve runs above are **347,768 comparisons**. One is out of
tolerance:

```
HLM_France + marketing, incidence_osteoarthritisknee, intervention 2042 female
  median   baseline 0.0069778   this build 0.0052281
  allowed  0.0017220            difference -0.0017497      = 1.016x the allowance
```

It exceeds its allowance by 1.6%, and the evidence says it is the test's expected tail rather than
a difference in the code:

- **It is isolated.** The next-highest excursion anywhere in the 347,768 is **0.963×**, in a
  different run, a different example and a different variable. There is no cluster pressing against
  the limit, which is the shape a real difference would have.
- **The same series' mean agrees**, at 0.908× of its own allowance — and the mean is the statistic
  with the smallest standard error of the five.
- **There is no direction to it.** Across years and both scenarios, this variable's ratio of
  new-to-baseline mean scatters both ways — 1.131, 0.827, 1.113, 0.842, 0.897, 1.065 — with 2042
  at one tail of that scatter and nothing resembling a trend.
- **One is what the threshold is set to produce.** 4.5σ is Bonferroni at α = 0.05 over a family of
  about 5,000 series, so about **0.05 false failures per run**; fourteen runs were scored, so the
  expected count is about **0.7**. Observing one is the test behaving as designed.

**The threshold was not changed.** There is a real argument that a Bonferroni correction stated for
one run should be restated for a sweep of fourteen — and at α = 0.05 over the whole sweep the limit
would be about 4.9σ, which this comparison would clear. It is not being done, because moving a
threshold *after* seeing which comparison it excludes is not evidence, whatever the argument for
it. The comparison is reported as it stands.

`scripts/check.sh` runs the two primary comparisons — `simple` on each example, against the
checked-in references — and both are at zero out of tolerance. The ten policy runs are a deliberate
extra, run by hand, and this is their one residual.


## The result — HLM_India, at a reduced cohort

**Read this before the numbers: `HLM_India` was compared at one hundredth of the cohort it ships,
not as shipped.** Its `inputs.settings.size_fraction` is 0.001, which is 1,240,613 people and forty
minutes a run in this build; the harness's `--size-fraction 1e-5` puts **12,406** people through the
same code, which is about twice `HLM_France`'s 6,244 and `KevinHall_FINCH`'s 6,817. The value goes
into **both** implementations' configs identically and is part of the derived config, so it is part
of the config hash, so a reduced run cannot be compared against a full-scale reference by accident.

What that buys is the comparison at all: at full scale, twenty seeds of both implementations is
about a day. What it does not buy is a statement about the shipped cohort. Nothing below is evidence
about `HLM_India` at 1.24 million people, beyond the fact that it runs there — which
[docs/performance.md](performance.md) measures separately.

It is also the first example compared here whose **dynamic model is `EBHLM`** rather than `HLM`, and
the first with 35 diseases rather than 6 or 15.

### Two comparisons, because the example ships a policy the two implementations deliberately differ on

`HLM_India` is the only one of the three examples that ships an **active** intervention:
`food_labelling`. That matters, because `food_labelling` is where deviation **B-24** lives — the
baseline re-applies its impact to a person who failed an early coverage draw and passed a later one,
and this build applies it once ([docs/deviations.md](deviations.md)). So the example's own
configuration compares a policy the two implementations are *known and intended* to disagree about.

Both were therefore run: the example's own `food_labelling`, and `simple`, whose one-year absolute
shift has no coverage book and no memory of who it has affected.

| Run | Seeds | Comparisons | Out of tolerance | Bands excluded |
|---|---:|---:|---:|---:|
| `simple` | 20 | 67,885 | **0** | 1,641 |
| `simple` | 60 | 68,740 | 3 | 1,771 |
| `food_labelling` (the example's own) | 20 | 68,083 | 3 | 1,657 |
| `food_labelling` | 60 | 68,833 | 34 | 1,791 |

**All 31 `mean_bmi` failures are in a `food_labelling` run and there are none in a `simple` run, at
either seed count** — 3 at 20 seeds and 28 at 60. That is the whole attribution, and the rest of this
section is the evidence behind it.

### The 31 `mean_bmi` comparisons are deviation B-24, measured

B-24 was found by reading the baseline's code two runs ago and has never been visible in a
comparison until now. It is visible now, and the shape is unmistakable. Mean BMI of males, this
build minus the baseline, averaged over 20 seeds:

| Year | Baseline scenario | Intervention scenario |
|---:|---:|---:|
| 2021 | −0.00002 | −0.00002 |
| 2022 | −0.00002 | −0.00002 |
| 2023 | −0.00002 | **+0.00066** |
| 2024 | −0.00002 | **+0.00446** |
| 2025 | −0.00003 | **+0.01235** |
| 2026 | −0.00002 | **+0.03114** |
| 2030 | −0.00003 | **+0.04373** |
| 2050 | −0.00003 | **+0.03981** |

Five things, and each of them is what B-24 predicts:

1. **The baseline scenario agrees to 2×10⁻⁵ BMI units at every year of the horizon** — a relative
   difference of 10⁻⁷, which is the floor of what the baseline's six printed significant digits can
   express. The two implementations are not disagreeing about the model.
2. **The gap exists only in the intervention scenario**, which is the only place a policy runs.
3. **It is exactly zero in 2022, the policy's first year.** The defect needs a person who failed a
   draw in an *earlier* year and passes a later one, so it cannot express itself until the second
   year of the coverage window. It does not, and then it does.
4. **It grows through the coverage window and then stops growing.** `coverage_cutoff_time` is 4 from
   a start of 2022, so the window is 2022–2025; the gap grows 2023 → 2026 and is flat from 2026 to
   2050. After the window every person is decided once and never reconsidered, in both
   implementations, so no new divergence is created — and BMI is state, so what was created persists.
5. **The sign is right.** The impact lowers BMI, the baseline applies it more often than once, so the
   baseline's BMI is the *lower* of the two. It is.

**And the same signature is on `HLM_France`, where it passes.** Re-running that example with
`--intervention food_labelling` reproduces the recorded row exactly — 31,552 comparisons, 0 out of
tolerance — and inside it, the same curve: baseline scenario identical to the last printed digit,
intervention scenario +0.00179 in 2023 rising to +0.05101 (+0.203%) in 2026. Its worst `mean_bmi`
excursion is **0.820× of its allowance, at intervention 2026 male** — the same variable, the same
statistic and the same cell as India's worst, one notch under the line instead of one over.

So the difference is not India's. It is the same size on both examples, about **+0.2% of mean BMI in
the intervention scenario**, and India surfaces it because India's cohort here is twice France's,
which makes its seed-to-seed spread smaller and its allowance tighter. A tighter test found a real
difference that a looser one had been passing over. That is the test working.

**It is not corrected away, and the harness is not adjusted to admit it.** B-24 is a baseline defect
this build deliberately does not reproduce ([ADR 0024](decisions/0024-deviations-recorded-baseline-bugs-fixed.md)),
so a comparison that activates `food_labelling` on a surface where policies work *should* fail, and a
harness that passed it would be the thing that was wrong. What `simple` shows is that nothing else
does.

### The six that are not B-24, and what they say about the harness

The remaining failures — 6 at 60 seeds with `food_labelling`, 3 at 60 seeds with `simple`, none at 20
seeds in either — are all the same shape: the **95th percentile of a rare-disease rate**, at 1.02× to
1.09× of its allowance, scattered over five variables and single years. `incidence_gout` at
(baseline, 2019, female) fails in both 60-seed runs, which already says these are a property of the
comparison rather than of the policy.

They are the defect class the previous run found and fixed for the median: a quantile of a series
that lives on a lattice cannot be compared numerically, because the allowance shrinks as 1/√n while
the lattice step does not. The harness has a rule for that, it covers all three quantiles and the
standard deviation, and it did not fire here. Profiling the four failing series says why:

| Series | Distinct values, pooled | Modal share | Seeds that are exactly zero |
|---|---:|---:|---:|
| `incidence_gout` p95 | 43 | 0.12 / 0.32 | 7 / 60 and 5 / 60 |
| `prevalence_pancreascancer` p95 | 71 | 0.40 / 0.33 | 24 / 60 and 20 / 60 |
| `incidence_arthritis` p95 | 79 | 0.33 / 0.32 | 20 / 60 and 19 / 60 |
| `prevalence_livercancer` p95 | 78 | 0.37 / 0.30 | 22 / 60 and 18 / 60 |

The detector's two rules are "at most 6 distinct values" and "one value covering more than half the
seeds". These series have 43 to 79 distinct values and a modal share of 0.12 to 0.40, so neither
fires — **and they are lattice-valued anyway.** The numerator is a small integer count of cases; the
denominator is a band head count that differs from seed to seed. Dividing a small integer by a
varying denominator produces a different value almost every time, so a series that is a handful of
counts in disguise presents 79 distinct values to a detector that is looking at the *rate*.

A third of the seeds being exactly zero is the tell: the modal value **is** zero, at 0.30 to 0.40,
just under the 0.5 the rule wants.

**The threshold is not being moved and the rule is not being widened here**, for the reason this
document already gives about the one France failure: changing a rule after seeing which comparisons
it excludes is not evidence, whatever the argument for it. The fix is a real one and it is specific —
the detector should classify on the **numerator** rather than on the rate, which needs the harness to
carry the count alongside the reduced value — and it is in [docs/backlog.md](backlog.md) with this
measurement attached.

### The fix, and what it did to the stored references

Done this run, and no threshold moved: both rules are now applied to `value × count` for the same
(scenario, year, sex) and seed, bucketed at the baseline's printed precision exactly as the reduced
value was. The count was never thrown away — it is a summed variable of the reduction — so nothing
new had to be stored and no reference had to be refreshed.
[docs/equivalence-method.md](equivalence-method.md) §5.1 has the rule.

**It is a change of the quantity asked about and not of the threshold, and that is checkable rather
than asserted**: if the head count were the same in every seed, multiplying both the values and the
scale by it would leave every bucket exactly where it was. The change can only act where the
denominator moves, which is the whole of the defect.

All four stored references were re-scored against it:

| Example | Intervention | Comparisons, before | After | Out of tolerance |
|---|---|---:|---:|---:|
| `HLM_France` | `simple` | 31,468 | **31,546** | **0** |
| `KevinHall_FINCH` | `simple` | 22,679 | **22,616** | **0** |
| `HLM_India` *(reduced)* | `simple` | 67,885 | **66,787** | **0** |
| `HLM_India` *(reduced)* | `food_labelling` | 68,041 | **66,805** | **0** |

The comparison counts move in both directions, and that is the mechanism rather than noise: a series
that becomes lattice-valued loses its three quantiles and its standard deviation and gains one
distribution test, so it goes from five comparisons to two; a series that stops being one goes the
other way. India loses 1,236 comparisons, which is 412 series moving *into* the lattice class —
overwhelmingly the rare-disease rates this was about. France gains 78, which is 26 series moving
out: France's cohort is nearly seed-constant, so for most of its series the numerator's buckets are
the rate's buckets exactly, and the ones that move are the later years where deaths and migration
have made the head count vary.

### What became of the six residuals

The question the fix was made to answer, and the answer is *three of the four series, not all four*.

The previous run profiled the four series whose 95th percentiles failed at 60 seeds. Re-measured now,
with the case count taken from the stored 60-seed reference at (baseline, 2019, female) — the cell
`incidence_gout` failed in both runs:

| Series | Distinct *rates* | Distinct **case counts** | Modal share | Lattice now? |
|---|---:|---:|---:|:-:|
| `prevalence_pancreascancer` | 22 | **4** | 0.50 | **yes** |
| `incidence_arthritis` | 39 | **5** | 0.30 | **yes** |
| `prevalence_livercancer` | 20 | **5** | 0.52 | **yes** |
| `incidence_gout` | 50 | **9** | 0.28 | no |

Three of them are a handful of counts wearing dozens of rates, which is exactly what the detector was
missing; they are lattice-valued now and have no quantile comparison left to fail. **`incidence_gout`
is not**, and it is not meant to be: nine distinct case counts is above the six-value rule and a
modal share of 0.28 is below the half-share one, so it is a rare-event series with enough distinct
values for a quantile to carry information, and it is compared numerically as it should be.

**Measured as a pair**, because the previous run's counts came from a different code state and
comparing against them would be comparing two things at once: the same 60 seeds, the same stored
reference, the same binary, and one line of the detector different.

| `HLM_India`, 60 seeds | Comparisons | Out of tolerance | Which |
|---|---:|---:|---|
| `simple`, printed precision on the **rate** | 68,740 | **3** | `incidence_gout` 2019 (×2), `prevalence_thyroidcancer` 2046 |
| `simple`, on the **numerator** | 67,894 | **2** | `incidence_gout` 2019 (×2) |
| `food_labelling`, on the rate | 68,830 | **4** | `incidence_gout` 2019 (×2), `prevalence_stomachcancer` 2034, `prevalence_livercancer` 2036 |
| `food_labelling`, on the numerator | 68,047 | **3** | `incidence_gout` 2019 (×2), `prevalence_stomachcancer` 2034 |

One failure removed in each: `prevalence_thyroidcancer` at 2046 and `prevalence_livercancer` at
2036. Both are now lattice-valued and have no quantile comparison left to fail; neither passed by a
wider allowance, because the allowance did not change. Nothing that was passing began to fail.

The comparison count falls by 846 and 783 because that is what reclassifying a series does: its
per-year quantile comparisons are replaced by one distribution test over the whole series, which is
the stricter of the two on a series whose values really are a lattice — the previous run's residual
list is where that argument is set out. The surviving failures are identical numbers in both passes
(`incidence_gout` p95 baseline 0.00104759 against 0.000598623, 1.1× the allowance), which is the
check that the two passes differ in one line and nothing else.

`prevalence_stomachcancer` was not one of the four the previous run profiled. Re-measured the same
way, it has **8 to 10** distinct case counts across the four (scenario, sex) cells that carry it, with
modal shares of 0.22 to 0.33 — the same shape as `incidence_gout`'s 9 and 13. Both are rare-event
series with enough distinct counts for a quantile to carry information, and both are compared
numerically because that is what the rule says to do with them.

**The threshold is not moving from six to nine.** That would be changing a rule after seeing which
comparisons it excludes, which is what this document refused for the one `HLM_France` residual and
for the original version of this same rule. What is left is reported, with the measurement above
attached, and it is a smaller and better-understood residual than the one this run started with: two
comparisons in 67,894, on one variable, in one year, at 1.08× of a 4.5σ allowance.

**And the first version of the change was wrong, in a way only this re-score could have caught.** It
rounded the numerator to the nearest whole event — which is the right bucket for a case count and a
*finer* one than printed precision for anything large. A calibrated band mean of 25.541647 over 3,146
people is a numerator of 80,354, and one unit in that is 1.2×10⁻⁵ relative, just above the 10⁻⁵ floor.
Every calibrated mean on `HLM_India` failed: 216 comparisons out of tolerance in `mean_energy`,
`mean_pa`, `mean_bmi` and `mean_fat`, each a distribution test at p = 2.9×10⁻¹¹, all of them two runs
agreeing to every digit the baseline prints. The unit tests passed throughout, because none of them
had a numerator large enough for the difference between "a whole event" and "a printed digit" to
matter. `run_test.py` has one now.

### What the India comparison adds, and what it does not

**Adds:** a third example, a second dynamic model family (`EBHLM`), 35 diseases against 6 and 15, a
cohort twice the size of the other two, and about 273,000 further comparisons. A tighter test than
either of the others, which is how it found B-24 in the numbers and a hole in the harness's lattice
detector on the same run.

**Does not add:** anything about `HLM_India` as shipped. 12,406 people is not 1,240,613, the excluded
band count is about twice the other examples' because a smaller cohort empties more bands, and no
statement here extends to the full-scale run.

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

| | HLM_France, 20 | HLM_France, 60 | KevinHall_FINCH, 20 | KevinHall_FINCH, 60 | HLM_India, 20 | HLM_India, 60 |
|---|---:|---:|---:|---:|---:|---:|
| Comparisons | 31,468 | 31,468 | 22,679 | 22,745 | 67,885 | 68,740 |
| Out of tolerance | **0** | **0** | **0** | **0** | **0** | 3 |
| Age bands excluded | 785 | 923 | 692 | 711 | 1,641 | 1,771 |
| Worst numeric excursion | 0.87× | 0.91× | 0.90× | 0.96× | 0.99× | 1.09× |
| Smallest distribution p | 1 | 1 | 0.081 | 0.055 | 1 | 1 |

(The `HLM_India` columns are the `simple` runs at a reduced cohort — the comparison of the model
surface rather than of a policy. Its `food_labelling` runs are the other two rows of the table in
*The result — HLM_India*, and what they find is deviation B-24.)

Tripling the seeds tightens every allowance by √3, so a difference that was hiding inside the
allowance at 20 seeds surfaces at 60. **Something has, every time it has been tried**, and each time
the question was which of three things it was:

- the previous run: a defect in the **test** — eighteen medians of lattice-valued series, above;
- this run, on `HLM_India` with `food_labelling`: a real and **deliberate** difference in the code,
  deviation B-24, which went from 1.07× at 20 seeds to 1.68× at 60 exactly as a fixed difference
  against a shrinking allowance must;
- this run, on `HLM_India` with either policy: three to six comparisons at 1.0–1.1× that are the
  same defect in the test as the first item, in a corner its rule does not reach — a rare-event
  *rate*, whose lattice lives in the numerator.

That is the seed count doing its job three times over. The excluded-band set grows with the seed
count in every column, because more seeds empty more bands — which is the mechanism behaving as
described rather than a new one appearing.

The 60-seed runs are what the evidence rests on where the two disagree, because at sixty seeds both
of the weak tests become sharp: the standard deviation's standard error falls to 9%, and the exact
rate test can separate 30-in-60 from 54-in-60, which at twenty seeds it could not do at any
difference at all.

## Verdict

Three examples, covering both model families and both dynamic model families. Over 20 seeds and
again over 60, every scenario and both sexes:

- **every comparison in the two primary runs is within tolerance** — 0 of 31,468 on HLM_France and
  0 of 22,679 on KevinHall_FINCH, at 20 seeds and again at 60;
- **`HLM_India` agrees on the model surface and disagrees on exactly one policy**, which is a policy
  the two implementations are meant to disagree about: 0 of 67,885 with `simple` at 20 seeds, and
  with `food_labelling` a cluster of `mean_bmi` comparisons that is deviation **B-24** seen in the
  numbers for the first time, +0.2% of mean BMI in the intervention scenario, reproduced at the same
  size on `HLM_France`. It was compared at **one hundredth of its shipped cohort**, and nothing in it
  is evidence about the full-scale example;
- **each of the six interventions is compared on its own**, 20 seeds each, on both examples, which
  with the confirmations is **347,768 comparisons in all — one of which is out of tolerance**, by
  1.6%, isolated, with its own mean agreeing, and against an expected count of about 0.7 false
  failures over a sweep this size. It is reported above rather than corrected away;
- the next-highest excursion anywhere is 0.963× of its allowance and the great majority sit far
  below, so nothing else is passing by a hair and nothing suggests the thresholds are doing the
  work;
- the mechanism behind the previous run's 54 residual failures has been measured, attributed to the
  baseline, recorded as deviation B-21, and excluded from the reduction by a rule derived from the
  data rather than declared;
- the residual failures that survived that were, twice, defects in the **test** rather than in
  either implementation — a normal-theory allowance applied first to a point mass and then to a
  quantile of a lattice — and both are now compared by an exact test of the counts, with the
  harness's own **39** tests pinning the rules;
- and since this run, **the comparison runs with the deliberate deviations put back**, so the two
  residual clusters above are not there at all and the deviation that caused them is *measured*
  rather than inferred: **131,061 comparisons across three runs, zero out of tolerance**. See
  [§ The deviation, measured directly](#the-deviation-measured-directly).

That is equivalence in the sense [ADR 0006](decisions/0006-validation-strategy.md) asked for. It is
not, and was never going to be, bit-exactness: [docs/deviations.md](deviations.md) lists the places
where this implementation deliberately computes or reports something differently.

**And population impact fraction has no comparison at all.** It is implemented
([ADR 0038](decisions/0038-population-impact-fraction.md)), but `KevinHall_PIF` is the only example
that uses it and **neither implementation can run that example** — it shares `KevinHall_India`'s
weight defect, byte for byte, and the baseline dies in the same place
([docs/examples.md](examples.md), "Does not run — upstream data defect"). So the PIF mechanism is
validated **end to end on the synthetic fixture pack only**, by `tests/data/pif_data_test.cpp`, which
asserts that incidence falls in the intervention scenario, that the baseline scenario is untouched to
the last bit, and that a PIF run is as reproducible as any other. Against the real baseline it is
validated **not at all**, and that cannot change until upstream fixes the pack.

**What would make this stronger**, in order: a second *country* for the FINCH surface, so that
evidence is not one data pack — which needs `KevinHall_India` to be runnable at all, and it is not
([docs/examples.md](examples.md)); `HLM_India` compared at the cohort it ships rather than at a
hundredth of it; a lattice detector that classifies on the numerator, so the six rare-rate residuals
above are either explained or gone; and more of the output compared at the band level rather than
only after reduction.

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

Running the baseline on `KevinHall_FINCH` is not reliable. Counting the comparisons in this
document that actually ran the baseline binary: **180 FINCH runs, of which 4 exited on a signal and
succeeded when re-run unchanged** — about one in forty-five. Three on `SIGTRAP` and one on
`SIGABRT`; same binary, same config, same seed each time. `HLM_France` ran 80 times over the same
comparisons without a single failure, so it is the FINCH surface that provokes it.

That is audit findings **B-01** and **B-02** showing up as a crash rather than as a reordering:
two scenario threads, and a disease repository populated lazily from inside a parallel loop behind
a lock-free fast path that races a concurrent insert.

The harness retries a baseline run up to three times for this reason, counts the retries, prints
each one, and puts them in the `--json` outcome, so the flake is visible rather than smoothed away.
It is not something a comparison against the baseline can fix, and it is the strongest single
argument for the sequential-scenario design this implementation uses
([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)). Across the same
runs, this build has not exited on a signal once.
