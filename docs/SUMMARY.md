# Summary of the seventh build run

What was built, what is proven, and where it stops. Written at the end of the run it describes.
Earlier runs' summaries are in the history of this file: the first covered the HLM surface, the
second the FINCH one, the third the library split and the index-keyed store, the fourth `HLM_India`
and the first CI workflow, the fifth the local server, the frontend and switchable deviations, the
sixth a second fixture pack, a browser in CI, and nine findings. A comment in the code that cites
"`docs/SUMMARY.md`, finding N" means the run that wrote the comment; `git log -p docs/SUMMARY.md`
is where to find it.

## The short version

A deterministic C++20 reimplementation of the Health-GPS microsimulation, with the whole upstream
model surface implemented, three examples compared against the baseline, and three ways to use it:
as a library, from a command line, and from a browser.

**This run had three tasks and all of them were the previous run's leftovers**: the one correctness
defect it found and did not fix, the ThreadSanitizer job its new tests had made twice as long, and
the performance item its profile pointed at. All three are done. The finding worth the run is the
one nobody asked for, and it came out of the first task:

> **Nothing has ever compared the income-stratified result files.** On `KevinHall_FINCH`, **49
> columns are identically zero in every row of every stratum file this build writes and non-zero in
> the baseline's** — same example, same data, same header. Four of them are the weight categories and
> are fixed here; the other 45 are [docs/backlog.md](backlog.md) item 2. The equivalence harness
> reduces the whole-population CSV, and `find_result_csv` exists precisely to *exclude* the
> stratified ones, so the only comparison this project has against the baseline does not cover them
> at all.

| | |
|---|---:|
| Tests, C++ | **850** in 101 suites — 853 CTest entries — passing under release, debug, ASan+UBSan and TSan, where 761 of them run ([ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md)) |
| Tests, the equivalence harness's own | **50** (was 48) |
| Tests, the frontend | **48** unit, **19** end to end in a browser |
| Comparisons against the baseline this run | **187,754**, **0** out of tolerance — all four references regenerated |
| Source | `src/` 153 files; `tests/` 72 files; 45,559 lines of C++ between them; `web/src/` 19 files and `web/e2e/` 6, 3,215 lines |
| Documents | **14**, plus **46 ADRs** |
| CI | **15 jobs** — see below |
| Findings this run | **5**, three of them one thing: a whole output file family nothing was checking |

## The three tasks, and how each ended

| | Task | Outcome |
|---:|---|---|
| 1 | The weight-category defect | **Done**, and **not** the way the ruling assumed. It is not a deviation from the baseline, so there is no compatibility flag. **Four findings.** All four stored references regenerated against the baseline binary; the comparison counts and the verdict are unchanged. |
| 2 | ThreadSanitizer runtime | **Done.** [ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md): one fixture pack and six self-check seeds under that sanitizer only. `macos · appleclang · tsan` **88m08s → 33m09s**, `linux · clang · tsan` **42m07s → 15m56s**, and the macOS job is no longer the workflow's long pole. |
| 3 | The analysis module's channels | **Done.** `KevinHall_FINCH` **1.33×**, `HLM_France` **1.22×**, and every CSV of every runnable example byte-identical — including `HLM_India` at the 1,240,613-person cohort it ships. Backlog item 9 closed. |
| + | Anything else small, output-preserving and undisputed | **One taken, and it is documentation.** [docs/upstream-reports.md](upstream-reports.md) writes up the four findings that belong to upstream, each with the command that reproduces it against their binary and data (backlog item 14's writing half). Nothing else was taken, and the reason is worth stating: by then `scripts/check.sh` was running against the tree the three tasks produced, and any further change to `src/` or `tests/` would have thrown that verification away to save an hour of somebody else's reading. |

## The five findings, and what found each

| | What | Found by |
|---|---|---|
| 1 | **It is not a deviation, so the ruling's first branch does not apply.** The weight categories are head counts in the baseline's result file too — `analysis_module.cpp:2014` increments one per person per band and nothing divides them — so this build does not differ from the baseline about them, and there is nothing for [ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md)'s compatibility flag to restore. The defect was entirely in this project's own two reductions | reading the baseline's analysis module beside this one's, before writing any code |
| 2 | **This build writes four columns of zeros in every income-stratified file that the baseline fills.** `calculate_income_based_series` never classified weight at all | fixing finding 1, and then checking whether the four columns were right in *every* file rather than in the one the harness reads |
| 3 | **45 more columns in those files are zero here and non-zero in the baseline** — `deaths`, `emigrations`, the three burden channels, seven demographic means and all 33 standard deviations. And **nothing compares those files**: the harness reduces the whole-population CSV only | the same measurement, run column by column over both implementations' output |
| 4 | **The income series had no test at all.** Neither fixture pack assigns an income category — both are HLM, and only the StaticLinear family assigns one — so no test that runs a configuration could reach that code | writing a test for finding 2 and discovering there was nowhere to put it |
| 5 | **The first version of the performance change cost `HLM_France` 5.4 MiB of peak memory** while making it faster: it resolved the income strata eagerly, allocating a channel vector per age for strata nobody is in, once per simulated year | the peak-memory column of the A/B, which is in that table for exactly this |

Findings 2, 3 and 4 are one thing seen from three sides, and the thing is worth stating plainly:
**a whole output file family has been written by this build for as long as it has written them,
with nothing checking it against anything.** The comparison that this project trusts reduces one
file per run; the tests that
run configurations use packs that cannot produce the others. What fixed finding 2 was four lines;
what finding 3 needs is a comparison, which is why it is [docs/backlog.md](backlog.md) item 2 rather
than more four-line fixes.

## The weight categories, and the ruling that turned out not to apply

The previous run found that `normal_weight`, `over_weight`, `obese_weight` and `above_weight` are
head counts and that both of this project's reductions treated them as means. The ruling for this
run said: if that is a deviation from the baseline, fix it and give it a named compatibility flag
after its deviation ID, default off; if it turns out to be in the new code only, it is a plain bug —
fix it, regenerate the affected references, and say so.

**It is the second case, and the evidence is one function in each implementation.**
`AnalysisModule::classify_weight` increments one of four channels per person per band, here and in
`hgps_main/src/HealthGPS/analysis_module.cpp:2014`, and neither implementation's "sums become means"
pass touches those four channels. So the result files agree, column for column, and there is nothing
to restore: a flag exists to reproduce a baseline behaviour this build deliberately does not have,
and this build has the baseline's behaviour exactly.

What was wrong was three things this project owns:

- **the equivalence harness** count-weighted them, which made the reduced figure for `normal_weight`
  on `HLM_France` at (baseline, 2030, male) **15.3** where the population figure is **1,311.85**;
- **`GET /api/runs/{id}/summary`** applied the same rule, and that is what the results screen charts,
  so the level of that chart meant nothing;
- **the income-stratified series** never filled the four columns at all.

The first two go together because [docs/server-api.md](server-api.md) says they must: two reductions
that disagree are worse than one that is wrong, since a client cannot tell which it is looking at.
`SummaryReduction.TheSummedColumnsAreTheOnesTheHarnessSums` now asserts the two lists are the same
list.

**No comparison was ever wrong about this**, because both implementations were reduced identically,
and the series' shape followed the underlying quantity, which is why it never looked wrong. What it
broke was the level of a number a reader sees.

**And nothing was added to [docs/deviations.md](deviations.md) or to the list of baseline findings
in
[docs/briefing.md](briefing.md)**, deliberately: both of those record places where this build
differs from the baseline on purpose, and this was not one. What went into the briefing instead is
the thing upstream would want to know — that the comparison reads one file per run, so their
stratified output has never been checked against ours.

### The check that the new figure is right

A stored reference holds *reduced* values, so all four had to be regenerated by running the baseline
binary again — 20 seeds each, the same derived configs and so the same hashes. That gives the check,
and it is a check against the **baseline's** numbers rather than against this build's:

| `HLM_France`, baseline, 2030, male | Before | After |
|---|---:|---:|
| `normal_weight` | 15.3194 | **1,311.85** |
| `over_weight` | 12.1496 | **1,068.70** |
| `obese_weight` | 8.5006 | **765.45** |
| `above_weight` | 20.6501 | **1,834.15** |
| `count` | 3,146.00 | 3,146.00 |

In the regenerated references, `normal + over + obese` equals `count` and `over + obese` equals
`above` **exactly — residual 0.0 — in all 3,280 `HLM_France` cells and all 880 `KevinHall_FINCH`
cells**. The old reduction could not satisfy that identity in any cell whose bands differ in size,
which is every cell.

And the comparison counts did not move: 31,546 on `HLM_France`, 22,616 on `KevinHall_FINCH`, 66,787
and 66,805 on `HLM_India`'s two — the same four counts the previous run's re-score produced, none of
them out of tolerance. The four variables are compared numerically before and after, five statistics
each, so what changed is the level of a number and not how it is tested.

## The income-stratified files

Run both implementations on `KevinHall_FINCH` and ask of every column of every stratum file whether
it is identically zero in all 4,884 of its rows. All four strata give the same answer:

| | Columns |
|---|---:|
| zero here, non-zero in the baseline | **49** |
| of those, fixed this run | 4 |
| zero in both — channels neither fills for this example | 7 |
| non-zero here and in the baseline | the rest |

The four this run fixed are the weight categories, and they now satisfy in our file the identity
they satisfy in the baseline's: `normal + over + obese == count` in all 4,884 rows of both.

The other 45 are `deaths`, `emigrations`, `mean_yll`, `mean_yld`, `mean_daly`, seven demographic
means, and **33 `std_` columns** — the last because `calculate_income_based_series` has no
standard-deviation pass at all, while the baseline has
`calculate_income_based_standard_deviation`. They are [docs/backlog.md](backlog.md) item 2, and the
item is as much about the missing comparison as about the missing columns: filling them with nothing
to check them against is how the four got missed in the first place.

## ThreadSanitizer

**88 minutes was the problem, and the second fixture pack was the cause.** The previous run
doubled the simulations the suite runs and under TSan a simulation is seconds rather than a fifth of
one; the `macos · appleclang · tsan` job went from 48m28s to 88m08s and became the workflow's long
pole by a wide margin.

Two changes, under that sanitizer only ([ADR
0046](decisions/0046-what-runs-under-which-sanitizer.md)):
the `Packs/` suites run against the first fixture pack, and the harness's two self-checks run at six
seeds rather than twenty. Nothing is dropped from release, from debug or from AddressSanitizer,
which is where the second pack's *logic* coverage lives; what TSan is for is races, and a race is a
property of the code rather than of the configuration that reaches it.

Locally, the same command before and after:

| | Tests | Time | Share |
|---|---:|---:|---:|
| `Packs/` — the parameterised suite | 184 → **92** | 1,975 → **1,207 s** | 78% → 83% |
| the harness's two self-checks | 2 | 501 → **185 s** | 20% → 13% |
| everything else | 662 → **667** | 51 → **64 s** | 2% → 4% |
| **total** | 848 → **761** | **2,529 → 1,458 s** | |

**That after column was measured while a twenty-seed baseline sweep ran beside it**, so it is
pessimistic, and the split is what it is for. The clean figure is the one `scripts/check.sh`
produced on a quieter machine two hours later: **1,001 seconds**, which is 2.5× rather than 1.7×.
**In CI, where the two numbers are directly comparable job for job**:
`macos · appleclang · tsan` went **88m08s → 33m09s** and `linux · clang · tsan` **42m07s → 15m56s**,
both 2.6×. The macOS job was the workflow's long pole by a factor of two; the longest entry is now
`linux · clang · asan-ubsan` at 33m20s, and the two are within eleven seconds of each other.

**What it costs** is stated in the ADR rather than explained away: a race reachable only through the
second pack's configuration and not the first's would no longer be found. Nothing this project has
recorded is of that shape, and both packs still run under AddressSanitizer.

## The analysis module

The previous run resolved names to indices at the call site and `_platform_memcmp` did not move —
795 samples before, 832 after. Its profile said why: the string work had moved one layer up, into
the analysis module, at 868 of 4,238 thread samples.

Both of the module's passes over the population, and the income-stratified one, built
`"mean_" + key` per factor **per person per year**, lower-cased it, probed a `std::set<std::string>`
and then looked the channel up again by name in a `std::map<std::string, std::vector<double>>` — for
the income series, in a map of maps of maps. A channel is now resolved once a year to the two
vectors it writes, and the mapping's factors once a year to `(index, channel)`, so the person loop
reads `find_index` and adds through a pointer.

Five runs of each binary, alternating run by run on an idle machine
([docs/performance.md](performance.md)):

| | Wall, best of 5 | CPU, best of 5 | Peak memory |
|---|---|---|---|
| `HLM_France` | 1.26 → **1.03 s, 1.22×** | 1.25 → **1.02 s** | 42.8 → 42.9 MiB |
| `KevinHall_FINCH` | 4.60 → **3.47 s, 1.33×** | 4.56 → **3.46 s** | 79.0 → 79.0 MiB |

**France moves this time and it had to**: the previous change was three places on the FINCH surface,
this one is in the module every example runs.

A whole-run profile of FINCH: **3,652 thread samples before, 2,828 after, and 622 of the 824 that
went — 75% — are name handling**. `DataSeries::at` by name goes from 111 samples to below the
profiler's five-sample floor; `memcmp` and its stub from 751 to 501; the `tolower` family from 391
to 165; the four analysis functions' own time from 128 to 14.

**And the output did not change**: byte for byte, every CSV of all three runnable examples,
`HLM_India` included at the cohort it ships — 16,650,850 bytes of result CSV and three stratum files
of 3,926,215 each, identical before and after. That one is twenty-five minutes a side, and it is the
example where a constant factor is paid 1.24 million times a year.

**The first version of it cost `HLM_France` 5.4 MiB of peak memory**, by resolving the income strata
eagerly for every category the layout declares rather than on first sighting — vectors nobody was
in,
allocated once a year. Measured, then fixed. It is in this summary because the only reason it was
caught is that the A/B carries a peak-memory column, and a performance change that quietly trades
memory for time should have to say so.

## CI, per matrix entry

Fifteen jobs. Run **35400069202** on `6b01398`, every entry read with `gh run view` rather than
from the run's own summary. **15 of 15 success.** The last column is the same job on the previous
run's final commit.

| Job | Result | Time | The previous run |
|---|---|---:|---:|
| `linux · clang · release` | **success** | 5m07s | 5m54s |
| `linux · clang · debug` | **success** | 13m09s | 10m35s |
| `linux · clang · asan-ubsan` | **success** | 33m20s | 43m13s |
| `linux · clang · tsan` | **success** | **15m56s** | 42m07s |
| `linux · gcc · release` | **success** | 4m23s | 5m24s |
| `linux · gcc · debug` | **success** | 11m51s | 16m20s |
| `macos · appleclang · release` | **success** | 5m23s | 3m56s |
| `macos · appleclang · debug` | **success** | 9m33s | 16m01s |
| `macos · appleclang · asan-ubsan` | **success** | 29m49s | 34m12s |
| `macos · appleclang · tsan` | **success** | **33m09s** | 88m08s |
| `equivalence · HLM_France · 20 seeds` | **success** | 4m42s | 7m16s |
| `equivalence · KevinHall_FINCH · 20 seeds` | **success** | 5m44s | 5m06s |
| `web · typecheck, test, build` | **success** | 0m13s | 0m09s |
| `web · end-to-end` | **success** | 2m54s | 3m02s |
| `performance · linux · indicative` | **success** | 3m19s | 5m42s |

`6b01398` is the last commit of this run that changes code; everything after it is documentation,
and the workflow's concurrency group cancels the earlier run on each push, so this is the newest
run that reports on the code as it now stands. The push that adds this file starts one more, on the
same matrix over the same code.

**The two ThreadSanitizer entries are the point of the table.** `linux · clang · tsan` went from
42m07s to 15m56s and `macos · appleclang · tsan` from 88m08s to the figure above, which is what
[ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md) was for. Nothing else in the table
moved for a reason belonging to this run: the other entries differ by runner weather, and the
previous run's numbers are beside them so a reader can see which is which.

## The same tree locally

`scripts/check.sh` with nothing skipped, on the tree these commits leave behind — 43 minutes end to
end, exit 0:

| | Result | Time |
|---|---|---:|
| release | **853 / 853** | 33 s |
| debug | **853 / 853** | 327 s |
| asan-ubsan | **853 / 853** | 982 s |
| tsan | **761 / 761** | 1,001 s |
| frontend, type-check and unit | **48 / 48** | under a second |
| frontend, end to end in a browser | **19 / 19** | 6.3s |
| equivalence, `HLM_France` | **31,546** comparisons, **0** out of tolerance | — |
| equivalence, `KevinHall_FINCH` | **22,616** comparisons, **0** out of tolerance | — |

The TSan row is the clean measurement of this run's change: **1,001 seconds against 2,529**, on a
machine with nothing else on it. The 1,458 s in the table above was taken while a twenty-seed
baseline sweep ran beside it.

**The two equivalence rows are the check that the regenerated references are the right ones**, run
here against a build this session made and in CI against one the runner made, independently.

## The recommended next run

**Give the income-stratified files a comparison** — [docs/backlog.md](backlog.md) item 2. It is
first because of what this run found out: 45 columns of every stratum file are zero here and filled
in the baseline, and the reason nobody noticed is that the harness reduces one file per run.
Filling the columns without a comparison would be writing code against a baseline read by eye,
which is how the four that were fixed here came to be wrong in the first place. The shape is to
reduce and compare every CSV a run writes rather than the one; it would also cover the
individual-tracking file if item 4 ever writes one.

If a modelling answer arrives before then, **item 1 outranks it**: interventions on the Kevin Hall
surface are a question for the upstream authors ([docs/briefing.md](briefing.md)), and four of the
six examples can only be run with a no-op policy until it is answered. It has been first on this
list for two runs and it is work nobody here can do.

And **`HLM_India` at the cohort it ships** (item 5) is still the largest single gap in the
validation: machine time rather than work, a few days of it, and the one thing that would let this
document stop hedging about a hundredth of a cohort.

## What a reader should still be sceptical about

- **The income-stratified files are not compared against anything**, and 45 of their columns are
  zero here and non-zero in the baseline. This run fixed four of the 49 and measured the rest; the
  files have never been part of any comparison ([docs/backlog.md](backlog.md) item 2).
- **India was compared at a hundredth of its cohort**, 12,406 people rather than 1,240,613. Nothing
  in the India result is evidence about the example as shipped.
- **Population impact fraction has never met the baseline.** Only the synthetic pack exercises it end
  to end; the one example that uses it cannot run in either implementation.
- **The FINCH surface is still one country, one data pack.**
- **Four of the six upstream examples can only be compared with `simple` active**, because an
  intervention on the Kevin Hall surface is a no-op upstream (B-25) and this build refuses the
  configuration rather than running it silently. Changing that needs a modelling decision this
  repository cannot make — [docs/briefing.md](briefing.md) states it as a question.
- **The comparison's floor.** The baseline writes six significant digits, so no comparison is tighter
  than about 10⁻⁵ relative.
- **The harness has been wrong five times** — a normal-theory allowance on a point mass, the same on
  a lattice-valued median, a detector that could not see a lattice in a numerator, a rule that
  checked half of its own justification, and the previous run's first attempt at the numerator fix.
  Its reduction was also wrong about four columns until this run, which is a sixth thing if you count
  the level of a number as a result, and you should. It has 50 tests, which is better than nothing
  and is not the same as being right.
- **Under ThreadSanitizer the suite is 761 tests rather than 850**, on purpose, and the risk that
  buys the time is named in [ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md).
- **The synthetic packs are invented.** Both of them; their `SYNTHETIC.md` says so. The second is not
  more realistic than the first, only *different*, which is the only property claimed for it. Neither
  assigns an income category, which is why the income series had no test until this run.
- **Nineteen end-to-end tests is not coverage.** They cover each screen's principal job and the
  hand-offs between them, in one browser.
