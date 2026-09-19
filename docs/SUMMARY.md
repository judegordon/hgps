# Summary of the ninth build run

What was built, what is proven, and where it stops. Written at the end of the run it describes.
Earlier runs' summaries are in the history of this file: the first covered the HLM surface, the
second the FINCH one, the third the library split and the index-keyed store, the fourth `HLM_India`
and the first CI workflow, the fifth the local server, the frontend and switchable deviations, the
sixth a second fixture pack, a browser in CI, and nine findings, the seventh the weight categories,
the ThreadSanitizer split and the analysis module's channels, the eighth the 45 empty columns and
the comparison over every output family. A comment in the code that cites "`docs/SUMMARY.md`,
finding N" means the run that wrote the comment; `git log -p docs/SUMMARY.md` is where to find it.

## The short version

A deterministic C++20 reimplementation of the Health-GPS microsimulation, with the whole upstream
model surface implemented, three examples compared against the baseline, and three ways to use it:
as a library, from a command line, and from a browser.

**This run changed what the word "equivalent" is worth.** The previous run found that the
comparison's threshold was `4.5 ×` an *estimated* standard error and that the same build against
the same baseline therefore produced anywhere between 0 and 45 failures depending on which twenty
seeds were drawn. It spent a failure budget of 3 rather than pretend otherwise. This run replaced
the threshold with a **family-wise false-positive rate of 1%**, measured it on a null before
adopting it, and removed the budget from every example.

> **A threshold is not a rate, and the difference is the whole run.** `4.5 s` asks how many
> estimated standard errors a difference is, and then treats the estimate as if it were the truth.
> Measured here for the first time, over 400 seeds: a twenty-seed standard deviation is between
> **0.63 and 1.38** of the whole sweep's at the 1st and 99th percentiles, so `4.5 s` was anywhere
> between about **2.8 and 8.8 true sigma**. Welch's t is the same statistic referred to the
> distribution it actually has; Holm over every test a run performs is the multiplicity count the
> code keeps rather than a number in a comment. Together they make the rate something chosen.

| | |
|---|---:|
| Tests, C++ | **863** in 102 suites — **867** CTest entries — passing under release, debug, ASan+UBSan and TSan, where **773** of them run ([ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md)) |
| Tests, the equivalence harness's own | **94** (was 63) |
| Tests, the frontend | **48** unit, **21** end to end in a browser |
| Comparisons against the baseline this run | **124,766** tests over four stored references, **0** failures, **no budget on any example** — plus **922,564** null tests calibrating the rule and a **200-seed** study of both implementations |
| Source | `src/` 153 files; `tests/` 73 files; 46,713 lines of C++ between them; the equivalence harness 4,226 lines of Python in six files; `web/src/` 18 TypeScript files and `web/e2e/` 6, 3,136 lines |
| Documents | **14**, plus **48 ADRs** |
| CI | **16 jobs** — see below |
| Findings this run | **5** |

## The seven tasks, and how each ended

| | Task | Outcome |
|---:|---|---|
| 1 | Orientation, pre-flight, CI on HEAD | **16 of 16 green** on `65e9a22`, run 35423786489, read per job with `gh`. |
| 2 | The 200-seed runs, and the candidate rule | **Done.** `sweep.py` (run many seeds once, store the reductions) and `calibrate.py` (score them many ways) are the new shape: a study never re-runs a sweep. 200 seeds of both implementations on `KevinHall_FINCH`, 400 of this build on `HLM_France` and on `HLM_India`. **One finding, and it stopped the first sweep dead.** |
| 3 | Is the `std_polyunsaturatedfattyacid` offset real? | **No**, and the numbers are not close: **+0.135%** at 199 seeds where twenty seeds said −1.1%, the other sign, with **0 of its 44 series** below p = 0.05. Answered before the rule was adopted, so the rule could not be what decided it. **One finding.** |
| 4 | Null calibration on all three examples | **30 pairs of twenty seeds, 922,564 tests, 0 failures against 0.30 expected**, with the raw p-value tail below uniform at every threshold on every example. No iteration was needed: the rule was conservative on the first measurement and is conservative for a reason that can be stated. |
| 5 | Detection against the perturbation set | **All three previously-detected cases still detected**, at twelve, twenty and sixty seeds. **At six they are not, and that is a finding** rather than a regression — found by the ThreadSanitizer preset on the first full `check.sh` after the change. |
| 6 | Adopt: harness, references, `check.sh`, CI | **Done**, with **zero budget on every example**. All four stored references re-scored under the new rule; none fails. `--max-failures` is gone from the harness entirely. |
| 7 | Docs | [equivalence-method.md](equivalence-method.md) §4 rewritten around the rule, with the calibration tables; [equivalence.md](equivalence.md) with the 200-seed answer and the null tables; [ADR 0048](decisions/0048-a-comparison-with-a-stated-false-positive-rate.md); backlog re-ranked with a new correctness item at 2. |

## The five findings, and what found each

| | What | Found by |
|---|---|---|
| 1 | **The comparison's threshold had no false-positive rate, and how much it wandered is now measured rather than argued.** A twenty-seed standard deviation is 0.63 to 1.38 of the 400-seed one at the 1st and 99th percentiles, and as far as 1.96 and 4.47 at the extremes — so `4.5 s` was between about 2.8 and 8.8 true sigma depending on the draw | running 400 seeds of two examples and scoring every disjoint block of twenty against the whole sweep |
| 2 | **The 1.1% `std_polyunsaturatedfattyacid` offset is not real.** At 199 seeds of both implementations it is **+0.135%**, not −1.1%; the largest of its 44 series is 1.95 standard errors and none reaches p = 0.05. `std_fat` is +0.070% with nothing surviving its own Bonferroni | two hundred seeds, because the only way to answer a question about a difference this small is more seeds |
| 3 | **This build refuses `KevinHall_FINCH` at seed 80 and the baseline does not.** The energy balance diverges for one 24-year-old man in simulated year 2031, to −1.7×10²⁸³ kg, deterministically, with no compatibility flag involved — and with a **three-year precursor** visible in the band means, so a run that stopped in 2030 would have written a contaminated number and exited zero | the 200-seed sweep, which killed itself on seed 80 the first time it was run |
| 4 | **At six seeds the perturbed self-check can no longer see a shifted point mass, and the old rule only appeared to.** A point mass shifted by any amount gives an exact p-value of `2/C(2n, n)`, doubled for the two values tested — 4.3×10⁻³ at n = 6, which Holm over the run's tests cannot take near α. The old rule detected it at any seed count because it compared a point mass against the printed-precision *floor* rather than testing it | the ThreadSanitizer preset, which is the only configuration that runs the self-checks at six seeds |
| 5 | **The report and the stored JSON picked the "worst" comparison with a key that saturates.** Holm-adjusted p is exactly 1 for almost every test in a clean run, so ordering on it alone left thousands of ties and reported whichever one happened to be built first — which is how `std_polyunsaturatedfattyacid` appeared to have a worst-case p of 0.649 in a run where it did not. A defect in code this run wrote, in the first output it produced | reading a report rather than running a test, which is the uncomfortable half |

## What the rule is now

The rules themselves are [docs/equivalence-method.md](equivalence-method.md) §4, written so that a
reviewer can check them without reading the script. In brief:

```
alpha = 0.01, family-wise, per example, over every test the run performs
```

Per series, per (family, scenario, year, sex, variable), on the two samples of twenty seeds:

| The series | What is tested | With |
|---|---|---|
| continuous | location | Welch's t on the two samples |
| continuous | dispersion | Welch's t on each sample's absolute deviations from its own median — the two-sample Brown–Forsythe test |
| lattice-valued or a point mass | the whole discrete distribution | the exact test that was already there |

Every p-value goes into **one** Holm correction. A test fails when its adjusted p-value is at most
0.01 *and* its difference is larger than the baseline's six printed digits can express — the second
clause can only remove failures, so the 1% stays an upper bound.

**Five statistics became two.** The mean, the standard deviation and the 5th, 50th and 95th
percentiles were five noisy functions of one twenty-seed sample, each with its own allowance and
each taking a slot in the family. A lattice series keeps only its exact test, because its mean is a
function of the counts that test already covers. `KevinHall_FINCH` went from 111,836 comparisons to
**45,544 tests**, and from three out of tolerance to **none**.

**What it costs, stated rather than buried.** A rule with a stated rate is weaker than one whose
threshold is partly luck, and the two places it is weaker are written down: a 20-against-20
location test needs **t = 6.30** where the old rule asked for **z = 4.5** of the same estimated
standard error, and a rare-event series on the exact path needs **17 of 20** seeds to move rather
than 14. Neither is a loss of information. The first is exactly the estimation noise the old rule
ignored; the second is the price of one multiplicity family instead of two levels — `4.5 sigma` and
`0.05/5000` — that never had to agree.

## The calibration, which is the part that makes the rate a fact

**Every failure in this table is a false positive by construction**: one build against itself, on
disjoint seed sets of twenty, same config, nothing perturbed, only the seeds differ.

| Example | Pairs of 20 | Tests the floor does not waive | Runs with ≥1 failure | Expected at α = 0.01 |
|---|---:|---:|---:|---:|
| `HLM_France` | 10 | 163,505 | **0** | 0.10 |
| `HLM_India` *(reduced cohort)* | 10 | 311,370 | **0** | 0.10 |
| `KevinHall_FINCH` | 10 | 447,689 | **0** | 0.10 |
| **total** | **30** | **922,564** | **0** | **0.30** |

**Thirty runs cannot see a rate of 1% with any precision, and that table is not where the
confidence comes from.** A run's verdict turns only on whether any raw p-value falls below about
`alpha/m`, so the honest check is the whole tail, pooled — observed of expected:

| p below | `HLM_France` | `HLM_India` | `KevinHall_FINCH` | all three |
|---|---:|---:|---:|---:|
| 10⁻² | 965 of 1,635 | 1,829 of 3,114 | 2,958 of 4,477 | **5,752 of 9,226** |
| 10⁻³ | 42 of 164 | 167 of 311 | 206 of 448 | **415 of 923** |
| 10⁻⁴ | 1 of 16.4 | 16 of 31.1 | 17 of 44.8 | **34 of 92.3** |
| 10⁻⁵ | 0 of 1.64 | 2 of 3.11 | 1 of 4.48 | **3 of 9.23** |
| 10⁻⁶ | 0 of 0.16 | 0 of 0.31 | 0 of 0.45 | **0 of 0.92** |
| 10⁻⁷ | 0 of 0.02 | 0 of 0.03 | 0 of 0.04 | **0 of 0.09** |

Observed is **below** expected at every threshold on every example, so the rule is conservative and
the 1% is an upper bound. That is the safe direction, and the reason is not mysterious: a large
minority of these series are pinned by calibration or are a handful of events, and both rules that
handle those — the exact test and the printed-precision floor — are conservative by construction.
Anti-conservative behaviour in that tail is what would have stopped the rule being adopted.

**And the measurement is a test rather than a document.** `tests/equivalence/null_check.py` runs
four null comparisons of the synthetic fixture pack in about fifteen seconds at every commit, and
asserts at most one reports anything. The bound is one rather than zero, and the file says why: at
most zero would fail 4% of the time for no reason, which over the ten build configurations CI runs
is a flake a third of the time.

## The 200-seed answer

The previous run flagged `std_polyunsaturatedfattyacid` as **about 1.1% below** the baseline's and
warned that a rule change hiding it would be the wrong fix. So the question was answered first,
with two hundred seeds of both implementations, and independently of the rule:

| | Series | Mean difference | Range | Largest, in standard errors | Below p = 0.05 |
|---|---:|---:|---|---:|---:|
| `std_polyunsaturatedfattyacid` | 44 | **+0.135%** | −0.227% to +0.441% | **1.95** | **0 of 44** |
| `std_fat` | 44 | **+0.070%** | −0.177% to +0.438% | 2.59 | 5 of 44 |
| `mean_polyunsaturatedfattyacid` | 38 | +0.004% median | largest −0.074% | | 4 of 38 |
| `mean_fat` | 31 | −0.001% median | largest −0.039% | | 3 of 31 |

**The sign is wrong, the size is wrong, and nothing is significant.** There is no mechanism to find
in the two-stage factor model, nothing to fix and nothing to flag under
[ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md). What there was is a rule that
could turn a 1.5-standard-error difference into a failure in every year at once whenever a seed set
gave a tight sample.

The honest reading of how close this came to being a finding: **at twenty seeds a
1.5-standard-error difference is not detectable by any rule with a stated rate.** The old rule
appeared to detect it because its threshold was partly luck. The way to answer a question like this
is more seeds, and it cost ninety-five minutes.

## One seed in two hundred, which is the run's uncomfortable finding

Running `KevinHall_FINCH` two hundred times — which neither implementation had been — found a seed
this build **refuses** and the baseline completes:

```
person 1222 (male, age 24) weighs -1.702e+283 kg after the energy balance, below the
configured minimum of 1 kg for 'Weight'
```

Deterministic and reproducible; **not** a compatibility flag, since `--baseline-compat none` fails
identically; in simulated year **2031**, the tenth of the horizon, with stopping at 2030 completing
cleanly. And there is a **three-year precursor**: the largest band mean weight is flat at 87.28 kg
through 2027 and then 87.32, **92.1**, **98.5** in 2028–2030. A shorter run would have written
those and exited zero.

The baseline finished all two hundred of its own seeds, and over the 199 both sides completed the
largest band mean weight is 123.3 kg there against 123.5 kg here. **But the two draw different
random streams, so seed 80 is not the same cohort on both sides**, and this is not proof the
instability is ours rather than the model's. It is one in two hundred here, none in two hundred
there, and unexplained. It is [docs/backlog.md](backlog.md) item 2 — the only correctness item on
that list — and it is in [docs/briefing.md](briefing.md), because if the coefficients admit a
runaway it matters upstream too, where nothing stops it being written out.

`sweep.py --tolerate-failures` drops such a seed from both sides and records it, because a
two-hundred-seed study losing its other 199 runs to one seed would be the wrong trade. **A
comparison never does this**: a run that does not finish is a failure and `run.py` treats it as
one.

## The separation that made all of this affordable

`run.py` runs both implementations and compares them in one process, which is right for a check and
wrong for a study: answering "does the rule deliver its rate?" means scoring the *same* seeds many
different ways, and re-running a two-hundred-seed sweep for each way is hours of machine time for
nothing. So a sweep is now separate from a scoring:

| | |
|---|---|
| `sweep.py` | many seeds of one example, run once, reduced once, stored in the reference format `run.py` already reads |
| `calibrate.py` | the studies: `--mode null` (the calibration), `--mode series` (one variable tested directly), `--mode spread` (how far a twenty-seed standard deviation wanders) |
| `null_check.py` | the calibration at a scale CTest can pay for, as a test |

Everything in them comes from `run.py` — imported, not copied — so a sweep goes through exactly the
code path the real comparison does, including the emptying-band exclusion taken as the union over
the whole sweep. The three null calibrations, the 200-seed answer, the spread measurement and every
re-score in this document came out of four sweeps and cost no extra simulation.

## CI, per matrix entry

Sixteen jobs. Run **35435361005** on `3bc34d9`, every entry read with `gh run view` rather than from
the run's own summary. **16 of 16 success.** The last column is the same job on the previous run's
final commit, run 35423786489.

| Job | Result | Time | The previous run |
|---|---|---:|---:|
| `linux · clang · release` | **success** | 3m33s | 5m52s |
| `linux · clang · debug` | **success** | 19m54s | 16m18s |
| `linux · clang · asan-ubsan` | **success** | **53m31s** | 42m00s |
| `linux · clang · tsan` | **success** | 12m01s | 13m12s |
| `linux · gcc · release` | **success** | 5m11s | 3m49s |
| `linux · gcc · debug` | **success** | 17m37s | 14m29s |
| `macos · appleclang · release` | **success** | 4m54s | 3m14s |
| `macos · appleclang · debug` | **success** | 13m24s | 10m48s |
| `macos · appleclang · asan-ubsan` | **success** | **42m39s** | 29m21s |
| `macos · appleclang · tsan` | **success** | 33m03s | 28m54s |
| `equivalence · HLM_France · 20 seeds` | **success** | 5m30s | 6m02s |
| `equivalence · KevinHall_FINCH · 20 seeds` | **success** | 9m06s | 5m48s |
| `column coverage · three examples` | **success** | 5m34s | 5m25s |
| `web · typecheck, test, build` | **success** | 0m12s | 0m18s |
| `web · end-to-end` | **success** | 2m14s | 3m09s |
| `performance · linux · indicative` | **success** | 3m19s | 5m30s |

**The two bold rows are this run's cost and they are worth naming.** The AddressSanitizer jobs are
11 and 13 minutes longer, and that is `EquivalenceHarness.TheRuleFailsAsOftenAsItSaysItDoes`: 160
runs of the synthetic fixture pack under ASan, so that the rule's false-positive rate is measured at
every commit rather than in a document. It is the most expensive single test in the suite, it buys
the one claim this run rests on, and it is skipped under ThreadSanitizer — whose two jobs moved by
runner weather rather than by anything here — because it scores stored reductions with a rule that
is arithmetic in Python and there is no third thing for TSan to watch.

`equivalence · KevinHall_FINCH` is 3 minutes longer for a reason that is not the rule: the job runs
this build twenty times and then scores it, and the scoring is now 45,544 Welch and Fisher tests in
Python where it was 111,836 threshold comparisons. Fewer tests, more arithmetic each. `HLM_France`
is *shorter* on the same change, so most of that 3 minutes is runner weather too.

This table is the run on `3bc34d9`, which carries every change this run made and every document but
this table. The push that updates it starts one more, on the same code and the same matrix — a fixed
point a summary of its own run cannot reach, so what is quoted is the newest run that had reported
when it was written.

## The same tree locally

`scripts/check.sh` with nothing skipped, on the tree these commits leave behind — **exit 0**:

| | Result | Time |
|---|---|---:|
| release | **867 / 867** | 48 s |
| debug | **867 / 867** | 623 s |
| asan-ubsan | **867 / 867** | 1,441 s |
| tsan | **773 / 773** | 1,210 s |
| frontend, type-check and unit | **48 / 48** | under a second |
| frontend, end to end in a browser | **21 / 21** | 15.6 s |
| equivalence, `HLM_France` | **16,486** tests over four families, **0** failed | — |
| equivalence, `KevinHall_FINCH` | **45,544** tests over five families, **0** failed, **no budget** | — |
| column coverage, three examples | **13 families** compared column by column, **0** findings | — |

The four presets' test phases alone are **55 minutes**, against 35 for the previous run. About
seven of the twenty added minutes are the new
`EquivalenceHarness.TheRuleFailsAsOftenAsItSaysItDoes` under AddressSanitizer, where 160 runs of
the fixture pack cost what 160 runs of the fixture pack cost; the rest is that this machine was
also running a two-hundred-seed sweep for much of it, so these are not comparable timings and
should not be read as a slowdown. The test does not run under ThreadSanitizer, whose count is
unchanged at 773 — it scores stored reductions with a rule that is arithmetic in Python, the engine
it runs is already covered by the two self-checks beside it, and there is no third thing for TSan
to watch.

The two examples' figures are what the rule change did to the counts. `HLM_France` went from 38,386
comparisons to 16,486 tests and `KevinHall_FINCH` from 111,836 to 45,544, because five statistics
per series became two and a lattice series stopped being compared twice. **Nothing was dropped**:
the series compared are the same series, and the two examples' four and five output families are
the same families.

## The recommended next run

**Find out whose the seed-80 divergence is** — [docs/backlog.md](backlog.md) item 2. It is first
because it is the only correctness item on the list, because it is the only thing in this project
that is both a run that does not finish and not understood, and because it is cheap to start: the
divergence is one person over four simulated years, so instrumenting the energy balance for that
person and that seed would show whether the intake, the expenditure or the integration runs away.
If it is the integration it is ours and fixable. If it is a coefficient combination the model
admits, it belongs upstream beside the other four reports, and the honest fix here is to clamp and
count rather than to refuse — `validate_weight` already treats *above* the configured maximum that
way, so the shape exists.

It also has a second half nobody can do from one event: **one in two hundred is a rate estimated
from a single observation**, and the interval around it runs from about one in forty to one in two
thousand. Another two hundred seeds would narrow it, and they now cost ninety-five minutes and one
command.

**If a modelling answer arrives before then, item 1 still outranks it**: interventions on the Kevin
Hall surface are a question for the upstream authors ([docs/briefing.md](briefing.md)), four of the
six examples can only be run with a no-op policy until it is answered, and it has been first on
this list for four runs because it is work nobody here can do.

**Item 3 is the cheapest real gain**: individual-level tracking output is the one output family the
baseline writes and this build does not, and it is carried by two explicit exclusions whose premise
is that the baseline's own file is empty. And **`HLM_India` at the cohort it ships** (item 5) is
still the largest single gap in the validation — machine time rather than work, and now also the
place where item 2 would next show up if the divergence is a property of a cohort rather than of
one pack.

**One thing this run deliberately did not do.** The rule is *conservative* — the observed
false-positive rate is below the 1% it promises at every threshold measured — and a conservative
rule is a less sensitive one. Tightening it would mean finding out where the conservatism comes
from, which is the exact test and the printed-precision floor, and neither can be tightened without
the baseline printing more digits. That is worth writing down as a limit rather than as an item: it
is the floor the method has, not a thing left undone.

## What a reader should still be sceptical about

- **The seed-80 divergence is unexplained**, and it is the only thing in this project that is both a
  run that does not finish and not understood. One in two hundred is also a rate estimated from one
  event: the true rate could be anywhere between about 1 in 40 and 1 in 2,000.
- **The null calibration is 30 draws of a 1% rate.** It cannot distinguish 1% from 0.1%, and it is
  not meant to — the tail table is the measurement and the 30-run count is the headline. Both say
  the rule is conservative; neither says by how much, and a conservative rule is a less sensitive
  one.
- **The dispersion test is new and has never found anything.** It is there because two
  implementations can agree about a mean and disagree about how much it moves from seed to seed, and
  nothing else would see that. It passed 30 null calibrations and it passes on all four references;
  that is evidence it is not noisy, not evidence it is useful.
- **India was compared at a hundredth of its cohort**, 12,406 people rather than 1,240,613. Nothing
  in the India result is evidence about the example as shipped ([docs/backlog.md](backlog.md) item
  5) — including its null calibration, which is a calibration on that cohort.
- **The two HLM examples' stratum files are empty on both sides.** `KevinHall_FINCH` is the only
  example that checks the 45 income-stratified columns, and it is one country and one pack.
- **Population impact fraction has never met the baseline.** Only the synthetic pack exercises it
  end to end; the one example that uses it cannot run.
- **Four of the six upstream examples can only be compared with `simple` active**, because an
  intervention on the Kevin Hall surface is a no-op upstream (B-25) and this build refuses the
  configuration rather than running it silently.
- **The comparison's floor.** The baseline writes six significant digits, so no comparison is
  tighter than about 10⁻⁵ relative.
- **The harness has been wrong six times**, and the sixth was this run's own new code — the
  saturating sort key in finding 5, which was wrong in the first report it printed. It has 94 tests,
  which is better than 63 and is not the same as being right. What this run added to it is a rule
  one run old with thirty-one tests over it and one null calibration behind it.
- **A rule with a stated rate is a weaker rule, and the weakening is real.** t = 6.30 rather than
  z = 4.5 on a twenty-seed location test, and 17 of 20 rather than 14 of 20 on the exact path.
  Everything the perturbed self-check detected at twenty seeds it still detects; at six, two of the
  three are now out of reach.
- **Nineteen end-to-end tests was not coverage and twenty-one is not either.** They cover each
  screen's principal job and the hand-offs between them, in one browser.
