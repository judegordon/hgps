# Summary of the second build run

What was built, what is proven, and where it stops. Written at the end of the run it describes.
The first run's summary is in the history of this file; what it covered is the HLM surface, and
what it left open was the FINCH one.

## The short version

A working, deterministic C++20 reimplementation of the Health-GPS microsimulation covering **both
in-scope model surfaces end to end**. `HLM_France` and `KevinHall_FINCH` both run all their
scenarios against the baseline and produce results that agree with it. Three of the six upstream
examples now run end to end, and a fourth loads completely and then stops on a contradiction in its
own data pack — which the baseline also stops on, in the same place.

- **554 tests, plus 26 for the equivalence harness itself**, all passing under every preset —
  release, debug, **AddressSanitizer + UndefinedBehaviorSanitizer** and **ThreadSanitizer**. The
  baseline's 471 were gone through one by one, and **the 35 the baseline skips now run**:
  [docs/test-port-map.md](test-port-map.md) says where each went.
- **Two statistical equivalence comparisons against the baseline, both at zero failures.**
  31,468 comparisons on `HLM_France` and 22,679 on `KevinHall_FINCH`, each at 20 seeds and again at
  60. With one run per intervention for each of the other five policies on each example, the whole
  sweep is **347,768 comparisons, of which one is out of tolerance** — by 1.6%, isolated (the next
  highest excursion anywhere is 0.963×), with its own mean agreeing, against an expected count of
  about 0.7 false failures at this threshold over a sweep this size. It is reported rather than
  corrected away. There is **no failure budget**: `scripts/check.sh` fails on any out-of-tolerance
  comparison, and the harness now has 26 tests of its own, because a mistake in it says PASS.
- **31 ADRs**, one per design decision, each with the alternatives rejected.
- **38 recorded deviations** from the baseline — 19 fixed defects that change the numbers, 11
  design differences that change results or output, 8 internal ones that change nothing — each with
  its audit finding ID and its evidence: [docs/deviations.md](deviations.md).
- **Four new baseline defects found**, B-21 to B-24, all of them by running code the baseline's own
  tests never reach.

## What is here

| | |
|---|---|
| `src/` | 128 files, 22,600 lines — core, diagnostics, RNG, I/O, config, data, model, sim, output, app |
| `tests/` | 47 C++ files, 11,600 lines — 554 tests in 63 suites — plus `equivalence/run_test.py`, 26 tests of the harness itself |
| `tools/` | `convert-config` (v1→v2, with `--policy-scenario`) and `gen-fixtures` (the synthetic data pack) |
| `schemas/v2/` | the published config contract, kept in step with the loader by a test |
| `docs/` | 9 documents and 31 ADRs |
| `examples/` | the six upstream examples, converted |
| `tests/equivalence/` | the harness, two stored baseline references, and the FINCH intervention set |

For comparison, the baseline is 41,400 lines of C++ for the whole model surface.

## The eleven tasks, and how each ended

| | Task | Outcome |
|---:|---|---|
| 1 | Orientation | **Done.** |
| 2 | Residual investigation — close the 54 with evidence, not a budget | **Done.** Measured, attributed to the baseline as B-21, and excluded from the reduction by a rule derived from the data. The seven that survived that turned out to be a defect in the *test*, as did eighteen more found later at 60 seeds. 54 → 0, and the budget is gone. |
| 3 | Derived-predictor resolver with load-time validation | **Done.** |
| 4 | FINCH data loading and manifest validation | **Done.** |
| 5 | `StaticLinear`, split into units, with tests | **Done.** 2,615 baseline lines become seven translation units; 22 + 11 new tests. |
| 6 | `KevinHall` and the 35 skipped baseline tests | **Done.** The 30 that test behaviour run and pass; the five that assert the contents of a printed summary box this build does not print are recorded as not ported. |
| 7 | The other five interventions, and determinism for each | **Done.** One `BandedInterventionScenario` and one virtual function per policy; 32 tests; every intervention byte-identical at one thread and at four, twice each. |
| 8 | Converter policy-scenario option, and every example converted and loaded | **Done.** `--policy-scenario S1..S7` resolves audit D-02 without editing the upstream example. All six convert and load their configs; three run end to end, and the three that do not each stop at a named cause. |
| 9 | Equivalence and performance for FINCH | **Done**, and it found more than the FINCH surface: two calibration defects here, and two defects in the comparison itself. See below. |
| 10 | Test port completion and every preset | **Done.** 554 tests and the harness's own 26, four presets. |
| 11 | Docs, ADRs, README, backlog, this file | **Done.** `scripts/check.sh` exits 0: release 8 s, debug 76 s, asan-ubsan 219 s and tsan 523 s, all at 555 tests, then both equivalence comparisons against the checked-in references. |

## What the validation actually shows

**Component level.** 554 tests, of which 229 are in files the baseline has no counterpart for. The
strongest are the ones that carry the baseline's expected numbers over unchanged and still pass:
the univariate-summary moment recurrence, the SHA-256 digests, the weight-model LMS classification,
and
`TestRelativeRiskLookup.ReferenceDataLookup`, 44 expected relative risks interpolated from a real
7×5 table.

The 35 the baseline skips are the interesting ones. `KevinHallHeight`, `KevinHallWeightQuantiles`,
`KevinHallWeightValidation` and `ModelParserFinch` have never executed in the baseline's CI, on any
machine, because the fixture path they derive from `__FILE__` does not exist in either upstream
data repository (audit B-11). Running them for the first time is how four of this run's defects
were found.

**End to end.** Two examples, two model families, six intervention scenarios, 20 seeds each and 60
for the two primary runs — 347,768 comparisons. The two primary runs are at zero out of tolerance
at both seed counts; across the whole sweep one comparison exceeds its allowance, by 1.6%, and
[docs/equivalence.md](equivalence.md) sets out why the evidence makes that the test's expected tail
rather than a difference in the code. The next-highest excursion anywhere is 0.963× of its
allowance and the great majority sit far below — which matters, because a set of comparisons
clustered at 0.99× would mean the thresholds were doing the work rather than the code.

One qualification the comparison itself uncovered, and it narrows what the intervention runs show:
on the FINCH surface **no** intervention scenario has any effect, in either implementation, because
nothing there calls `Scenario::apply`. So the five policies' own rules are compared against the
baseline on `HLM_France`, and on `KevinHall_FINCH` what is compared is that both implementations
agree they do nothing. Finding (2) below has the measurement.

**Determinism.** Byte-identical output across repeats, across thread counts, and for **each of the
six interventions** — asserted by `tests/sim/reproducibility_test.cpp`, not just claimed.

**Memory and threading.** The whole suite passes under AddressSanitizer + UndefinedBehaviorSanitizer
(219 s) and under ThreadSanitizer (523 s), which is where audit finding B-02 — a data race in the
baseline's lazily-populated repository — was confirmed in the first place. The six interventions'
byte-identical-at-1-and-4-threads checks are about 85 s each under ThreadSanitizer, which is why
they are six tests rather than the one that exceeded CTest's 300 s timeout.

Across every run this project has made — several hundred, over two examples, six interventions and
both seed counts — this build has not once exited on a signal. The baseline has, on
`KevinHall_FINCH`, in 4 of 180 measured runs: three `SIGTRAP` and one `SIGABRT`, each succeeding
when re-run unchanged with the same binary, config and seed.

**Performance.** [docs/performance.md](performance.md):

| | Baseline | This build |
|---|---|---|
| `HLM_France`, 2010–2050 | 4.78–5.26 s, 85.2 MiB | **2.78–3.23 s, 56.8 MiB** |
| `KevinHall_FINCH`, 2022–2032 | 15.19–15.52 s, 198 MiB | **11.63–11.79 s, 195.6 MiB** |

The budget for this run was the previous one's `HLM_France` figure plus 10%, and it holds: adding
the whole FINCH surface cost an example that uses none of it nothing measurable.

## What the process found that reading would not have

1. **Two calibration targets were aimed at the wrong number**, and the equivalence harness found
   both. Physical activity's target was being clamped to the factor's configured range — but the
   FINCH table legitimately puts a newborn at 1.2 against a configured lower bound of 1.4, so the
   band came out a tenth of a unit high and 3.5% wide. And weight was being calibrated onto the
   Kevin Hall model's derived adult-weight regression, which is a *fit to* the FactorsMean table's
   own `Weight` column — 86.1912 against the table's 86.1864. Both are a fraction of a percent, in
   the right direction, on a quantity that looks calibrated either way. Neither is visible in any
   unit test and neither would have been found by reading the code.

2. **An intervention scenario does nothing at all on the FINCH model surface**, in either
   implementation. `Scenario::apply` — the call that offers a person and a risk factor to the
   active policy — has exactly one call site in the whole baseline,
   `dynamic_hierarchical_linear_model.cpp:110`; neither `static_linear_model.cpp` nor
   `kevin_hall_model.cpp` calls it. So running `KevinHall_FINCH` with `marketing` active gives
   output byte-identical to running it with `simple` active, which is what the measurement shows.
   It explains why the example ships `simple` with an **empty impact list** — filling it would
   change nothing — and it means a config can select `food_labelling` on a Kevin Hall model today
   and get a run with no error and no effect. FINCH's policy is elsewhere: `policy_start_year:
   2024`, from which the static linear model applies the S1 policy-effect coefficients and residual
   policy covariance, and that mechanism *does* work — the baseline's two scenarios are identical
   in 2022 and 2023 and differ in 2,476 of 4,600 reduced series in 2024.

   This one was found by checking that the comparison was testing what it claimed to. It would have
   passed, silently, either way.

3. **A quantile of a lattice-valued series cannot be compared numerically, and more seeds made that
   worse rather than better.** The 60-seed FINCH confirmation failed where the 20-seed run passed —
   18 comparisons, every one a median of a rare cancer. A count over a denominator lives on a
   lattice, so its median is a lattice point; the allowance shrinks as 1/√n while the lattice step
   does not. At 20 seeds the allowance was larger than one step and it passed; at 60 it is smaller
   and it cannot. The distributions themselves were indistinguishable — Fisher's exact test gives
   p = 0.27 on the worst of them. A test that gets worse with more evidence is not measuring what
   it claims, and [docs/equivalence.md](equivalence.md) has what replaced it.

4. **The FINCH static model names two files the pack does not contain.** It ships seven variants of
   each, `S1_` to `S7_`, one per modelled policy scenario, and the example as shipped fails at load
   in the baseline. That is audit D-02, and the fix is a converter option rather than an edit to
   somebody else's example ([ADR 0030](decisions/0030-policy-scenario-selection-for-the-broken-finch-example.md)).

5. **`std_income` is a column the baseline emits and never fills** (B-22). Two loops each skip
   `income` on the ground that the other one handles it. Every value is exactly zero, in every
   band, every year, every run.

6. **`two_stage.use_logistic` is read and never consulted** (B-23). The FINCH pack sets it `false`,
   ships the logistic file anyway, and is fitted to the behaviour with the first step *on* — the
   first step changes `mean_redmeat` by 30%. So the file has to decide, and the disagreement is
   reported rather than resolved silently.

7. **The food-labelling policy can apply its impact more than once** (B-24). It marks somebody it
   has just affected with `try_emplace`, which does nothing when they are already in its book as
   unaffected, so a person who failed an early coverage draw and passed a later one is offered the
   impact again every remaining year of the window.

8. **A background indexer is worth 20% of the wall time.** The first attempt at this run's
   performance figures put `HLM_France` at 3.38 s — over budget — while Spotlight was indexing the
   working directories. It is in [docs/performance.md](performance.md) with the cause, because the
   next person to measure this will hit it too.

## What a reader should still be sceptical about

- **One country per model family.** The evidence is `HLM_France` for the HLM surface and
  `KevinHall_FINCH` for the FINCH one. `HLM_India` runs here — 42 minutes for its 1.2 million
  people — but is not compared against the baseline, which was this run's scope ruling, and the
  baseline has not been run on it at all. `KevinHall_India` — the obvious second FINCH-surface
  country — **cannot be compared at all**: both implementations stop in its first simulated year,
  because its configured lower bound on `Weight` is above what its own weight quantile curve
  produces for the lightest newborns. [docs/examples.md](examples.md) has both implementations' messages side by
  side. So the FINCH evidence is one data pack, and making it two needs a pack that works.
- **The comparison's floor.** The baseline writes six significant digits, so no comparison can be
  tighter than about 10⁻⁵ relative. Several of these models' aggregates are nearly deterministic,
  so for those the test *is* that floor.
- **The five age-banded policies are compared on one example, not two.** They are compared against
  the baseline on `HLM_France`, where each produces a genuinely different future — about 2,100 of
  3,854 reduced series move — and all five agree. On `KevinHall_FINCH` they cannot be compared at
  all, because nothing on that surface consults them (finding 2), so what those five runs show is
  only that both implementations agree they do nothing.
- **The harness is no longer a simple thing, and it decides the headline result.** It now carries
  an emptying-band exclusion, a printed-precision floor, a lattice rule and an exact test, and two
  of those rules were wrong at some point in this run. They have 26 tests now, which is better than
  nothing and is not the same as being right.
- **macOS only, so far.** The code targets Linux and macOS and avoids what would break on either,
  but every measurement here is from one Apple M5 and there is no CI. That is now the first item in
  the backlog.
- **The synthetic fixture pack is invented.** It exists so tests can run without the network. Its
  own `SYNTHETIC.md` says so, and records the one artefact it has.
- **Population impact fraction is still refused.** It is the one part of the upstream model surface
  that is rejected at load rather than implemented, with a named error and a pointer to the
  backlog.

## The next three things to do

1. **A CI workflow.** `scripts/check.sh` is the whole of it; what is missing is a runner and a
   decision about whether CI fetches the disease data or runs against the synthetic pack. Two
   equivalence references are checked in and nothing runs them automatically, and a harness nobody
   runs is a document.
2. **Population impact fraction**, the last refused feature, and with it `KevinHall_PIF`.
3. **Resolve factor and channel names to indices once per run.** Both profiles say the same thing —
   40% of `HLM_France`'s samples and 52% of `KevinHall_FINCH`'s are string comparison — and the same
   structure is where FINCH's 195 MiB goes.

[docs/backlog.md](backlog.md) has the rest, ranked, with what each costs.
