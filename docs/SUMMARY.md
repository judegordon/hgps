# Summary of the first build run

What was built, what is proven, and where it stops. Written at the end of the run it describes.

## The short version

A working, deterministic C++20 reimplementation of the Health-GPS microsimulation covering the
**HLM surface end to end**: the converted `HLM_France` reference example runs 2010–2050 over both
scenarios and produces results that agree with the baseline's, and it does so in **2.7 s against
the baseline's 5.0 s** and **57 MiB against 85 MiB**, with byte-identical output on every repeat
and at any thread count.

- **433 tests**, all passing under every preset — release, debug, **AddressSanitizer +
  UndefinedBehaviorSanitizer** and **ThreadSanitizer**. The baseline's 471 were gone through one by
  one: [docs/test-port-map.md](test-port-map.md) says where each went.
- **38,260 statistical comparisons** against the baseline, run at 20 seeds and again at 60, of
  which **every single mean agrees** — 0 of 7,652, at both seed counts — and 99.86% of all
  comparisons fall inside a multiplicity-corrected 4.5σ allowance:
  [docs/equivalence.md](equivalence.md).
- **26 ADRs**, one per design decision, each with the alternatives rejected.
- **33 recorded deviations** from the baseline — 15 fixed defects that change the numbers, 10
  design differences that change results or output, 8 internal ones that change nothing — each with
  its audit finding ID and its evidence: [docs/deviations.md](deviations.md).

The **FINCH surface was not completed**. See *Where step 7 stopped*, below.

## What is here

| | |
|---|---|
| `src/` | 114 files, 17,200 lines — core, diagnostics, RNG, I/O, config, data, model, sim, output, app |
| `tests/` | 43 files, 9,600 lines — 433 tests in 51 suites |
| `tools/` | `convert-config` (v1→v2) and `gen-fixtures` (the synthetic data pack) |
| `schemas/v2/` | the published config contract, kept in step with the loader by a test |
| `docs/` | 8 documents and 26 ADRs |
| `examples/` | the six upstream examples, converted |
| `tests/equivalence/` | the harness and the baseline's stored reference output |

For comparison, the baseline is 41,400 lines of C++ for the whole model surface, of which this run
implements the HLM part.

## The ten tasks, and how each ended

| | Task | Outcome |
|---:|---|---|
| 1 | Build the baseline and run its tests | **Done.** `471 tests, 436 passed, 35 skipped`, matching the expectation exactly. Four macOS shims were needed and each is recorded in [docs/build-notes.md](build-notes.md) with a judgement on whether it is a baseline defect. |
| 2 | Scaffold | **Done.** CMake + presets (release, debug, asan-ubsan, tsan), vcpkg pinned to a builtin baseline, three dependencies, `-Wall -Wextra -Wpedantic -Werror` with every warning fixed rather than suppressed. |
| 3 | Design and ADRs | **Done.** [docs/design.md](design.md) and 26 ADRs, one per ruling and per design choice. |
| 4 | Tests first | **Done.** Ported before the code they test, including the expectations changed because a baseline test encoded a finding. |
| 5 | Foundations | **Done.** The determinism contract's 14 clauses are enforced by types, not by review: an unseeded engine does not compile, an RNG draw inside a parallel region throws with a source location, `Categorical<T>` has its `unordered_map` constructors deleted, and `reduce_ordered`'s block decomposition is independent of the thread count. |
| 6 | Converter, fixtures and examples | **Done.** All six upstream examples converted; [docs/examples.md](examples.md) records that one runs end to end and why each of the others stops. |
| 7 | Model components | **Partly done — this is where the run stopped.** See below. |
| 8 | Equivalence harness | **Done** for the surface that exists. [docs/equivalence.md](equivalence.md). |
| 9 | Profiling | **Done.** [docs/performance.md](performance.md) — and it changed the code three times. |
| 10 | Backlog, summary, README | **Done.** [docs/backlog.md](backlog.md). |

## Where step 7 stopped

**Implemented:** the demographic module (births, deaths, ageing, net migration, residual
mortality), the SES module, the disease module (incidence, remission, mortality, relative risks,
comorbidity), the analysis module (five units: burden, channels, series, income strata, module),
the `HLM` static and `EBHLM` dynamic risk-factor models, the `simple` intervention, the scenario
journal, the engine and runner, and the result writer.

**Not implemented:** the `StaticLinear` and `KevinHall` model families, which are the FINCH
surface. With them go the income and physical-activity models, region and ethnicity **data
loading**, the two-stage logistic option, the income-quintile FactorsMean strata, the trend types
other than `null`, the other five interventions, PIF, and individual-level tracking output.

Everything in that list is **rejected at load time with a located error naming the missing
feature and pointing at the backlog** — never silently ignored, and never producing a
plausible-looking wrong number. Four of the six converted examples load; `HLM_India` is rejected
for selecting `food_labelling` and `KevinHall_PIF` for enabling PIF; the two `KevinHall` examples
load their configs and are rejected at their model files.

The scope ruling for this run was the HLM_France and FINCH surfaces. HLM_France is done and FINCH
is not, and the reason is the size of the two model families — 2,615 and 1,462 lines in the
baseline — against the time the earlier tasks took. [docs/backlog.md](backlog.md) ranks them first
and second, and says what each unblocks.

## What the validation actually shows

**Component level.** 433 tests. The strongest are the ones that carry the baseline's expected
numbers over unchanged and still pass: the univariate-summary moment recurrence, the SHA-256
digests, the weight-model LMS classification, and — the best single piece of evidence in the
suite — `TestRelativeRiskLookup.ReferenceDataLookup`, 44 expected relative risks interpolated from
a real 7×5 table.

**End to end.** Over 20 seeds of 2010–2050, both scenarios, both sexes and 52 output variables:
38,260 comparisons of five statistics each. **Every mean agrees** — 0 failures in 7,652
comparisons. 54 comparisons (0.14%) fall outside the allowance, all between 1.01× and 1.36× it,
and all traced to one mechanism: when an age-sex band empties, immigration has nobody of that age
to clone, both implementations fall short of the projected total, and they do so on different
seeds.

Repeating at 60 seeds gives **the same 54 failures** — tripling the seeds tightens every allowance
by √3 and changes nothing, so what is left is a real difference of about the size the allowance now
is, not sampling noise. At 60 seeds it concentrates into three (year, sex) cells, each failing
simultaneously for six to eight correlated variables: one event, propagated through the count
weights. The largest disagreement anywhere in either run is 5 people in 3,461.

**Determinism.** Byte-identical output across repeats, across thread counts, and with an
intervention active — asserted by `tests/sim/reproducibility_test.cpp`, not just claimed.

**Memory and threading.** The whole suite passes under AddressSanitizer + UndefinedBehaviorSanitizer
(80 s) and under ThreadSanitizer (181 s), which is where audit finding B-02 — a data race in the
baseline's lazily-populated repository — was confirmed in the first place.

## What the process found that reading would not have

Five things worth recording, because each came from actually running something:

1. **The HLM model loaders read the wrong member names.** They used the internal spellings
   (`transition`, `residual_distribution`, `residuals_standard_deviation`) instead of the files'
   (`m`, `s`, `residualsStandardDeviation`). The synthetic fixture had been generated to match the
   loaders, so the whole path was green until the real 18.8 MB France model produced 54 located
   errors at once. The lesson was a test file that spells out the real format
   (`tests/config/model_loader_test.cpp`), one of whose tests asserts the internal names are
   *rejected*.

2. **`mean_gender` was `1/count` instead of `1`.** `gender` is a declared level-0 risk factor, so
   it is in the mapping, but a person carries it in `person.gender` rather than in `risk_factors` —
   so the sum came from the explicit accumulation and was then divided by the head count twice. The
   equivalence harness found it on its first real run. The baseline has a test for the income
   version of the same bug; this implementation now has the generalised one.

3. **The calibration adjustment must not be clamped.** `adjust_to_factors_mean` shifts an
   (age, sex) band by `expected − simulated_mean` so the band's mean lands exactly on the
   FactorsMean table. Clamping the shifted values to the factor's configured range moves it back
   off — most visibly at the young ages, where France's expected BMI of 14 sits just above the
   configured bound of 13.88. A consequence worth knowing: the band means of a calibrated run are
   **seed-independent**, in the baseline as here, and what the seed moves is the spread.

4. **41% of the run time was throwing exceptions.** The result writer asked for every
   income-stratified channel and caught the `out_of_range` when there wasn't one — which is most
   channels. It is the same anti-pattern the audit criticised in the earlier rewrite, written here
   by the same reflex. Removing it took the run from 8.8 s to 3.3 s. No amount of reading the code
   would have found it.

5. **`std_yld` disagreed with its own `mean_yld`.** The mean divided by person-years at risk
   (head count plus deaths) and the standard deviation by the head count, so a channel's spread was
   about 2% off the baseline's and inconsistent with its own mean. Found by the 60-seed run, where
   it was the only failing *mean* in the whole comparison.

## What a reader should be sceptical about

- **One example runs.** The equivalence evidence is one country, one model family, one
  intervention. It is the reference example and it is the right one to have first, but it is one.
- **The comparison's floor.** The baseline writes six significant digits, so no comparison can be
  tighter than about 10⁻⁵ relative. Several of this model's aggregates are nearly deterministic, so
  for those the test *is* that floor. A difference smaller than it would not be seen.
- **Twenty seeds is few for a standard deviation.** Its standard error at n = 20 is 16% of the
  standard deviation, which is why 29 of the 54 failures are standard deviations and why the run
  was repeated at 60.
- **macOS only, so far.** The code targets Linux and macOS and avoids what would break on either,
  but every measurement here is from one Apple M5 and there is no CI. That is the first
  low-effort item in the backlog.
- **The synthetic fixture pack is invented.** It exists so tests can run without the network. Its
  own `SYNTHETIC.md` says so, and records the one artefact it has (its top age equals the
  configured age range, so the top band empties each year).

## The next three things to do

1. `StaticLinear` — it unblocks region, ethnicity, income, physical activity, the two-stage
   logistic and the income strata, and it is the prerequisite for the FINCH example.
2. `KevinHall` — and with it the 30 baseline tests that are 30 of the baseline's own 35 skips, so
   porting them is the first time anyone learns whether they pass.
3. A CI workflow — `scripts/check.sh` is the whole of it; what is missing is a runner and a
   decision about whether CI fetches the disease data or runs against the synthetic pack.

[docs/backlog.md](backlog.md) has the rest, ranked, with what each costs.
