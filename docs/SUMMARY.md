# Summary of the fourth build run

What was built, what is proven, and where it stops. Written at the end of the run it describes.
Earlier runs' summaries are in the history of this file: the first covered the HLM surface, the
second the FINCH one, the third the library split and the index-keyed store.

## The short version

A deterministic C++20 reimplementation of the Health-GPS microsimulation, with **the whole upstream
model surface implemented** — nothing that upstream does is refused here any more — and three
examples compared against the baseline over 20 seeds and again over 60.

This run closed the previous one's three named next steps (CI, population impact fraction, the
factor-store lookup), added `HLM_India` to the comparison, and fixed a footgun in the harness
structurally. Along the way the India comparison **found a recorded deviation in the numbers for the
first time**, and the profiling work **falsified a claim this project had been repeating for two
runs**.

- **665 tests in 86 suites, plus 30 for the equivalence harness itself** — 668 CTest entries — all
  passing under every preset: release, debug, AddressSanitizer + UndefinedBehaviorSanitizer, and
  ThreadSanitizer. The baseline's 471 were gone through one by one; **426 have a counterpart here**,
  and of the 35 it skips on every machine, 30 run and pass here — the other five assert the contents
  of console tables this build does not print.
- **Three examples compared against the baseline**, `HLM_France`, `KevinHall_FINCH` and now
  `HLM_India` — the last at one hundredth of its shipped cohort, which is stated everywhere it is
  reported. `HLM_India` also brings the `EBHLM` dynamic model and 35 diseases into the comparison
  for the first time.
- **Population impact fraction is implemented**, the last refused feature. It cannot be compared
  against the baseline, because the only example that uses it is one neither implementation can run.
- **A CI workflow**: four presets on `ubuntu-latest` and `macos-latest`, Linux with clang *and* GCC,
  the harness's own tests, and the equivalence harness against the checked-in references. It has
  never been executed.
- **40 ADRs**, one per design decision, each with the alternatives rejected. Two are new, and one of
  them records a design that was implemented and then thrown away when it was measured.
- **45 recorded deviations** from the baseline — 22 fixed defects that change the numbers, 14 design
  differences, 9 internal ones — each with its audit finding ID and its evidence.
- **Three further baseline defects found**, B-26 to B-28, all by implementing population impact
  fraction against the real data pack.

## What is here

| | |
|---|---|
| `src/` | 142 files, 25,068 lines — core, diagnostics, RNG, I/O, config, data, model, sim, output, app |
| `tests/` | 60 files, 14,071 lines — 665 tests in 86 suites — plus `run_test.py` (30 tests of the harness) and `self_check.py` |
| `tools/` | `convert-config` (v1→v2, `--policy-scenario`, `--rebase`) and `gen-fixtures` (the synthetic pack) |
| `schemas/v2/` | the published config contract, kept in step with the loader by a test |
| `docs/` | 11 documents and 40 ADRs |
| `examples/` | the six upstream examples, converted, plus `KevinHall_PIF`'s twelve alternatives |
| `tests/equivalence/` | the harness, **three** stored baseline references, and the FINCH intervention set |
| `.github/` | the CI workflow and one composite action |

For comparison, the baseline is 41,400 lines of C++ for the whole model surface.

## The six tasks, and how each ended

| | Task | Outcome |
|---:|---|---|
| 1 | Orientation and cleanup | **Done.** Three processes were still running from the previous session and all were killed; the uncommitted work was the finished PIF implementation and was committed after checking it at 666 tests on all four presets. Nothing was discarded. [docs/build-notes.md](build-notes.md) has the detail. |
| 2 | The symlink footgun, fixed structurally | **Done.** A staged scratch directory now copies the config and links only what is read, and the single writer refuses to write through a symlink at all. Four tests, each of which fails against the old behaviour. [ADR 0039](decisions/0039-scratch-directories-copy-what-they-may-write.md). |
| 3 | `HLM_India` equivalence and full-scale figures | **Done**, and it found more than it was asked to. 20 and 60 seeds at a documented `size_fraction` reduction, on two intervention choices; full-scale wall time, memory and row counts for both implementations. See below. |
| 4 | The `find_index` linear scan | **Done**, in about an hour, byte-identical on both examples. `KevinHall_FINCH` is 1.44× faster. The interesting part is what the measurement said about the *previous* decision. [ADR 0040](decisions/0040-a-bounded-search-for-the-long-vectors.md). |
| 5 | CI workflow | **Done**, and validated by reading rather than by running, because neither `act` nor Docker is on this host. GCC is in the matrix; the three duplicated bootstraps are one composite action. |
| 6 | Docs, ADRs, backlog, this file | **Done.** `scripts/check.sh` exits 0: 668 tests in release (17.7 s), debug (188.5 s), asan-ubsan (574.4 s) and tsan (1,345.3 s), then both stored-reference comparisons at zero out of tolerance. Those preset timings are two to three times the previous run's because the machine was not idle — see *Performance*. |

## What the validation actually shows

**Component level.** 665 tests, of which 340 are in files the baseline has no counterpart for. The
strongest remain the ones carrying the baseline's expected numbers over unchanged: the
univariate-summary moment recurrence, the SHA-256 digests, the weight-model LMS classification, and
`TestRelativeRiskLookup.ReferenceDataLookup`'s 44 interpolated relative risks.

**End to end.** Three examples, two model families, two dynamic model families, six intervention
scenarios, 20 seeds and again 60 for each primary run.

| Example | Intervention | Seeds | Comparisons | Out of tolerance |
|---|---|---:|---:|---:|
| `HLM_France` | `simple` | 20 / 60 | 31,468 / 31,468 | **0** / **0** |
| `KevinHall_FINCH` | `simple` | 20 / 60 | 22,679 / 22,745 | **0** / **0** |
| `HLM_India` *(reduced cohort)* | `simple` | 20 / 60 | 67,885 / 68,740 | **0** / 3 |
| `HLM_India` *(reduced cohort)* | `food_labelling` | 20 / 60 | 68,083 / 68,833 | 3 / 34 |

Plus each of the six interventions on its own, on the two primary examples — the 347,768-comparison
sweep the previous run reported, with its one residual, which the `HLM_France` + `food_labelling`
re-run here reproduced exactly (31,552 comparisons, 0 out of tolerance).

**Adding India's four runs, 621,309 comparisons have now been made against the baseline**, of which
**41 are out of tolerance**: 31 are `mean_bmi` and are deviation B-24 measured, 9 are the harness's
lattice blind spot, and 1 is the previous run's isolated `HLM_France` residual. Every one of the 41
is accounted for by name, and none is unexplained.

**India was compared at reduced scale, not as shipped.** `--size-fraction 1e-5` puts **12,406**
people through the same code as the 1,240,613 the example ships — about twice `HLM_France`'s cohort.
The value goes into both implementations' configs identically, so the comparison is exactly as valid
a test of the *code* as it would be at full scale; what it is not is a test at full scale. It is part
of the derived config and so part of the config hash, so a reduced run cannot be matched against a
full-scale reference by accident.

**Population impact fraction has no comparison against the baseline at all.** It is implemented and
`KevinHall_PIF` loads completely — config, both model files, the registry, 69 fraction tables, both
scenarios' modules — and then stops in its first simulated year, because it shares
`KevinHall_India`'s weight defect **byte for byte** and the baseline dies in the same place. So the
PIF mechanism is validated **end to end on the synthetic fixture pack only**, by
`tests/data/pif_data_test.cpp`. Against the real baseline it is validated **not at all**, and that
cannot change until upstream fixes the pack.

**Determinism.** Byte-identical output across repeats, across thread counts, and for each of the six
interventions — asserted, not claimed. Across every run this project has made, this build has not
once exited on a signal; the baseline has, on `KevinHall_FINCH`, in 4 of 180 measured runs.

**Memory and threading.** The whole suite passes under ASan+UBSan and under ThreadSanitizer.

## What the process found that reading would not have

1. **A recorded deviation showed up in the numbers for the first time.** B-24 — the baseline's
   food-labelling policy re-applies its impact to somebody who failed an early coverage draw and
   passed a later one — was found by reading code two runs ago and had never affected a comparison.
   `HLM_India` is the only example shipping an *active* intervention, and the one it ships is that
   policy. Mean BMI of males, this build minus the baseline: identical to 10⁻⁷ in the baseline
   scenario at every year of 2010–2050; in the intervention scenario **exactly zero in the policy's
   first year**, then +0.0007, +0.0045, +0.0124, +0.0311 through 2026, then flat to 2050. Zero in
   year one because the defect needs a *previous* failed draw. Growing only while the coverage window
   is open. The baseline lower, because it applies a BMI-lowering impact more than once. Every one of
   the five things the defect predicts. Re-running `HLM_France` with the same policy reproduces the
   same curve at the same size, with its worst excursion at **0.820×** of its allowance in the same
   variable, statistic and cell as India's worst — so the difference is not India's, and India
   surfaces it only because a larger cohort gives a tighter allowance. **A tighter test found a real
   difference that a looser one had been passing over.**

2. **A claim this project had repeated for two runs was false, and the profiler caught it.** ADR 0037
   said a person carries "eleven risk factors on `HLM_France` and fifty-five on `KevinHall_FINCH`",
   and sized a data structure on it. Those are the counts a config *declares*. Instrumenting the
   store over a whole run says a person actually carries **6** on France and **121** on FINCH. The
   change that was about to be committed on the strength of the old numbers — a direct probe at
   `entries_[index]` — hits **0.0% of 122 million lookups** on FINCH. It would have made `HLM_France`
   5% slower and been credited with FINCH's improvement, which came from bounding the search instead.
   [ADR 0040](decisions/0040-a-bounded-search-for-the-long-vectors.md) records the rejected design as
   well as the accepted one.

3. **The harness's lattice rule has a hole, and it is in the denominator.** Six of India's residuals
   are the 95th percentile of a rare-disease rate at 1.02×–1.09×, in *both* policies — so they belong
   to the comparison, not to either implementation. The rule that should catch them classifies on the
   **rate**: these series have 43–79 distinct values and a modal share of 0.30–0.40, under both of its
   thresholds, while a third of their seeds are exactly zero. A small integer count of cases over a
   band head count that varies by seed presents many distinct values to a detector looking at the
   quotient. The fix is to classify on the numerator. The thresholds were **not** widened to admit
   what had already been seen; it is backlog item 12 with the measurement attached.

4. **Two published data releases, and one example that names both.** `KevinHall_PIF`'s primary config
   names `pif-data-v5.zip` and all twelve of its alternatives name `pif-data-v7.zip`, which is a
   different store with a third risk factor. Both were fetched and verified. So
   `config_jointS1..S3` are not broken, as they appear against v5; the example is inconsistent.

5. **A missing population impact fraction is a silent no-op upstream** (B-26). `get_pif_data` returns
   nothing and warns at a verbosity nobody runs. Worse, a table with *gaps* reads as a fraction of
   exactly zero, because the dense array is sized from the observed extremes and unmentioned cells
   are default-constructed — and zero is a value the real tables are full of. The example's own legacy
   `config.json` selects fifteen diseases and a risk factor four of them have no table for; upstream
   applies the policy to eleven and says nothing.

6. **The PIF schema's sex encoding is the opposite of its data's** (B-27). `cervicalcancer` settles
   it: 600 non-zero cells of 3,330 at `Gender=1`, none at `Gender=0`.

7. **At the size where it is visible, sequential scenarios cost exactly the parallelism.** The
   baseline had never been run on `HLM_India` — the largest example, 1.24 million people — on any
   machine. It was, and it finished in **2,079 s against this build's 2,068 s: the same wall time**,
   using **4,154 s of CPU against 2,038 s** and **3,011 MiB against 1,872 MiB**. The baseline runs its
   two scenarios on two threads; this build runs them in sequence on one, which is what buys
   byte-identical output and removes audit findings B-01 and B-02. On the two small examples that
   trade costs nothing, because the work itself is smaller; on the large one it costs the second core
   and nothing else. That is a better answer than the small examples could give, and it took running
   the baseline on the one example nobody had run it on.

8. **A long measurement started in the background must write to a file, not a pipe.** The previous
   session's full-scale `HLM_India` run was still going, 20 minutes in, with its `/usr/bin/time -l`
   output going to a pipe whose reader had died with that session. It would have finished and lost
   the only thing it was running for.

## Performance

[docs/performance.md](performance.md). Every figure below is this build against the baseline on the
same derived configs, one thread.

| | Baseline | This build |
|---|---|---|
| `HLM_France`, 2010–2050 | 4.78–5.26 s, 85.2 MiB | **2.41 s, 52.4 MiB** |
| `KevinHall_FINCH`, 2022–2032 | 15.19–15.52 s, 198 MiB | **7.38 s, 78 MiB** |
| `HLM_India`, 2010–2050, **full scale** | 2,079 s (34.7 min), 4,154 s CPU, 3,011 MiB | **2,068 s (34.5 min), 2,038 s CPU, 1,872 MiB** |

Before and after this run's one performance change
([ADR 0040](decisions/0040-a-bounded-search-for-the-long-vectors.md)), alternating the two binaries
run by run so drift falls on both equally:

| | CPU, best of 5 | Verdict |
|---|---|---|
| `KevinHall_FINCH` | 10.65 → **7.38 s** | **1.44×** |
| `HLM_France` | 3.13 → 3.37 s | unchanged — its vectors are under the threshold, so it runs the same code |

**Those before/after numbers were taken on a machine that was not idle** (Android Studio at 346% of
CPU, Spotlight indexing), which is the same effect this project already records as worth 20% of a
wall-clock measurement. The relative figure survives it because the runs were interleaved; the
absolute ones are worse than the table above for that reason.

`HLM_India` at full scale is **2,068 s and 1,872 MiB**, against 2,535 s and 2,555 MiB before the
index-keyed store — **1.23× faster and 27% less memory**, and the first confirmation that ADR 0037
and ADR 0040 pay on the largest example rather than only on FINCH. It writes 16,564 rows to each of
four CSVs, the same count as at any cohort size, because the output is per (year, sex, age band) and
not per person.

**The baseline had never been run on `HLM_India` before this run**, on any machine. It was, and the
result is the one place where this build does *not* win on wall time: **2,079 s against 2,068 s — the
same — but 4,154 s of CPU against 2,038 s, and 3,011 MiB against 1,872 MiB.** The baseline runs its
two scenarios on two threads; this build runs them one after the other on one
([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)). So at the size where
the trade is visible, sequential scenarios cost exactly the parallelism and nothing more: **half the
CPU and 62% of the memory, for the same answer in the same time.** On the two small examples the
trade costs nothing at all, because the work itself is smaller.

## What a reader should still be sceptical about

- **India was compared at a hundredth of its cohort.** 12,406 people, not 1,240,613. Its excluded-band
  count is about twice the other examples' for exactly that reason — a smaller cohort empties more
  bands — so the full-scale comparison would exclude less and test more. Nothing in the India result
  is evidence about the example as shipped.
- **Population impact fraction has never met the baseline.** Only the synthetic pack exercises it end
  to end. The one example that uses it cannot run in either implementation.
- **The FINCH surface is still one country, one data pack.** The HLM surface now has two, France and
  India, which is what `HLM_India` adds. `KevinHall_India` was to be the FINCH surface's second and
  cannot be run at all, so everything known about `StaticLinear` and `KevinHall` comes from one pack.
- **CI has never run.** The workflow was validated by reading every step against the local scripts,
  because neither `act` nor Docker is installed here. The first execution will also be the first
  evidence that it works, and something in it is probably wrong.
- **GCC has never built this tree**, and neither has Linux. The warning set is `-Werror` with
  `-Wconversion`, `-Wsign-conversion` and `-Wold-style-cast`, and only clang has ever satisfied it.
  The GCC matrix entries are `continue-on-error` for that reason.
- **The comparison's floor.** The baseline writes six significant digits, so no comparison can be
  tighter than about 10⁻⁵ relative. For the nearly-deterministic aggregates, the test *is* that floor.
- **The harness decides the headline result, and it has now been wrong three times** — a normal-theory
  allowance on a point mass, the same on a lattice-valued median, and now a lattice detector that
  cannot see a lattice in a numerator. It has 30 tests, which is better than nothing and is not the
  same as being right.
- **macOS and Apple clang only**, on one machine, for every measurement in this repository.
- **The synthetic fixture pack is invented.** Its own `SYNTHETIC.md` says so.

## The recommended next run

**Build a graphical host**, and close the API gaps it needs on the way.
[docs/backlog.md](backlog.md) item 1 lists eight, with costs: results in memory, per-year results in
the event stream, progress inside a year, structured diagnostic arguments, config *writing*,
enumerating what a data pack offers, observable cancellation, and a version on the API.

It is the right next run for three reasons. The model surface is complete, so there is no feature
work competing with it. The library split, the event stream and the run manifest were all built for a
host that does not exist, and a contract with one implementor is a description of that implementor —
`src/app` is currently the only thing that has ever tested whether `docs/api.md` is the right shape.
And the two gaps that matter most, results in memory and per-year results, are **one design decision
about result ownership**, which is cheaper to make before something depends on the current shape than
after.

Two smaller things are worth doing in the same run because they are nearly free and they unblock
claims this document has to hedge: **run the CI workflow once** and fix whatever it says (item 2),
and **fix the lattice detector** to classify on the numerator (item 12), which would either explain
or remove the six residuals in the India comparison.

[docs/backlog.md](backlog.md) has the rest, ranked, with what each costs.
