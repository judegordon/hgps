# The equivalence method

The rules by which this implementation is judged to agree with the baseline, in one place, so that a
reviewer can check them without reading `tests/equivalence/run.py`.

[docs/equivalence.md](equivalence.md) is the companion: it reports what applying these rules
produced, example by example, and tells the story of how two of them turned out to be wrong.

Everything here is enforced in one script and tested by two others:

| | |
|---|---|
| `tests/equivalence/run.py` | the comparison. Runs both implementations, reduces, tests, reports. |
| `tests/equivalence/run_test.py` | 94 tests of the rules below, of the staging rule in ADR 0039 and of the deviation-impact measurement in ADR 0041, in milliseconds. Run by CTest. |
| `tests/equivalence/self_check.py` | the comparison pointed at itself: does it pass when it should, and fail when it should. Run by CTest. |
| `tests/equivalence/null_check.py` | the third question those two cannot answer: does it fail as **often** as it says it does (§4.5)? Run by CTest. |
| `tests/equivalence/sweep.py` | many seeds of one example, run once and stored, so a study can score the same runs many ways without re-running them. |
| `tests/equivalence/calibrate.py` | the studies: the null calibration behind §4.5, a direct test of one variable, and how far a twenty-seed standard deviation wanders. |

---

## 1. What a comparison is

Two Monte Carlo simulations with different random streams cannot agree exactly, and this project
does not ask them to: bit-exact reproduction of the baseline was ruled out up front
([ADR 0006](decisions/0006-validation-strategy.md)), because it would have required preserving every
draw order and every floating-point operation order in code this implementation exists to improve.

So the question is not "are the numbers the same" but **"are the two implementations drawing from the
same distributions?"** Each comparison is a hypothesis test: the null hypothesis is that the two
sides differ by sampling noise alone, and a comparison fails when the difference exceeds what that
noise allows.

Both sides get the same seed, the same input files, the same horizon, one trial run, one thread, and
the same active intervention. The two configs differ — the baseline reads its own v1 format and this
build reads config v2 — so the harness records the SHA-256 of each derived config with the seed
removed and stores it beside the reference output, which is what ties a stored reference to the
scenario that produced it.

## 2. The reduction: from rows to series

**Every CSV a run writes, not one of them.** A run writes the whole-population file, one
income-stratified file per configured income category and, where a config enables it, an
individual-tracking file. Until the eighth run this harness reduced the first and nothing else —
`find_result_csv` existed precisely to exclude the rest — and the price was measured rather than
argued: on `KevinHall_FINCH`, **45 columns of every stratum file were identically zero in this
build and non-zero in the baseline's**, and had been for as long as this build has written them.
Every key below therefore begins with the **output family**, and §2.1 says what that changes.

A result file has one row per (scenario, run, year, sex, **age band**). Comparing rows would be
meaningless: the two implementations' age bands hold *different people*, because their random
streams differ. Comparing population figures is not meaningless, so every file is reduced to one
value per **(family, scenario, year, sex, variable)**:

- `count`, `deaths`, `emigrations` and the four weight categories — `normal_weight`, `over_weight`,
  `obese_weight`, `above_weight` — are head counts, so they are **summed** over the age bands;
- everything else is a mean or a proportion within a band, so it is the **count-weighted mean** over
  the bands — which is the figure the variable reports for the population.

**The four weight categories were in the second bullet until this run.** The analysis module
increments one of them per person per band, in both implementations, and neither divides them by
anything — so a count-weighted mean of them is the average band's count rather than the population's
total: **15.3** for `normal_weight` on `HLM_France` at (baseline, 2030, male), where the population
figure is about 1,550. It made no comparison wrong, because both implementations were reduced
identically, and the series still moved with the underlying quantity, which is why it never looked
wrong. It made the number meaningless, here and in the server's charting endpoint, which applies the
same rule.

Fixing it changed every stored reference — a reference holds *reduced* values — so all four were
regenerated against the baseline binary ([docs/equivalence.md](equivalence.md)). The
one-list-two-readers rule is checked rather than trusted:
`SummaryReduction.TheSummedColumnsAreTheOnesTheHarnessSums` asserts the server's list is the
harness's `SUMMED_VARIABLES`.

A band with no people in it contributes nothing to a weighted mean and nothing to a sum.

The **seeds** then turn each series into a sample: 20 seeds give 20 values per series, and the
comparison is between the two samples.

### 2.1 What the family being part of the key changes

Three things, and each of them is a decision rather than a consequence.

**The head count a rate is reconstructed from is the family's own.** The lattice detector (§5)
rebuilds a count-weighted variable's numerator as `value × count`, and for a stratum file that
`count` is the stratum's head count. Reading the whole population's would give it a number two or
three times too large and classify the series wrongly.

**The emptying-band exclusion is taken from the whole-population file and applied to every family.**
A band is excluded because immigration cannot refill it once it empties, so the two
implementations' *cohorts* disagree there (§3.1, deviation B-21). That is a property of the
population band. A stratum band being empty is a different thing entirely — it is a real split of a
band both sides agree about — and excluding those would drop the stratified output of every band
nobody happens to be in, which on the two HLM examples, where nobody has an income category at all,
is every band there is.

**A family one side writes and the other does not is a failure, not a skip.** The previous run's
finding was not a wrong number, it was a file nobody was looking at, and a harness that quietly
compared the intersection would have the same blind spot with more code in it. The one recorded
exception is the baseline's individual-tracking file, which it opens for every run whose config
enables tracking and writes nothing into; the exclusion holds **only while that file is empty** and
turns back into a failure with its reason if it ever has a row
([docs/deviations.md](deviations.md), [docs/backlog.md](backlog.md) item 2).

**What this still cannot see**, and why `scripts/column-coverage.py` exists beside it: a column
that is zero on *both* sides. On `HLM_France` and `HLM_India` nobody has an income category, so
every stratum file is empty on both sides and the comparison agrees about nothing — which is a pass
and should not be read as a check. The coverage script asks of the files themselves which columns
are identically zero on each side, and fails where the baseline has numbers and this build has
none ([docs/equivalence.md](equivalence.md), *Every column of every family*).

## 3. What is left out of the reduction, and why

Three things, and they are different in kind. Nothing else is excluded.

### 3.1 The bands that empty — a real difference in the data, not a convenience

When immigration has to add somebody to an (age, sex) band, both implementations clone a person
already in that band. When the band is **empty** there is nobody to clone, so the immigration target
for that band is abandoned and the cohort falls short of the demographic projection it is calibrated
to. That is the baseline's rule, kept here deliberately — inventing a donor from a neighbouring age
would change the age distribution — and recorded as deviation **B-21**.

It is the one place the two implementations are not reporting the same quantity, so the bands that
**either** implementation empties, in **any** seed, are excluded from the reduction **on both
sides**. Measured on HLM_France, that exclusion is what makes the two cohorts agree *exactly* in
every year for both sexes at every seed.

Three properties keep this from being a licence:

- the excluded set is **derived from the runs**, not declared in the script;
- it is **recorded in the stored reference's manifest**, so a later run can be checked against it;
- a run whose empty bands differ from the recorded set **fails** and says which bands changed,
  rather than quietly widening the exclusion.

### 3.2 A variable the baseline does not compute

`std_income`, on the FINCH surface only. The baseline emits the column and never fills it: the loop
that accumulates squared deviations skips `income` on the ground that the mapping loop handles it,
and the mapping loop skips it for the same reason, so every value is exactly zero in every band of
every year of every run (deviation **B-22**). This build computes it. There is no number on one side,
so comparing it would fail for ever and say nothing.

The exclusion is conditional, which is the part that matters: the harness drops the variable **only
while the baseline's series is identically zero**, and compares it normally the moment that stops
being true. The exclusion cannot outlive the defect.

**And only while this build's series is not.** That half was missing until the review in §3.4, and
its absence was a hole of exactly the kind this document keeps finding: the rule's justification has
two halves — "the baseline never fills it" and "we do" — and it checked only the first. If this
build ever stopped computing `std_income`, the two series would be identically zero, the rule would
fire, and the harness would print *the baseline does not compute it* and skip — which is word for
word what it prints when everything is fine. A regression in the one variable the rule covers was
invisible, in the rule written to cover it. Both series identically zero is now **reported**, not
skipped, and `BaselineDoesNotComputeTest` pins all three cases.

### 3.4 Should any of these be a compatibility flag instead?

[ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md) makes every deliberate deviation
that changes outputs switchable, so its effect can be measured rather than argued. That raises the
question for the exclusions above, each of which is also a place where a known difference is kept
out of the comparison. All three were reviewed against it. **None is converted**, and the reason is
the same one in each case, stated once here:

> A compatibility flag is right when **both** implementations compute a meaningful number and they
> differ on purpose. Then the difference is the finding, and excluding it throws away the
> measurement. An exclusion is right when **one side has nothing to compare** — then there is no
> difference to measure, only an absence, and a flag would have nothing to toggle.

- **§3.1, the emptying bands.** Not a deviation at all in the sense that matters: B-21's rule is
  *kept*, so both implementations do the same thing, and what differs is which bands happen to empty
  under different random streams. There is no behaviour to switch. An empty band has no
  count-weighted mean, so there is nothing on either side. **Keep.**
- **§3.2, `std_income`.** The closest call, and the one worth arguing. It *is* a fixed defect that
  changes an output column's values, which is the ADR's criterion read literally. But the baseline's
  column is a placeholder, not a number: turning a flag on would make this build emit zero, the
  comparison would then be zero against zero, and that is not a stronger test than not comparing.
  The deviation-impact section would report this build's `std_income` series, which anyone can
  already read straight out of a result file. The flag would cost a branch inside the analysis
  module's hot loop to buy a number that is not hidden. **Keep** — and the review found the missing
  half of the rule instead, which was worth more than the conversion would have been.
- **§3.3, the first simulated year.** Not a deviation: both implementations agree that a flow
  variable has no value before anything has flowed. **Keep.**
- **§2.1, the `IndividualIDTracking` family.** The same test, one level up: the baseline opens the
  file and writes nothing into it, so there is no number on either side and a flag would have
  nothing to toggle. What it is instead is a **scope** gap — this build does not write the file at
  all ([docs/backlog.md](backlog.md) item 2) — and the exclusion is what records that honestly while
  the baseline's own file stays empty. **Keep.**

**And the 45 columns the eighth run filled were none of these.** They were the third case, which
neither a flag nor an exclusion fits: the baseline had numbers and this build had nothing, in a file
nothing was comparing. The answer there is to compute the column, and the reason it took a run to
find is that an absence in a file nobody reads looks exactly like agreement.

The distinction is not a formality. **B-24** — the deviation that prompted ADR 0041 — was on the
other side of it: two implementations computing a real mean BMI and disagreeing on purpose, with
nothing excluded, which is why it showed up as 28 out-of-tolerance comparisons that a person had to
attribute by hand. That is the case a flag is for.

### 3.5 What the comparison runs with

Since ADR 0041, every comparison runs this build with **`--baseline-compat all`**, so it reproduces
the baseline's deliberate deviations and the comparison tests everything except them. The harness
then runs this build once more with the flags **off** and reports the difference as the **deviation
impact** section — per variable, per year, per scenario, signed. Nothing in that section can fail a
run.

One consequence is worth stating because it bit on the first attempt: **the excluded-band set in
§3.1 depends on the compatibility flags**, because it is partly derived from this build's own runs
and a flag changes which bands empty. So a stored reference is tied to the flag setting it was
written with, and the check in §3.1 catches the mismatch and refuses rather than comparing against
the wrong reduction — which is what it did, on `HLM_India` with `food_labelling` active, the one
example where B-24 reaches the numbers. The reference was refreshed. On the examples where no
deviation reaches the run, the set is identical and the existing references were untouched.

### 3.3 The first simulated year, for quantities not defined in it

A death, emigration, incidence or burden variable has no value in the first year — nothing has
happened yet. Those comparisons are skipped, and counted in the report.

## 4. The rule, and the rate it delivers

**A comparison is a family of hypothesis tests with a chosen family-wise false-positive rate.**

```
alpha = 0.01, family-wise, per example, over every test the run performs
```

That sentence is the whole rule, and every other sentence in this section exists to make it true
rather than aspirational. It means: **if the two implementations really agree, the probability that
a run of one example reports even one failure is at most one in a hundred.** `FAMILY_WISE_ALPHA` in
`tests/equivalence/run.py` is the constant; it is the same number for every example; there is no
per-example constant and no failure budget (§6).

### 4.1 What this replaced, and why the old rule had no rate at all

Until the ninth run a comparison passed when

```
|difference| <= 4.5 x sqrt((s_b^2 + s_n^2)/n) + 1e-5 x scale
```

and the 4.5 came from a Bonferroni argument over "about 5,000 independent series". Two things were
wrong with it, and neither is the 4.5:

- **`s` is not `sigma`.** That is a *z* threshold applied to an *estimated* standard error. Measured
  on 400 seeds of `HLM_France`, over every series that really varies and every disjoint block of
  twenty — 108,920 (series, block) pairs — a twenty-seed standard deviation is **0.73 to 1.26** of
  the 400-seed one at the 5th and 95th percentiles, and **0.63 to 1.38** at the 1st and 99th. So
  `4.5 s` was anywhere between about **2.8 and 8.8 true sigma**, depending on the seed set. A
  threshold that wanders over that range does not have a false-positive rate.
- **The multiplicity count was a number in a comment.** By the eighth run one example made 111,836
  comparisons, not 5,000, and the exact test of §5 was judged against its own separate constant
  (`0.05/5000`) that never had to agree with the 4.5.

What that cost is in [docs/equivalence.md](equivalence.md): re-scoring one 60-seed
`KevinHall_FINCH` run over 100 random 20-seed subsets of itself — the same build against the same
baseline — gave between **0 and 45** failures, and 28 of the 100 subsets had none.
[ADR 0048](decisions/0048-a-comparison-with-a-stated-false-positive-rate.md) records the choice
between the two candidate replacements and why this one.

### 4.2 The three tests

Per series — per (family, scenario, year, sex, variable), on the two samples of *n* seeds §2
produced:

| The series | What is tested | The test | The statistic reported |
|---|---|---|---|
| continuous | **location** | Welch's unequal-variance t on the two samples | the two means |
| continuous | **dispersion** | Welch's t on each sample's absolute deviations from **its own median** — the two-sample Brown–Forsythe test | the two mean absolute deviations |
| lattice-valued or a point mass (§5) | **the whole discrete distribution** | Fisher's exact test per distinct value, Bonferroni-corrected over the values tested | the two modal shares |

Three things about that table are choices rather than consequences:

- **Welch's t is the same statistic the old rule used, referred to the distribution it actually
  has.** The difference of means over `sqrt(s_b^2/n + s_n^2/n)` is exactly what was compared to
  4.5 before. What changes is that it is now compared to Student's t on the Welch–Satterthwaite
  degrees of freedom, which is the distribution that quantity has when the standard error is
  estimated from the same draws. **At the level §4.3 leaves, a 20-against-20 test needs
  t = 6.30 where the old rule asked for z = 4.5**, and the gap between those two numbers *is* the
  estimation noise the old rule ignored.
- **Five statistics became two.** The mean, the standard deviation and the 5th, 50th and 95th
  percentiles were five noisy functions of one twenty-seed sample. Location and spread are what
  they were between them measuring, three quantiles of twenty draws add noise rather than
  information, and every extra test costs the family a slot.
- **A lattice series gets one test, not two.** Its mean is a function of the same counts the exact
  test already covers, so comparing it separately would spend a slot on a second look at one
  thing.

### 4.3 Holm, over every test the run performs

Every p-value from every test of every output family goes into **one** correction: Holm's
step-down, at `alpha`. A test fails when its **Holm-adjusted p-value is at most 0.01**.

Holm rather than Benjamini–Hochberg, for two reasons
([ADR 0048](decisions/0048-a-comparison-with-a-stated-false-positive-rate.md)). The series are
strongly dependent — twenty-two years of one variable move together, and a stratum's series moves
with the whole population's — and Holm needs no assumption about that while a false-discovery rate
does. And the question a check asks is family-wise: the answer is a verdict, "nothing in this run is
distinguishable from sampling noise", and what belongs to a verdict is the probability of being
wrong about it once.

The practical consequence, and the number to keep in mind when reading §5.3: Holm leaves
`alpha / m` for the **smallest** p-value in a run, and `m` is about 45,000 for `KevinHall_FINCH`,
so that level is about **2.2e-7**.

### 4.4 The floor: nothing can fail on a difference the baseline cannot print

The baseline writes its CSV with **six significant digits**. Each band figure therefore carries a
relative rounding error of up to 4e-6 and the difference of two of them up to 8e-6, so no
comparison can be tighter than about 1e-5 of the scale.

That precision is applied **twice, in this order**, and which of the two decides a given series is
worth knowing:

1. `lattice_keys` buckets both samples at it **before** the series is classified (§5), so two
   figures the baseline cannot print apart are one value. A series whose whole spread is below the
   precision has one bucket, is a point mass, and its exact test has nothing to compare.
2. The floor in `compare` then waives any remaining test whose **difference** is at or below
   `1e-5 x scale`, however small its p-value, where `scale` is the larger of the two means or the
   larger standard deviation when both means are near zero.

Under the rule this replaced, step 2 was load-bearing: for a series that does not move with the
seed the floor *was* the whole allowance. Under this one it is the unconditional guarantee, and it
still fires — 798 of `KevinHall_FINCH`'s 45,544 tests and 125 of `HLM_France`'s 16,486 are waived
by it. **It can only ever remove failures, so the 1% stays an upper bound.** It does not apply to
the exact test, whose samples went through step 1 already: that test's statistic is a modal share,
and two point masses at *different* values both have a modal share of 1, so a floor on that
difference would waive every point-mass comparison there is.

### 4.5 The calibration: the rate is measured, not asserted

**A rule that states a rate and is never checked against one is a rule with a number in it.** So
before this rule was adopted it was run against a null: **one build against itself, on disjoint seed
sets, on all three runnable examples**. Every failure there is a false positive by construction —
same build, same config, nothing perturbed, only the seeds differ — so the observed count is the
realised rate.

**Thirty null comparisons, 922,564 tests, zero failures.**

| Example | The sweep it was cut from | Pairs of 20 | Tests the floor does not waive | Runs with ≥1 failure | Expected |
|---|---|---:|---:|---:|---:|
| `HLM_France` | 400 seeds, this build | 10 | 163,505 | **0** | 0.10 |
| `HLM_India` *(reduced cohort)* | 400 seeds, this build | 10 | 311,370 | **0** | 0.10 |
| `KevinHall_FINCH` | 200 seeds of both, plus 80 | 10 | 447,689 | **0** | 0.10 |
| **total** | | **30** | **922,564** | **0** | **0.30** |

And the tail of the raw p-values, pooled, as **observed of expected**:

| p below | `HLM_France` | `HLM_India` | `KevinHall_FINCH` | all three |
|---|---:|---:|---:|---:|
| 10⁻² | 965 of 1,635 | 1,829 of 3,114 | 2,958 of 4,477 | **5,752 of 9,226** |
| 10⁻³ | 42 of 164 | 167 of 311 | 206 of 448 | **415 of 923** |
| 10⁻⁴ | 1 of 16.4 | 16 of 31.1 | 17 of 44.8 | **34 of 92.3** |
| 10⁻⁵ | 0 of 1.64 | 2 of 3.11 | 1 of 4.48 | **3 of 9.23** |
| 10⁻⁶ | 0 of 0.16 | 0 of 0.31 | 0 of 0.45 | **0 of 0.92** |
| 10⁻⁷ | 0 of 0.02 | 0 of 0.03 | 0 of 0.04 | **0 of 0.09** |

**Observed is below expected at every threshold on every example**, so the rule is conservative:
the 1% is an upper bound and the realised rate sits under it. The reason is not mysterious — a large
minority of these series are pinned by calibration or are a handful of events, and both of the rules
that handle those (the exact test of §5, and the floor of §4.4) are conservative by construction.
Anti-conservative behaviour in that tail is what would have stopped the rule being adopted, and
there is none.

Two things about how to read that table:

- **The headline is the first column pair.** `alpha = 0.01` over 30 null comparisons predicts 0.3
  runs with a failure. Observing fewer is the rule being conservative, which is the safe direction
  for a family-wise bound; observing more than about 2 would have been the rule failing its own
  claim, and the rule would not have been adopted.
- **The tail is where the verdict actually lives, and 30 runs can see it.** A run's verdict depends
  only on whether any raw p-value falls below about `alpha/m`, so the honest check is the whole
  tail of the raw p-values, pooled over every test the floor does not waive. Under the null those
  should be uniform, and the table gives expected against observed at six thresholds spanning the
  one that matters.

The same measurement runs at every commit, at a scale CTest can pay for:
`tests/equivalence/null_check.py` does four pairs of twenty seeds of the synthetic fixture pack in
about fifteen seconds and asserts that at most one of them reports anything. That bound is a trade
and the file says so: at most **zero** would fail 4% of the time for no reason, which over the ten
build configurations CI runs is a flake a third of the time.

## 5. Lattice-valued series, where normal theory does not apply

Both tests in §4.2 assume the seeds are a sample from something like a normal distribution. For a
large minority of series that is plainly false. Two shapes:

- an aggregate that calibration pins: the same value in most seeds, with rare jumps;
- a count over a denominator — the incidence of a rare disease — which can only be 0, one case, two
  cases: values on a **lattice**.

In both, every **quantile** of the sample is itself a lattice point, so a quantile comparison has a
resolution of one whole lattice step. And a normal-theory threshold shrinks as 1/√n while the
lattice step does not, **so such a comparison gets worse with more seeds** — the opposite of what a
test should do. That is why the three quantiles the rule before the ninth run compared were the
first casualty of both of the corrections this section records.

That is not a theoretical worry. It is what a 60-seed confirmation found: eighteen comparisons
failed at 60 seeds that had passed at 20, every one of them a median of a rare cancer, with
distributions Fisher's exact test cannot tell apart (p = 0.27 on the worst).
[docs/equivalence.md](equivalence.md) has the case in full.

### 5.1 The rule

A series is **lattice-valued** when either:

1. the two samples **pooled** take at most **six distinct values** — a continuous quantity gives one
   distinct value per seed, so this cannot catch one, and six is a quarter of the smallest seed count
   the harness accepts; **or**
2. one value covers **more than half** of either sample — a series can have many distinct values and
   still be a point mass with rare jumps, and when one value covers more than half the seeds, the
   median *is* that value, so it is a step function too.

**Both rules are applied to the numerator, not to the reduced value.** That was the eighth run's
correction, and it was the whole of it: neither threshold moved, and neither has moved since.

§2 reduces a per-band figure to one population figure per (scenario, year, sex). Counts are summed;
everything else is the count-weighted mean over the bands. So a disease rate comes out as *total
cases over total head count* — and the cases are a small integer while the head count differs from
seed to seed. Dividing one by the other smears the lattice. A series that is a handful of case counts
in disguise presents dozens of distinct **rates**, and both rules above, asked of the rate, miss it
completely.

The correction is to ask them of the count. Nothing new has to be stored: the reduction already
carries `count` as a summed variable, so for a count-weighted variable the numerator is
`value × count` for the same (scenario, year, sex) and seed, rounded to the nearest whole event.
`reduce_result` checks the identity that rests on — that the weight it divided by is the head count
it summed — and refuses rather than classifying from a wrong number.

A mean's numerator is a total rather than a count, so it stays continuous and is not mistaken for a
lattice; a band mean that calibration pins takes one value in every seed and its numerator does too,
because the cohort size is the same in every seed, so it stays a point mass. `run_test.py` pins both
of those as well as the case it fixes.

**Bucketing at printed precision is what the fallback does** — for a summed variable, whose reduced
value *is* its numerator, and for a series with no head count beside it. `0.00029274` and
`0.000292741` are one value the baseline cannot print apart, and counting them as two was enough to
hide a lattice series from an earlier version of this rule.

### 5.2 What a lattice-valued series gets instead

**One test, and it is exact**: a two-sided Fisher exact test per distinct value, "this value against
every other", Bonferroni-corrected for the number of values tested. That tests the whole shape of
the discrete distribution rather than three points of it, and being exact, its contribution to §4's
family-wise rate is at most its nominal one whatever the counts happen to be.

It gets **only** that test. Until the ninth run the mean was compared as well, on the ground that a
mean is not a lattice point; it is however a function of the same counts the exact test already
covers, and once every test in a run went into one multiplicity family (§4.3) a second look at one
thing cost the family a slot and bought nothing.

### 5.3 How strong that test is

Blunt at twenty seeds, and the numbers are worth stating rather than assuming. The p-value goes into
the same Holm family as everything else, so the level that decides it is the `alpha / m` of §4.3 —
about **2.2 × 10⁻⁷** for the ~45,000 tests one example produces. For a two-valued series:

| | n = 20 | n = 60 |
|---|---|---|
| baseline never leaves one value; this build leaves it in *k* seeds | fails at k = **17** | fails at k = **22** |
| baseline at 0 in half its seeds; this build at 0 in *k* | never fails, even at k = n | fails at k = **57** |

Those were 14, 17 and 54 while this test alone was judged against a hand-counted α = 0.05/5000 and
everything else against 4.5 sigma. **Putting every test in one family is what made the two levels
agree, and this table is the price of that agreement** — paid by the least sensitive test the method
has, and written down rather than left to be discovered. `tests/equivalence/run_test.py` pins the
n = 60 row.

So **a rare-event rate is barely testable at twenty seeds and properly testable at sixty.** That is a
reason for the 60-seed confirmation independent of the one the standard deviation gives.

## 6. The verdict

**There is no failure budget, and there is no flag that could grant one.** `run.py` has no
`--max-failures`, `report` has no parameter that could hold one, `scripts/check.sh` and the CI
matrix pass none, and `run_test.py` pins that. One failing test fails the run, on every example.

That is a change from the eighth run, which spent a budget of **3** on `KevinHall_FINCH`. The budget
existed because the rule then in force had no false-positive rate to appeal to (§4.1), so a count
was the only thing anyone could bound. A rule that states a rate and is measured against it (§4.5)
does not need one, and keeping the flag would have left a way to not notice if it did.

A run also fails, regardless of any test, if:

- a series is reported by one implementation and not the other;
- an output family is written by one and not the other, outside the one recorded exception, whose
  premise is checked rather than trusted (§2.1);
- the set of emptying bands differs from the stored reference's;
- either binary exits non-zero after its retries.

## 7. Does the comparison work? — the self-consistency suite

Sections 1 to 6 are rules, and rules can be wrong. Two of these have been: an earlier version of the
lattice rule bucketed at full precision and therefore hid the series it existed to catch, and an
earlier version of the emptying-band exclusion was applied to one side only. Both were found by a
twenty-minute run rather than by a test, which is the wrong way round — **a mistake in the harness
does not produce a wrong number, it produces a confident one.**

`run_test.py`'s 94 unit tests pin the arithmetic of each rule. They cannot answer either of the two
questions that matter about a comparison as a whole, nor the third that §4.5 is about.
`tests/equivalence/self_check.py` answers both,
using `run.py`'s own `compare` and `report` — imported, not copied, because the point is to test the
rules that decide the real result.

### 7.1 Does it pass when it should?

```bash
python3 tests/equivalence/self_check.py --mode seeds --example Synthetic --seeds 20
```

**The same build against itself at two disjoint seed sets.** Seeds 1–20 on one side, 1001–1020 on the
other. The two sides differ by nothing but sampling noise, which is precisely the null hypothesis
§4 is built on. A harness whose thresholds are too tight fails here — and a failure here is a
**false positive** in the real comparison, which is the expensive kind, because it sends somebody
looking for a defect that is not there.

Measured on the synthetic fixture pack: **458 tests, none failed**, with one waived by the
printed-precision floor.

The two samples are paired arbitrarily: seed *i* on one side against seed *i* + 1000 on the other. It
has to be arbitrary. The comparison is between two *distributions*, and a pairing that meant anything
would make it a different test.

**One draw is not a rate**, which is what §7.4 is for.

### 7.2 Does it fail when it should?

```bash
python3 tests/equivalence/self_check.py --mode perturbation --example Synthetic --seeds 20
```

**The same build against a deliberately corrupted copy of itself.** The corruption is a run-time knob,
`--perturb`, which transforms named output channels after the simulation and before the writer:

```
mean_bmi=scale:1.01;mean_energy=scale:1.05;std_energy=scale:1.05;emigrations=step:1
```

Four rules, and the interesting part is that **one of them is not detected**. The numbers below are
measured on the synthetic fixture pack at twenty seeds, not predicted, and they are the rule of §4:
a Holm-adjusted p-value over the run's 458 tests, against α = 0.01.

| Rule | What it does | Which test fires | Result |
|---|---|---|---|
| `mean_bmi=scale:1.01` | multiplies every band's mean BMI by 1.01 | exact distribution | **detected** in all 10, p = 2.9×10⁻¹¹, Holm 1.3×10⁻⁸ |
| `mean_energy=scale:1.05` | the same at 5% | exact distribution | **detected** in all 10, identically |
| `std_energy=scale:1.05` | 5% of a series that genuinely varies seed to seed | location (Welch's t) | **detected** in all 10, p = 6.5×10⁻²³, Holm 3.0×10⁻²⁰ |
| `emigrations=step:1` | adds one person to **one** age band | location | **not detected**, best p = 0.145 |

**Why the first two give the same p-value however different they are.** Calibration pins a band mean,
so `mean_bmi` and `mean_energy` are the *same value in every seed* on each side. Each side is one
bucket, the two buckets differ, and Fisher's exact test on twenty against twenty with complete
separation gives 2.9×10⁻¹¹ whatever the gap between the buckets is. A 1% shift and a 5% shift are
equally impossible under the null; the test says so and cannot say more. **That is the lattice path
working, and it is also the one place where the method reports certainty rather than size** — the
size is in the `difference` column of the report beside it.

**And it is the one place where the seed count decides whether the method can see anything at all.**
The exact test conditions on the margins, so the p-value for a complete separation is exactly
`2/C(2n, n)`; the test doubles it for the two values it tests, and Holm then multiplies by the
run's 300–460 tests:

| n | 6 | 8 | 10 | 11 | 12 | 20 |
|---|---|---|---|---|---|---|
| the test's p-value, `4/C(2n, n)` | 4.3×10⁻³ | 3.1×10⁻⁴ | 2.2×10⁻⁵ | 5.7×10⁻⁶ | 1.5×10⁻⁶ | 2.9×10⁻¹¹ |
| Holm over ~350 | 1 | 0.11 | 0.0076 | 0.0020 | 0.00052 | 1.0×10⁻⁸ |

So a shifted point mass is **out of reach at six seeds, marginal at ten and clear at twelve**, and
`self_check.py` derives its expectation from the seed count rather than asserting one. That matters
because CTest runs **six** seeds under ThreadSanitizer
([ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md)) and twenty everywhere else — and it
is a real change from the rule before
[ADR 0048](decisions/0048-a-comparison-with-a-stated-false-positive-rate.md), which detected these
at any seed count because it compared a point mass against the printed-precision floor rather than
testing it. "The two constants differ by more than the baseline can print" is an observation; at six
seeds it is not *evidence* that the two distributions differ, because six against six splitting two
values perfectly happens by chance about once in 230 tries and a run makes hundreds of tests. **The
TSan run of this test is what found that**, on the first full `check.sh` after the rule changed.

**Why `std_energy` is there at all.** The two above are seed-constant, so between them they exercise
only the exact path. `std_energy` takes twenty distinct values out of twenty seeds, so it is the one
rule that exercises Welch's t — the path most of a real comparison's tests take, and otherwise
untested here. Its **dispersion** test passes, at p = 0.84, which is right: scaling a series by 1.05
moves its mean by 5% of a large number and its across-seed spread by 5% of a small one.

**Why one person is invisible, in two steps.**

- In the **first** simulated year `emigrations` is identically zero on both sides, so a shift of one
  would be certain. But that year is skipped for `emigrations`, because the quantity is not defined
  until a year has passed (§3.3).
- In **every later year** the twenty seeds give eight or nine distinct values, so the series is not
  lattice-valued and Welch's t judges it. The best of its eight comparisons is **p = 0.145**, before
  any correction for multiplicity; Holm takes it to 1.

That is not a defect, it is the arithmetic: **one emigration in sixteen is the sort of difference
twenty seeds of this pack throw up about one time in seven by chance**, and a test claiming otherwise
would be wrong. Sixty seeds give p = 0.016 — measured, not extrapolated — which is still nowhere near
a family-wise 0.01 spread over hundreds of tests, and the 60-seed perturbation run fails in the same
three variables and no others.

It also exposes something subtler about the lattice rule, which is worth having written down: **the
classification is computed from the two samples pooled**, so a real shift can push a series *out* of
the lattice regime — eight distinct values on one side become sixteen pooled — and into a numeric
comparison too wide to see it. A shift is hardest to see exactly where it is large enough to change
the classification and small enough to be ordinary noise for the test it lands in.

**So the test asserts both halves.** `self_check.py` carries two sets: `DETECTED`, which must fail
exactly, and `BELOW_NOISE`, which must **not** fail although it is perturbed. A variable missing from
the first means the harness cannot see a difference that is really there. A variable appearing that is
in neither means the perturbation leaked into a series it does not name — a real risk, and the reason
`count` is not among the rules: it is the reduction's weight, so perturbing it would move every
count-weighted mean in the file. And a variable from `BELOW_NOISE` that starts failing is reported as
*news* rather than as a failure, with instructions to work out whether the method or the example
changed.

**A note on what this delivers against what was asked.** The brief for this suite said the perturbed
build must "fail on exactly the perturbed series and nothing else". Three of the four do; the fourth
provably cannot, for the reason above. Pinning that with an assertion, and the arithmetic with it, is
what was done instead — it is strictly more than "exactly these three fail", because it also says what
the method cannot do.

**The knob cannot be mistaken for an ordinary run.** It is off unless asked for and no example config
sets it; a specification that does not parse, or that names a channel the output does not have, makes
the run **fail** rather than quietly doing nothing — because a silently unperturbed run would make
this test pass for the wrong reason, which is the worst outcome available here; and every run manifest
records it, with `null` when it was not set
([ADR 0034](decisions/0034-a-run-manifest-beside-the-results.md)).

**Every case this suite detected before the rule changed, it still detects at the seed counts the
suite is run at.** That was checked directly rather than assumed, at twelve, twenty and sixty seeds:
the three `DETECTED` variables fail and the one `BELOW_NOISE` variable does not, which is exactly
the assertion the file carried before
([ADR 0048](decisions/0048-a-comparison-with-a-stated-false-positive-rate.md)). At **six**, two of
the three are out of reach, for the reason and with the arithmetic above.

### 7.3 Where they run

Both modes default to the **synthetic fixture pack**, whose runs take about a tenth of a second, so
twenty seeds of both sides is a couple of seconds — `EquivalenceHarness.SelfConsistencyAcrossSeeds`
and `EquivalenceHarness.DetectsADeliberatePerturbation` are ordinary CTest tests inside
`scripts/check.sh` rather than something somebody remembers to do.

`--example HLM_France` runs the same two checks against a real example, which takes about ten minutes;
[docs/equivalence.md](equivalence.md) records what that produced. The synthetic pack is the one that
runs on every commit, and its 458 tests over 31 variables and five years are enough to exercise every
rule in §§2–5 — including, because its aggregates are calibrated and therefore seed-constant, both the
printed-precision floor and the lattice path.

### 7.4 Does it fail as *often* as it says it does?

```bash
python3 tests/equivalence/null_check.py --seeds 160 --block 20
```

§7.1 is **one** draw from a distribution whose whole point is that it has a stated rate, and one draw
cannot measure a rate. This runs four of them — eight disjoint blocks of twenty runs of the fixture
pack, paired — and holds the count of pairs that reported anything against §4's α. It is
`EquivalenceHarness.TheRuleFailsAsOftenAsItSaysItDoes`, about fifteen seconds in release, and it is
the same measurement as §4.5 at a scale a test suite can pay for.

It asserts **at most one** of the four pairs may report a failure, and that bound is a trade the file
states: at most zero would fail 4% of the time for no reason, which over the ten build configurations
CI runs is a flake a third of the time, while at most one fails 0.06% of the time and still catches a
rule whose realised rate is anywhere near what the old one had. It prints the pooled p-value tail
beside the verdict, because four pairs cannot see the far tail Holm actually operates in and a reader
should not have to guess that from a pass.

It does **not** run under ThreadSanitizer, and for a different reason than the second fixture pack's
([ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md)): it scores stored reductions with a
rule that is arithmetic in Python, the engine it runs is already covered by §7.1 and §7.2, and there
is no third thing for TSan to watch.

## 8. Running it

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --build --preset release

# against the stored baseline references — no baseline binary needed
tests/equivalence/run.py --example HLM_France      --seeds 20 --use-reference
tests/equivalence/run.py --example KevinHall_FINCH --seeds 20 --use-reference

# against the baseline binary itself, rewriting the stored reference
tests/equivalence/run.py --example HLM_France --seeds 20 --refresh-reference

# the harness against itself
tests/equivalence/self_check.py --mode seeds       --example Synthetic --seeds 20
tests/equivalence/self_check.py --mode perturbation --example Synthetic --seeds 20

# does the rule fail as often as it says it does? (§7.4)
tests/equivalence/null_check.py --seeds 160 --block 20
```

And the studies, which score stored sweeps rather than running the comparison (§4.5):

```bash
# run many seeds once and keep the reductions
tests/equivalence/sweep.py --example HLM_France --seeds 400 --sides new \
    --out /tmp/hgps-sweeps/HLM_France-400

# the null calibration of §4.5, over as many disjoint pairs as the sweep has room for
tests/equivalence/calibrate.py --mode null --block 20 \
    --pairs-from /tmp/hgps-sweeps/HLM_France-400:new

# one variable, tested directly at whatever seed count the sweep has, with no correction —
# this is what answered the std_polyunsaturatedfattyacid question (docs/equivalence.md)
tests/equivalence/calibrate.py --mode series --sweep /tmp/hgps-sweeps/KevinHall_FINCH-200 \
    --variable std_polyunsaturatedfattyacid

# how far a twenty-seed standard deviation wanders from the whole sweep's
tests/equivalence/calibrate.py --mode spread --sweep /tmp/hgps-sweeps/HLM_France-400
```

`--verbose` adds the ten tests with the strongest evidence that still passed, which is how a
systematic shift sitting just inside the threshold becomes visible. `--json FILE` writes the whole
outcome — every test's raw and Holm-adjusted p-value, grouped — so a document can quote exact
numbers rather than round ones.

**`sweep.py --tolerate-failures` is for studies and never for a comparison.** A run that does not
finish is a failure and `run.py` treats it as one; a sweep of two hundred seeds is a different
thing, and losing the other 199 to one refused seed would be the wrong trade. The dropped seeds and
the reason for each are in the sweep's manifest, and the ninth run's one dropped seed is itself a
finding ([docs/equivalence.md](equivalence.md), *One seed in two hundred*).

Building the baseline is [docs/build-notes.md](build-notes.md); it is needed only for
`--refresh-reference` and for a `--sides both` sweep.
