# 0048 — The comparison's threshold is a stated false-positive rate, not an allowance

## Status

Accepted, 2026-09-19. Replaces the threshold rule in
[ADR 0006](0006-validation-strategy.md)'s validation strategy — the strategy itself is unchanged;
what changes is how "differ by sampling noise alone" is decided. Closes
the ninth run's [docs/backlog.md](../backlog.md) item 6 and removes the failure budget the
eighth run introduced.

## Context

Since the first run, a comparison passed when

```
|difference| <= 4.5 * SE_hat + 1e-5 * scale
```

where `SE_hat` was built from the **sample** standard deviations of the two twenty-seed samples.
The 4.5 came from a Bonferroni argument: 0.05 over "about 5,000 independent series" needs z = 4.4.

**The defect is not the 4.5. It is that the rule has no false-positive rate at all.** Two things
make that true, and they are independent:

1. **It is a *z* threshold applied to an *estimated* standard error.** A twenty-seed `s` is not
   `sigma`. Measured on this model's own output — 400 seeds of `HLM_France`, every series that
   really varies, every disjoint block of twenty, 108,920 (series, block) pairs:

   | a 20-seed `s` divided by the 400-seed one | |
   |---|---|
   | 1st percentile | **0.633** |
   | 5th percentile | 0.729 |
   | median | 0.982 |
   | 95th percentile | 1.257 |
   | 99th percentile | 1.377 |
   | worst seen | 0.000 / 1.958 |

   So `4.5 s` was somewhere between about **2.8 sigma and 8.8 sigma** depending on which twenty
   seeds were drawn. A threshold that moves over that range is not a threshold; a rule built on it
   cannot say how often it fails when nothing is wrong.

2. **The multiplicity correction was a count nobody kept.** "About 5,000 independent series" was
   true when it was written. By the eighth run one example produced 111,836 comparisons, and the
   exact test the lattice rule uses was judged against its own separate constant, `0.05/5000`,
   which never had to agree with the 4.5.

The consequence was measured before it was understood. Re-scoring one 60-seed `KevinHall_FINCH`
run over 100 random 20-seed subsets of itself — the *same* build against the *same* baseline —
gave between **0 and 45** out-of-tolerance comparisons, median 2, and 28 of the 100 subsets had
none. The worst subset's 45 were concentrated in six series: a tight sample shrank the threshold,
and a series sitting at a small persistent signed offset then failed in every year at once. The
eighth run could not tell that from a regression, so it spent a **failure budget of 3** on one
example, which is the first this project had had since the fifth run.

## The two candidates

The ruling that opened this work named two designs and asked for one, justified by measurement.

**(b) A fixed reference variance, estimated once from a 200-seed baseline-only sweep and stored
with the reference.** It fixes defect 1 directly: with `sigma` known rather than estimated, a z
threshold is the right threshold. It was rejected, and the reason is not cost alone.

- **It fixes half of the standard error.** `SE` is `sqrt((s_b^2 + s_n^2)/n)`, and only `s_b` is the
  baseline's. `s_n` is *this build's*, and this build changes — that is what the repository is for.
  Storing our own variance too would mean regenerating it on every commit that moves a number,
  which is a 200-seed sweep per example per commit; not storing it leaves the estimated quantity
  the defect is about still in the formula.
- **The machine time is per example per configuration, and it recurs.** 200 baseline seeds is
  about 40 minutes for `KevinHall_FINCH` and about 12 for `HLM_France` on the machine this was
  measured on; `HLM_India` ships two comparable configurations and runs at a hundredth of its
  cohort, and at full scale (*`HLM_India` at the cohort it ships* in
  [docs/backlog.md](../backlog.md)) a 200-seed baseline sweep is not a thing anyone will
  run. So the design has a size of example it cannot be applied to.
- **It adds a second artefact that can silently disagree with the first.** The stored reduction is
  keyed by the config hash and regenerated whenever the reduction changes — which has happened in
  three of the last four runs. A stored variance would be keyed the same way and regenerated on a
  different schedule.

**(a) A per-series test with the multiplicity controlled over the full comparison count.** Chosen.
Welch's t is the same statistic as before, referred to the distribution it actually has when the
standard error is estimated from the same twenty draws — which is precisely defect 1, expressed as
a change of reference distribution rather than of constant. Holm over every test the run emits is
defect 2, expressed as a count the code keeps rather than a number in a comment.

## Decision

**A comparison is a family of hypothesis tests with a family-wise false-positive rate of 1%,
controlled by Holm's step-down over every test the run performs.** `FAMILY_WISE_ALPHA = 0.01` in
`tests/equivalence/run.py` is the one constant, it is stated once in
[docs/equivalence-method.md](../equivalence-method.md) §4, and **it is the same number for every
example**. There is no per-example constant and no failure budget.

Per series, per (family, scenario, year, sex, variable):

| The series | What is tested | With |
|---|---|---|
| continuous | location | Welch's t on the two seed samples |
| continuous | dispersion | Welch's t on each sample's absolute deviations from its own median — the two-sample Brown–Forsythe test |
| lattice-valued or a point mass | the whole discrete distribution | the exact test that was already there: Fisher per distinct value, Bonferroni over the values tested |

and then:

- every p-value from every family goes into **one** Holm correction, so the "5,000" is now `m`,
  the number of tests the run actually made;
- a test fails when its **Holm-adjusted p-value is at most 0.01** *and* its difference is larger
  than the baseline's printed precision can express. The second clause can only remove failures,
  so 1% remains an upper bound;
- **the five statistics became two.** The mean, the standard deviation and the 5th, 50th and 95th
  percentiles were five noisy functions of one twenty-seed sample, each with its own allowance and
  each taking a slot in the family. A lattice series keeps only its exact test, because its mean
  is a function of the counts that test already covers.

**The rule must be shown to deliver its rate before it is believed, and re-shown at every commit.**
`calibrate.py --mode null` scores one build against itself on disjoint seed sets;
`null_check.py` is the same measurement at a scale CTest can pay for and is a test.

## Why Holm and not Benjamini–Hochberg

The ruling allowed either. Holm, for two reasons:

- **The series are strongly dependent.** Twenty-two years of one variable move together, and a
  stratum's series moves with the whole population's. Holm controls the family-wise rate under
  *any* dependence; Benjamini–Hochberg's guarantee needs positive regression dependence, which
  nobody here can demonstrate for this output.
- **The question this check asks is family-wise.** A false-discovery rate of 5% is the right thing
  to control when the answer is a list of discoveries to follow up. The answer here is a verdict —
  "nothing in this run is distinguishable from sampling noise" — and the budget that belongs to a
  verdict is the probability of being wrong about it once.

## Consequences

**The failure budget is gone, on every example, and so is the flag that could grant one.**
`run.py` has no `--max-failures`, `report` has no parameter that could hold one, and
`scripts/check.sh` and the CI matrix pass none. `run_test.py` pins that.

**A rare-event series is harder to fail than it was, and the numbers are written down rather than
discovered later.** The exact test used to be judged against a hand-counted `0.05/5000 = 1e-5`; it
is now judged inside the same family as everything else, which leaves about `0.01/45000 = 2.2e-7`
for the smallest p-value in a run. For a two-valued series where the baseline never leaves one
value, the number of seeds this build must leave it in before the test fires went from 14 to 17 at
n = 20, and from 17 to 22 at n = 60. That is the price of one consistent family instead of two
levels that never had to agree, and it is paid where it is cheapest: `distribution_p_value` says so
in its own docstring, and [docs/equivalence-method.md](../equivalence-method.md) §5.3 has the table.

**A twenty-seed comparison is honestly weaker than the old rule pretended.** At the level Holm
leaves, a 20-against-20 Welch test needs **t = 6.30** on its ~38 degrees of freedom where the old
rule asked for **z = 4.5** of the same estimated standard error. Nothing about the data changed;
what changed is that the gap between 4.5 and 6.30 — the noise in the estimate — is now counted
rather than ignored. A difference of a couple of standard errors is not distinguishable from noise
at twenty seeds by *any* rule with a stated rate, and the way to see one is more seeds rather than
a narrower threshold.

**`std_polyunsaturatedfattyacid` is the case that makes that concrete, and it did not survive it.**
The eighth run reported it as 1.1% below the baseline's and warned that a rule change hiding it
would be the wrong fix. Two hundred seeds of both implementations say it is **+0.135%** — the other
sign — with none of its 44 series reaching p = 0.05
([docs/equivalence.md](../equivalence.md)). So what the old rule was detecting was not a small real
difference; it was its own threshold shrinking when a seed set gave a tight sample. **That answer
was obtained before this rule was adopted and independently of it**, so that adopting the rule
could not be what decided it.

**Everything the harness could already see at twenty seeds, it still sees.** The perturbed
self-check fails in `mean_bmi` (1%), `mean_energy` (5%) and `std_energy` (5%) and in nothing else,
exactly as before, and `emigrations` shifted by one person stays below the noise — now reported as
p = 0.145 at twenty seeds rather than as a ratio against a threshold. Checked at twelve, twenty and
sixty.

**At six seeds, two of those three are no longer detected, and that is the rule being honest rather
than a regression.** ThreadSanitizer runs the self-checks at six seeds
([ADR 0046](0046-what-runs-under-which-sanitizer.md)), and the first full `check.sh` after this
change failed there. A shifted point mass — one value in every seed on each side, a different value
on each side — gives an exact p-value of exactly `2/C(2n, n)`, **whatever the size of the shift**,
because a complete separation is a complete separation. The test doubles it for the two values it
tests, giving 4.3×10⁻³ at n = 6 — which Holm over the run's ~300 tests cannot take anywhere near
α = 0.01 — against 1.5×10⁻⁶ at n = 12, which clears it by a factor of fifteen.

The old rule detected it at any seed count because it compared a point mass against the
printed-precision floor rather than testing it. That is exactly the substitution this ADR is about:
"the two constants differ by more than the baseline can print" is an *observation*, and at six seeds
it is not evidence that the two distributions differ — six against six splitting two values
perfectly happens by chance about once in 230 tries. So `self_check.py` derives its expectation from
the seed count, with the arithmetic in the file, rather than the TSan seed count being raised to
hide it: under TSan this test is there to watch the engine for races, and it still does that.
