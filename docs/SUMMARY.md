# Summary of the sixth build run

What was built, what is proven, and where it stops. Written at the end of the run it describes.
Earlier runs' summaries are in the history of this file: the first covered the HLM surface, the
second the FINCH one, the third the library split and the index-keyed store, the fourth `HLM_India`
and the first CI workflow, the fifth the local server, the frontend, and switchable deviations.

## The short version

A deterministic C++20 reimplementation of the Health-GPS microsimulation, with the whole upstream
model surface implemented, three examples compared against the baseline, and three ways to use it:
as a library, from a command line, and from a browser.

**This run added no feature.** Every one of its tasks was a way of making the existing tree harder to
fool, and the point of it is the count at the bottom of this section: **nine findings** — eight
defects and one component with no test that ran it — seven of them found by something that was not
there before.

Three things did most of the finding.

- **A second synthetic configuration**, differing from the first in every way a program might have
  assumed it did not, with every test that runs a configuration parameterised over both. The
  previous run found a hard-coded output file name that forty-five passing server tests had missed,
  because all forty-five used the one fixture. This is that lesson made structural
  ([ADR 0044](decisions/0044-two-fixture-packs-and-a-parameterised-suite.md)).
- **A browser, driving the built frontend against a real server**, in CI. The previous run recorded
  nine defects in its new code and none found by a test; three of those were found by a person
  opening the page. This is that person, written down
  ([ADR 0045](decisions/0045-end-to-end-tests-in-a-real-browser.md)).
- **A randomised server-lifetime stress test**, under ThreadSanitizer, which is the first test of
  that layer written without knowing what it is looking for.

| | |
|---|---:|
| Tests, C++ | **845** in 100 suites — 848 CTest entries — passing under release, debug, ASan+UBSan and TSan |
| Tests, the equivalence harness's own | **48** (was 39) |
| Tests, the frontend | **48** unit, **19** end to end in a browser |
| Comparisons against the baseline this run | **187,754**, **0** out of tolerance |
| Source | `src/` 153 files; `tests/` 71 files; 44,978 lines of C++ between them; `web/src/` 18 files and `web/e2e/` 6, 3,039 lines |
| Documents | 13, plus **45 ADRs** |
| CI | **15 jobs** — see below |
| Findings this run | **9** — eight defects and one untested component. Five came from the three mechanisms above, one from re-scoring the stored references, one from a unit test, two from reading |

## The ten tasks, and how each ended

| | Task | Outcome |
|---:|---|---|
| 1 | Orientation, pre-flight, the concurrency group, CI on HEAD | **Done.** No stale processes of this project's were running; the concurrency group was already in the workflow from the end of the previous run; run 35360811751 on the starting commit was 13 of 13 green. |
| 2 | A second fixture, and parameterised tests | **Done.** [ADR 0044](decisions/0044-two-fixture-packs-and-a-parameterised-suite.md). **Three findings**, and 184 tests where there were 92. |
| 3 | Playwright end to end, and a CI job | **Done.** [ADR 0045](decisions/0045-end-to-end-tests-in-a-real-browser.md). 19 tests in about seven seconds, **one defect**, green in CI on its first run. |
| 4 | A server stress test under TSan | **Done.** `tests/server/stress_test.cpp`, **one defect** — a hang — on its first run. 78 seconds under TSan at six shuffles, trimmed to three. |
| 5 | GCC required | **Done.** One line, and the reason for it had expired. |
| 6 | The lattice detector, and the India re-score | **Done**, with no threshold moved. All four stored references re-scored; **one defect in the first version of the change**, found by that re-score. |
| 7 | Names to indices at the call site | **Done**, byte-identical on all three runnable examples. `KevinHall_FINCH` is faster; `HLM_France` is unchanged and had to be. **Two defects**, one of them a hazard rather than a fault. |
| 8 | A Linux timing job | **Done.** `scripts/measure.sh`, run by CI and by a person, uploading its JSON. Indicative only, and it cannot fail the build. |
| 9 | The briefing for Imperial | **Done.** [docs/briefing.md](briefing.md). |
| 10 | Docs, ADRs, backlog, this file | **Done.** |

## The nine findings, and what found each

The point of the run, in one table. **Seven of the nine were found by something running**, which is
the difference between this run and the one before it — that one found nine defects in its new code
and none of them by a test. Two of these were still found by reading, and they are marked as such,
because a summary that claimed otherwise would be doing the thing this project keeps catching.

| | What | Found by |
|---|---|---|
| 1 | **An age range narrower than the population data crashed with `map::at: key not found` and no location.** The cohort is drawn from the data while several per-age tables are built over the configured range. The baseline reaches the same place inside a parallel loop | the second fixture pack, in its first run |
| 2 | **A cancelled two-scenario run started the intervention anyway and simulated a year of it**, so the result file held a baseline stopping in one year and an intervention stopping in another — and `years_completed` was four rather than three. The comment above that code claimed the opposite | the second fixture pack, which is the first fixture with two scenarios |
| 3 | **The CLI had no test that ran it.** Everything ran in process with the output folder overridden, so honouring a configuration's own `output.folder` and printing the files it wrote were untested | writing the second fixture's tests |
| 4 | **A server stopped before it had served anything hung for ever.** cpp-httplib's `stop()` does nothing unless the server is already running, so a `start()` that returned as soon as its thread was spawned could lose the stop, and the join never returned | the stress test, on its first run, because one of its shuffled moments was "immediately" |
| 5 | **Pressing Start left the previous run on screen** — its id, its `completed` state and a "See the results" button pointing at the run before — for as long as the POST took | the end-to-end tests, whose helper read that stale id |
| 6 | **The first version of the lattice change rounded the numerator to a whole event**, which is finer than printed precision for a large total. Every calibrated mean on `HLM_India` failed: 216 comparisons, all of them two runs agreeing to every digit the baseline prints | re-scoring the stored references |
| 7 | **`resolve_predictors` used `find`**, which answers `unknown` for a name nothing has interned *yet* as well as for one that never will — freezing a predictor into the string-resolver path for the life of the model. A whole `KevinHall_FINCH` run was byte-identical with it | a unit test that resolves a model before building the person it is evaluated against |
| 8 | **The per-call fallback wrote to a process-wide table**, which is a race waiting for a caller even though nothing calls it from a parallel region today | reading back the change in 7 |
| 9 | **The weight-category columns are head counts and both reductions treat them as means**, so the server's chart of `normal_weight` has a meaningless level. Not fixed: the harness's reduction has to change with it, and that invalidates every stored reference | reading the reduction while fixing the lattice detector |

Defect 7 is the one worth dwelling on. **The byte-for-byte comparison that this project trusts more
than any statistical one did not find it** — a whole `KevinHall_FINCH` run was identical with the
defect present, because something else happened to have interned every name that run uses before the
models were built. What found it was a unit test that passed in debug and failed in release, for the
same reason: a different test had interned the name first. A check that depends on the order tests
run in is a check that can be right by accident, and this one was.

## The second fixture pack

`gen-fixtures` writes two configurations over one data store. They differ in file layout (model files
in subdirectories, named differently), output folder (nested three deep), output file name (carrying
a `{TIMESTAMP}` token, so it is neither `result.csv` nor the same name twice), scenario set (an
active intervention, so two scenarios rather than one), disease set (two, reordered, against three),
comorbidity count, seed, horizon, cohort fraction and age range.

**`FixturePack` deliberately carries no facts about a pack's contents.** A test that needs the
horizon or the scenario names asks the loaded configuration. That rule is the mechanism: it is what
stops a test asserting a constant only one pack satisfies, which is exactly what
`tests/engine/manifest_test.cpp` was doing when it named `result_manifest.json`, and what
`tests/sim/simulation_test.cpp` was doing when it named 2010–2014, fifty ages and three diseases.

One assertion is worth quoting because it was wrong in a way nothing would have caught: a
reproducibility test used 987654321 as "a different seed", which is the *second pack's own seed*.
Against that pack it would have compared a run with itself and passed.

**Two packs is not a proof.** A third would find things the second does not, and the real examples
find things neither does — the previous run's server defect came from `HLM_France`, not from a
fixture. What this buys is that the cheap, fast, always-run layer can no longer be satisfied by a
program that assumes one particular configuration.

## The end-to-end tests

Nineteen tests, about seven seconds, one spec per screen plus one for the journey across them: edit a
configuration, see a located diagnostic land on the field it names, start a run, watch it finish over
the event stream, open the results, download the CSV, find it in the history. Chromium only, serial,
one worker — one run at a time is the server's contract, not an accident of the configuration.

Nothing is mocked. `scripts/e2e-server.sh` lays out the two fixture packs and starts the real binary
with the real built frontend; a run of a synthetic pack takes about a fifth of a second, which is
what keeps this a thing that runs rather than a thing that is run.

Two things the suite reported were the tests being wrong rather than the code, and both are worth
knowing about this app: **all four screens are in the DOM at once**, hidden rather than unmounted,
so a bare locator matches screens nobody is looking at; and **a form section is a closed `<details>`**
until something in it is wrong, so a field has to be revealed before it can be typed into.

`scripts/check.sh` now runs the frontend too — type-check, unit tests, build, then the browser.
Until this run it verified nothing in `web/` at all, so "green at every commit" was a claim about the
C++ only.

## The lattice detector

The equivalence harness compares a quantile of a lattice-valued series with an exact test of the
counts rather than numerically, because the normal-theory allowance shrinks as 1/√n while the lattice
step does not. Its detector was asking the question of the **rate**: a disease rate reduces to total
cases over total head count, the cases are a small integer and the head count moves seed to seed, so
a series that is a handful of counts in disguise presented 43 to 79 distinct rates and neither rule
fired. Six comparisons on `HLM_India` at 60 seeds failed because of it.

It now asks the question of the numerator, bucketed at the baseline's printed precision exactly as
the reduced value was. **No threshold moved**, and that is checkable rather than asserted: if the
head count were the same in every seed, multiplying both the values and the scale by it would leave
every bucket where it was, so the change can only act where the denominator moves.

All four stored references re-scored:

| Example | Intervention | Comparisons, before | After | Out of tolerance |
|---|---|---:|---:|---:|
| `HLM_France` | `simple` | 31,468 | **31,546** | **0** |
| `KevinHall_FINCH` | `simple` | 22,679 | **22,616** | **0** |
| `HLM_India` *(reduced)* | `simple` | 67,885 | **66,787** | **0** |
| `HLM_India` *(reduced)* | `food_labelling` | 68,041 | **66,805** | **0** |

The counts move in both directions, which is the mechanism rather than noise: a series entering the
lattice class trades three quantiles and a standard deviation for one distribution test. India loses
1,236 comparisons — 412 series entering — and France gains 78, because France's cohort is nearly
seed-constant and most of its buckets do not move at all.

## What a deviation is worth, measured

Unchanged from the previous run, and re-confirmed by the re-scores above. With
`--baseline-compat all` the engine reproduces the baseline's deliberate deviations, so a comparison
tests everything except them; the harness then runs once more with the flags off and reports the
difference. Mean BMI of males in the intervention scenario, this build minus the baseline-compatible
one, over twenty seeds:

| | Largest | When | Relative |
|---|---:|---:|---:|
| `HLM_France` | **+0.0531** | 2037 | **+0.209%** |
| `HLM_India` *(reduced)* | **+0.0313** | 2050 | **+0.160%** |

**The deviation reaches much further than mean BMI.** On `HLM_India`, **194 series differ** and
12,532 agree to the printed precision — years of life lost, disability-adjusted life years, head
counts, and the prevalence and incidence of eleven diseases.

## Performance: names resolved at the call site

Backlog item 2, the half [ADR 0037](decisions/0037-index-keyed-risk-factor-store.md) left behind.
The store stopped comparing strings; its callers went on handing it a `core::Identifier`, and some of
them *constructed* one per factor per person per year from a string concatenation. Three places, all
of them "do it once when the model is built": the linear model's coefficient list holds each name's
index **and** the three name-shaped questions the evaluator used to ask of the string; the static
linear model builds its `<factor>_residual`, `_policy`, `_trend` and `_income_trend` names once; the
Kevin Hall model's food-to-nutrient and nutrient-to-energy equations are index-keyed.

<!--PERF-->

**The check that matters is byte identity**, not a statistical comparison over twenty seeds, which
would call a last-bit difference agreement. It is also the check that did not find defect 7 above.

## CI, per matrix entry

Fifteen jobs, two of them new this run.

<!--CI-->

## The recommended next run

**Answer the Kevin Hall intervention question, or decide not to** —
[docs/briefing.md](briefing.md) states it as a question for the upstream authors, and
[docs/backlog.md](backlog.md) item 1 is the work it would unblock. It is first because everything
above it is done and because it is the largest thing this build refuses that a user could reasonably
want: four of the six upstream examples can only be run with a no-op policy.

If that answer is not available, the next run is **item 2**: the weight-category columns are head
counts and both reductions treat them as means. It is four names in two places and a regeneration of
every stored reference, and until it is done the server draws a chart whose level means nothing. It
is the only correctness item this run found and did not fix.

Two smaller things would each remove a hedge from this document. **`HLM_India` at the cohort it
ships** (item 5) is machine time rather than work, and it is the largest single gap in the
validation. And **`DataSeries` keyed by channel name** (item 10) is what is left of the performance
item: a `KevinHall_FINCH` profile taken after this run's change still has `_platform_memcmp` as its
largest entry, and what remains of it is the analysis module looking channels up by `std::string`
rather than anything per person per year.

[docs/backlog.md](backlog.md) has the rest, ranked, with what each costs.

## What a reader should still be sceptical about

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
- **The harness has been wrong five times now** — a normal-theory allowance on a point mass, the same
  on a lattice-valued median, a detector that could not see a lattice in a numerator, a rule that
  checked half of its own justification, and this run's first attempt at the numerator fix. It has 48
  tests, which is better than nothing and is not the same as being right.
- **The reduction mislabels four columns**, and both the harness and the server's charting endpoint
  do it. It makes no comparison wrong and it makes those numbers meaningless
  ([docs/backlog.md](backlog.md) item 2).
- **The synthetic packs are invented.** Both of them; their `SYNTHETIC.md` says so. The second is not
  more realistic than the first, only *different*, which is the only property claimed for it.
- **Nineteen end-to-end tests is not coverage.** They cover each screen's principal job and the
  hand-offs between them, in one browser.
