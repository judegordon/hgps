# The equivalence method

The rules by which this implementation is judged to agree with the baseline, in one place, so that a
reviewer can check them without reading `tests/equivalence/run.py`.

[docs/equivalence.md](equivalence.md) is the companion: it reports what applying these rules
produced, example by example, and tells the story of how two of them turned out to be wrong.

Everything here is enforced in one script and tested by two others:

| | |
|---|---|
| `tests/equivalence/run.py` | the comparison. Runs both implementations, reduces, compares, reports. |
| `tests/equivalence/run_test.py` | 46 tests of the rules below, of the staging rule in ADR 0039 and of the deviation-impact measurement in ADR 0041, in milliseconds. Run by CTest. |
| `tests/equivalence/self_check.py` | the comparison pointed at itself: does it pass when it should, and fail when it should. Run by CTest. |

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

## 4. The allowance

```
allowed(statistic) = 4.5 × SE(statistic) + 10⁻⁵ × scale
```

where `scale` is the larger of the two means, or the larger standard deviation when both means are
near zero.

### 4.1 The standard error

For samples of *n* seeds with standard deviations `s_b` and `s_n`:

| Statistic | SE of the difference | Where it comes from |
| --- | --- | --- |
| mean | `sqrt((s_b² + s_n²)/n)` | textbook |
| median | `1.2533 × sqrt((s_b² + s_n²)/n)` | a quantile's SE is `sqrt(q(1−q)/n)/φ(z_q)`; at q = 0.5 that is 1.2533 σ/√n |
| 5th, 95th percentile | `2.1133 × sqrt((s_b² + s_n²)/n)` | the same formula at q = 0.05: `sqrt(0.0475/n)/0.10314` |
| standard deviation | `sqrt((s_b² + s_n²)/(2(n−1)))` | the SE of a sample standard deviation is `s/sqrt(2(n−1))` |

The standard deviation is compared as a **difference**, not a ratio. A ratio cannot be formed when
one side's standard deviation is exactly zero — and that case, a quantity that is deterministic in
one implementation and not the other, is exactly the one worth seeing rather than skipping.

### 4.2 Why 4.5 sigma

A 3σ threshold has a one-in-370 false-failure rate per comparison. Over the ~38,000 comparisons a
two-example sweep produces that is about a hundred failures from noise alone, which would make the
result unreadable. So the threshold is set for the **whole family**: a Bonferroni correction at
α = 0.05 over the ~5,000 independent series needs z = 4.4, and 4.5 is used.

This is a deliberate trade, and it has a cost worth stating: the test is insensitive to a real
difference smaller than about 4.5 standard errors **in a single series**. What protects against that
is the *pattern* — a systematic difference shows up in many series at once — and the harness prints
the largest differences that **passed** as well as the ones that failed, so a shift sitting just
inside the allowance is visible rather than silent.

### 4.3 Why there is a floor

No comparison can be tighter than the precision of the numbers compared, and **the baseline writes
its CSV with six significant digits**. Each of its band figures therefore carries a relative rounding
error of up to 4×10⁻⁶, and the difference of two such figures up to 8×10⁻⁶. The floor is 1×10⁻⁵ of
the scale.

This matters more than it sounds. Many of this model's aggregates are *nearly deterministic* —
calibration pins a band mean, and the seed moves nothing except which particular person happened to
die — so their standard-error term collapses and **the floor becomes the whole allowance**. For those
variables the test is "equal to the precision the baseline prints", which is the strongest test the
baseline's output supports. Strengthening it would mean changing the baseline's writer, and the
baseline is read-only ([ADR 0003](decisions/0003-read-only-sources-and-out-of-tree-baseline-build.md)).

## 5. Lattice-valued series, where normal theory does not apply

Every standard error above assumes the seeds are a sample from something like a normal distribution.
For a large minority of series that is plainly false. Two shapes:

- an aggregate that calibration pins: the same value in most seeds, with rare jumps;
- a count over a denominator — the incidence of a rare disease — which can only be 0, one case, two
  cases: values on a **lattice**.

In both, every **quantile** of the sample is itself a lattice point, so a quantile comparison has a
resolution of one whole lattice step. And the allowance shrinks as 1/√n while the lattice step does
not, **so such a comparison gets worse with more seeds** — the opposite of what a test should do.

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

**Both rules are applied to the numerator, not to the reduced value.** That is the correction this
run made, and it is the whole of it: neither threshold moved.

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

### 5.2 What replaces the quantiles

For a lattice-valued series:

- the **mean** is compared exactly as before. It is not a lattice point, its allowance shrinks
  correctly, and for a rare-disease series it is the summary that carries the content;
- the standard deviation and all three quantiles — every one of which is a function of the same
  counts — are replaced by **one exact test of those counts**: a two-sided Fisher exact test per
  distinct value, "this value against every other", Bonferroni-corrected for the number of values
  tested, against a family-wide α = 0.05/5000 = 10⁻⁵.

That tests the whole shape of the discrete distribution rather than three points of it.

### 5.3 How strong that test is

Blunt at twenty seeds, and the numbers are worth stating rather than assuming. Against α = 10⁻⁵, for
a two-valued series:

| | n = 20 | n = 60 |
|---|---|---|
| baseline never leaves one value; this build leaves it in *k* seeds | fails at k = 14 | fails at k = 17 |
| baseline at 0 in half its seeds; this build at 0 in *k* | never fails, even at k = n | fails at k = 54 |

So **a rare-event rate is barely testable at twenty seeds and properly testable at sixty.** That is a
reason for the 60-seed confirmation independent of the one the standard deviation gives.
`tests/equivalence/run_test.py` pins both rows.

## 6. The verdict

**There is no failure budget.** `--max-failures` defaults to zero and nothing in
`scripts/check.sh` raises it. A run also fails, regardless of any comparison, if:

- a series is reported by one implementation and not the other;
- the set of emptying bands differs from the stored reference's;
- either binary exits non-zero after its retries.

The one out-of-tolerance comparison this project has found is **reported**, in
[docs/equivalence.md](equivalence.md), with the evidence that makes it the test's expected tail
rather than a difference in the code. It is not subtracted from anything.

## 7. Does the comparison work? — the self-consistency suite

Sections 1 to 6 are rules, and rules can be wrong. Two of these have been: an earlier version of the
lattice rule bucketed at full precision and therefore hid the series it existed to catch, and an
earlier version of the emptying-band exclusion was applied to one side only. Both were found by a
twenty-minute run rather than by a test, which is the wrong way round — **a mistake in the harness
does not produce a wrong number, it produces a confident one.**

`run_test.py`'s 26 unit tests pin the arithmetic of each rule. They cannot answer either of the two
questions that matter about a comparison as a whole. `tests/equivalence/self_check.py` answers both,
using `run.py`'s own `compare` and `report` — imported, not copied, because the point is to test the
rules that decide the real result.

### 7.1 Does it pass when it should?

```bash
python3 tests/equivalence/self_check.py --mode seeds --example Synthetic --seeds 20
```

**The same build against itself at two disjoint seed sets.** Seeds 1–20 on one side, 1001–1020 on the
other. The two sides differ by nothing but sampling noise, which is precisely the null hypothesis
every allowance in section 4 is derived under. A harness whose thresholds are too tight fails here —
and a failure here is a **false positive** in the real comparison, which is the expensive kind,
because it sends somebody looking for a defect that is not there.

Measured on the synthetic fixture pack: **1,089 comparisons, zero out of tolerance.** That is what
says the thresholds of §4 are not doing the work the code should be doing.

The two samples are paired arbitrarily: seed *i* on one side against seed *i* + 1000 on the other. It
has to be arbitrary. The comparison is between two *distributions*, and a pairing that meant anything
would make it a different test.

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
measured on the synthetic fixture pack at twenty seeds, not predicted.

| Rule | What it does | Result |
|---|---|---|
| `mean_bmi=scale:1.01` | multiplies every band's mean BMI by 1.01 | **detected**, at 10⁵× its allowance |
| `mean_energy=scale:1.05` | the same at 5% | **detected**, at 10⁵× |
| `std_energy=scale:1.05` | 5% of a series that genuinely varies seed to seed | **detected**, at 2.8–4.8× |
| `emigrations=step:1` | adds one person to **one** age band | **not detected** |

**Why the first two are so far over.** Calibration pins a band mean, so `mean_bmi` and `mean_energy`
are the *same value in every seed*. Their standard error is zero and the allowance collapses to the
printed-precision floor, 10⁻⁵ of the value — so a 1% shift is five orders of magnitude out. They are
also lattice-valued by rule 5.1 (one distinct value), so what actually fires is the **exact
distribution test**, at p = 2.9×10⁻¹¹ against a threshold of 10⁻⁵. That is the lattice path working.

**Why `std_energy` is there at all.** The two above are seed-constant, so between them they exercise
only the floor and the distribution test. `std_energy` takes twenty distinct values out of twenty
seeds, so it is compared numerically, by the standard-error rule of §4.1 — the path most of a real
comparison's 31,468 comparisons take, and otherwise untested here.

**Why one person is invisible, in two steps.**

- In the **first** simulated year `emigrations` is identically zero, so a shift of one is 10⁵× its
  allowance and would fail loudly. But that year is skipped for `emigrations`, because the quantity is
  not defined until a year has passed (§3.3).
- In **every later year** the twenty seeds give eight or nine distinct values, so the series is not
  lattice-valued and the comparison is numeric. The standard deviation of the reduced emigrations
  across seeds is about 2, so the allowance is 4.5 × √(2·2²/20) ≈ 3, and the shift is **0.29 to 0.33
  of it**.

That is not a defect, it is the arithmetic: **one emigration in sixteen is not distinguishable from
sampling noise at twenty seeds**, and a test claiming otherwise would be wrong. Sixty seeds would
narrow the allowance by √3 and still not reach a shift of one.

It also exposes something subtler about the lattice rule, which is worth having written down: **the
classification is computed from the two samples pooled**, so a real shift can push a series *out* of
the lattice regime — eight distinct values on one side become sixteen pooled — and into a numeric
comparison too wide to see it. A shift is hardest to see exactly where it is large enough to change
the classification and small enough to fit inside the allowance.

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

### 7.3 Where they run

Both modes default to the **synthetic fixture pack**, whose runs take about sixty milliseconds, so
twenty seeds of both sides is a couple of seconds — `EquivalenceHarness.SelfConsistencyAcrossSeeds`
and `EquivalenceHarness.DetectsADeliberatePerturbation` are ordinary CTest tests inside
`scripts/check.sh` rather than something somebody remembers to do.

`--example HLM_France` runs the same two checks against a real example, which takes about ten minutes;
[docs/equivalence.md](equivalence.md) records what that produced. The synthetic pack is the one that
runs on every commit, and its 1,089 comparisons over 46 variables and five years are enough to
exercise every rule in §§2–5 — including, because its aggregates are calibrated and therefore
seed-constant, both the printed-precision floor and the lattice path.

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
```

`--verbose` adds the ten largest differences that passed, which is how a systematic shift sitting
inside an allowance becomes visible. `--json FILE` writes the whole outcome, so a document can quote
exact numbers rather than round ones.

Building the baseline is [docs/build-notes.md](build-notes.md); it is needed only for
`--refresh-reference`.
