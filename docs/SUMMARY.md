# Summary of the eighth build run

What was built, what is proven, and where it stops. Written at the end of the run it describes.
Earlier runs' summaries are in the history of this file: the first covered the HLM surface, the
second the FINCH one, the third the library split and the index-keyed store, the fourth `HLM_India`
and the first CI workflow, the fifth the local server, the frontend and switchable deviations, the
sixth a second fixture pack, a browser in CI, and nine findings, the seventh the weight categories,
the ThreadSanitizer split and the analysis module's channels. A comment in the code that cites
"`docs/SUMMARY.md`, finding N" means the run that wrote the comment; `git log -p docs/SUMMARY.md`
is where to find it.

## The short version

A deterministic C++20 reimplementation of the Health-GPS microsimulation, with the whole upstream
model surface implemented, three examples compared against the baseline, and three ways to use it:
as a library, from a command line, and from a browser.

**This run had one subject seen from six sides**: the previous run found that 45 columns of every
income-stratified result file were identically zero in this build and filled in the baseline's, and
that **nothing had ever compared those files at all**. The columns are filled, the comparison covers
every CSV a run writes, a script asks of the files themselves which columns are empty on each side,
a fixture produces every output family this engine can write, the server and the frontend can chart
a stratified series, and the whole lot runs in CI.

> **The check that would have caught it is the one that was missing, and it is not the comparison.**
> A statistical comparison cannot express "the baseline has numbers here and we have nothing": a
> column of zeros against a column of numbers either fails for ever, or — where the column is
> legitimately absent for an example — is skipped on both sides and says nothing. On the two HLM
> examples, where nobody has an income category, the stratum files are empty on *both* sides and
> agree about nothing. `scripts/column-coverage.py` asks the blunter question of the files
> themselves, and is a CI job.

| | |
|---|---:|
| Tests, C++ | **863** in 102 suites — 866 CTest entries — passing under release, debug, ASan+UBSan and TSan, where 773 of them run ([ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md)) |
| Tests, the equivalence harness's own | **63** (was 50) |
| Tests, the frontend | **48** unit, **21** end to end in a browser |
| Comparisons against the baseline this run | **297,494** over four stored references, **3** out of tolerance, plus a 60-seed confirmation of 112,871 — all four references regenerated |
| Source | `src/` 153 files; `tests/` 73 files; 46,678 lines of C++ between them; `web/src/` 18 TypeScript files and `web/e2e/` 6, 3,136 lines |
| Documents | **14**, plus **47 ADRs** |
| CI | **16 jobs** — see below |
| Findings this run | **4** |

## The seven tasks, and how each ended

| | Task | Outcome |
|---:|---|---|
| 1 | Orientation, pre-flight, CI on HEAD | **15 of 15 green** on `1ef6062`, run 35405669788, read per job with `gh`. |
| 2 | The column inventory and the coverage script | **Done.** `scripts/column-coverage.py`, and the starting state recorded: **198 findings** — 45 columns in each of `KevinHall_FINCH`'s four stratum files, and `mean_age`, `mean_age2`, `mean_age3` in each of the six stratum files of the two HLM examples. |
| 3 | The fixtures | **Done**, and it needed a new model family. The second pack's static model is now `StaticLinear` — the only family that assigns an income category, and the only one whose model file carries the region and ethnicity prevalence ([ADR 0047](decisions/0047-the-second-pack-carries-the-stratified-dimensions.md)). **Two findings.** |
| 4 | The harness over every file family | **Done.** Every CSV a run writes is reduced and compared, with its own counts, and a family one side writes and the other does not is a failure rather than a skip. All four references regenerated against the baseline binary. |
| 5 | The 45 columns | **Done.** `column coverage: PASS` on all three examples, every all-zero count matching the baseline's. The whole-population CSV is **byte-identical** before and after on both examples that have one. **One finding**, and it belongs to upstream. |
| 6 | The coverage CI job | **Done.** `column coverage · three examples`, sixteenth job, and a step in `scripts/check.sh`. |
| 7 | The server and the frontend | **Done.** `GET /api/runs/{id}/summary?family=…`, a selector on the results screen, two more end-to-end tests. |

## The four findings, and what found each

| | What | Found by |
|---|---|---|
| 1 | **`AssignedAttributes::region` and `::ethnicity` were never set by anything**, so the two branches in `initialise_output_channels` that read them had never fired. `KevinHall_FINCH`'s `mean_region` column is there because its config declares `Region` as a level-0 risk factor and the mapping loop adds it, not because of the branch meant to decide it | giving the second fixture pack a region and finding it had no column |
| 2 | **Classifying a stratum file by the *shape* of its CamelCase suffix is wrong.** The second pack's configured output name is `synthland_{TIMESTAMP}_B.csv`, and `_B` is a CamelCase suffix that is not a family | the enumeration test counting four stratum files where the pack writes three |
| 3 | **Every demographic standard deviation that is also a declared risk factor has its square root taken twice in the baseline.** `std_region` is 0.122097 in a band of 50 whose spread is 0.745, and 0.122097 is `sqrt(0.745/50)`. The finishing loop walks the mapping and then a fixed list of demographic names, and a name in both is finished twice | writing the stratified standard-deviation pass beside the baseline's and asking why the two loops overlap |
| 4 | **The comparison's allowance is estimated from the same twenty draws it is judging**, so a series at a small signed offset well inside its allowance fails in *every year at once* when a seed set gives a tight sample. It is why the failure count on one example ranges from 0 to 45 across equally valid seed sets, and it is a property of the whole-population comparison that is older than this run | three comparisons out of tolerance at twenty seeds, four at sixty and no cell in common — then re-scoring the sixty-seed run over subsets of itself, which contradicted the obvious explanation |

Finding 3 is reproduced here rather than fixed, and [docs/upstream-reports.md](upstream-reports.md)
is the fifth report. A standard deviation is a number somebody may have published; changing it is a
decision that belongs with the model rather than with a reimplementation, and the harness compares
these files column for column, so a quiet correction would be a difference nobody chose.

## What the 45 columns were, and what they are now

`calculate_income_based_series` accumulated `count`, the factor means, `mean_income`,
`mean_physical_activity` and the diseases' prevalence and incidence, and nothing else. The stratum
files carry the whole-population file's header and the writer writes a zero for a channel with no
stratified counterpart — which is right for a channel that has none and wrong for one that should.

It is now the whole-population series of `series.cpp`, per stratum, written to be read beside it:
the same two passes, the same denominators, the same resolve-once discipline. What that added:

| | Columns | Where they come from |
|---|---:|---|
| `deaths`, `emigrations` | 2 | somebody who left this year, counted in the stratum they were in — this file skipped every inactive person outright |
| `mean_yll`, `mean_yld`, `mean_daly` | 3 | over the living **plus** the year's dead, which is the denominator the whole-population series uses |
| `mean_age`, `mean_age2`, `mean_age3` | 3 | the row's own key, written for **every** configured stratum whether or not anybody is in it |
| `mean_gender`, `mean_region`, `mean_ethnicity`, `mean_income_category` | 4 | read off the person rather than out of the factor map |
| the `std_` columns | 33 | a second pass over the population, from the finished mean |
| **total** | **45** | |

**All 45 are matched. None is a flagged deviation and none is a documented exclusion**, which is the
tally the ruling asked for: this build had no number where the baseline had one, so there was nothing
to differ about on purpose. The two documented exclusions this example has are unchanged and belong
to the whole-population file and to a family, not to these columns: `std_income` (**B-22**, the
baseline emits the column and never fills it) and the `IndividualIDTracking` family
([docs/backlog.md](backlog.md) item 2).

The check that says so is not one test but three:

- **`column coverage: PASS`** on all three runnable examples: every all-zero count now matches the
  baseline's, family by family ([docs/equivalence.md](equivalence.md)).
- **The comparison**, which now covers those files: `KevinHall_FINCH` went from 22,616 comparisons
  to **111,836**, and from **15,390 out of tolerance to 3**.
- **Eleven unit tests** over a cohort small enough to add up in the head
  (`tests/model/analysis_income_test.cpp`), which is the only way to say a column means what the
  baseline's definition says rather than that it agrees with a number we also produced.

**And the whole-population CSV did not change**: byte for byte, `HLM_France` and `KevinHall_FINCH`,
before and after.

## The three marginal comparisons, and what they turned out to be

`KevinHall_FINCH` is the one example whose stratum files have numbers in them, so it is where the
change to what is compared shows: **22,616 comparisons became 111,836, and 15,390 out of tolerance
became 3.** The three are the only thing in this run that is not simply better than before, so they
get the space.

**They are not reproducible.** At 60 seeds there are 4 out of 112,871 — and **not one of them is a
cell that failed at 20**. A defect fails harder with more seeds; these move. The whole-population
file is 0 of 22,616 at 20 seeds and 0 of 22,715 at 60, with no budget at all.

**The obvious explanation was wrong, and the measurement that showed it is the finding.** The first
reading was that these are stratified low-count series — a rate or a spread built from a handful of
events inside one income stratum, where income category is itself a draw. It fits every cell that
failed. Re-scoring the 60-seed run over 20-seed subsets of itself says otherwise:

| 20 seeds drawn from the 60 | Out of tolerance |
|---|---|
| seeds 1–20 (what `check.sh` runs) | **3** |
| seeds 21–40 | **1** |
| seeds 41–60 | **1** |
| 100 random draws of twenty | min **0**, median **2**, mean **3.4**, max **45**; **28 of 100** had none |

**The worst draw's 45 failures are 32 in one whole-population series** —
`result/std_polyunsaturatedfattyacid`, 21 years of it on the mean and 11 on the median — with seven
more in `result/std_fat`. Six groups in all, in the file this project has been comparing for eight
runs. The mechanism is the allowance: it is `4.5 × sqrt((s_b² + s_n²)/n)`, estimated from the same
twenty draws it is judging, so a tight sample shrinks it and any small **signed** offset in that
series fails in every year at once. `std_polyunsaturatedfattyacid` sits about **1.1%** below the
baseline's while its mean agrees to **0.109%**, and it uses 0.60× of its allowance over all 60 seeds.

So this run **spends a failure budget** — the first since the fifth run: `--max-failures 3` on
`KevinHall_FINCH` and zero everywhere else, in `scripts/check.sh` and in the CI matrix entry, sized
at exactly what this build produces at the seeds the check runs and therefore failing on any
increase. What it hides is stated where it is set. [docs/backlog.md](backlog.md) item 6 is the work
that removes it, and it is statistics rather than a constant — **the sigma limit was deliberately
not touched**, because re-deriving it clears one of the three cells and neither 60-seed one, makes
the threshold stricter for the smaller sweeps where `HLM_India` sits at 0.987× of its allowance, and
does nothing about a whole series failing together.

## The harness now compares every file a run writes

`find_result_csv` existed precisely to *exclude* the stratified files. It is gone; `result_families`
returns every CSV by family, the reduction's key begins with the family, and the report has a
section per family with its own counts. Three decisions inside that are worth stating because none
of them is a consequence of the others ([docs/equivalence-method.md](equivalence-method.md) §2.1):

- **A rate's head count is its own family's.** The lattice detector rebuilds a count-weighted
  variable's numerator as `value × count`; reading the whole population's count for a stratum's rate
  would give it a number two or three times too large and classify the series wrongly.
- **The emptying-band exclusion comes from the whole-population file and applies to every family.**
  A band is excluded because immigration cannot refill it once it empties, so the two
  implementations' *cohorts* disagree there (B-21). A stratum band being empty is not that, and
  excluding those would drop the stratified output of every band nobody is in — which on the two
  HLM examples is every band there is.
- **A family one side writes and the other does not is a failure, not a skip.** The one recorded
  exception is the baseline's individual-tracking file, and its premise is checked rather than
  trusted: the exclusion holds only while that file is *empty*, and turns back into a failure with
  its reason if upstream ever puts a row in it.

The reference format gained a `family` column, so all four stored references were regenerated
against the baseline binary. A reference written before the change is refused **by name** rather
than failing on its first row.

## The fixtures, and the model family they needed

Neither synthetic pack assigned an income category, so no test that ran a configuration could reach
`calculate_income_based_series` at all. That is not a missing test — it is a place with nowhere to
put one, because `person.income` is assigned by the `StaticLinear` family and by nothing else, and
`RegionFile` and `EthnicityFile` are read from a `StaticLinear` model file and nowhere else.

So the second pack's static model is now `StaticLinear`, with a categorical income model per
category, region and ethnicity prevalence tables, a `RuralPrevalence` block and a simple
physical-activity model — and its project requirements switch on every dimension those make
possible ([ADR 0047](decisions/0047-the-second-pack-carries-the-stratified-dimensions.md)). It keeps
its `EBHLM` dynamic model, because its active intervention has to reach a dynamic model that applies
it (B-25). The first pack is unchanged and assigns none of this, which is what makes
`TestSimulation.AChannelExistsOnlyWhenAModelActuallyAssignsIt` a test of **both** halves of its rule
rather than only the half that says "no".

`hgps::api::all_output_families()` is the enumeration, in the public header because it is part of
what a caller is told about a run, and
`OutputFamilies.EveryFamilyTheEngineCanWriteIsProducedByAFixture` fails if one has no pack behind
it. Nothing enumerated that before, so "is every output covered?" was not a question anything could
be asked.

## The server and the frontend

`GET /api/runs/{id}/summary?family=…` reduces any of a run's CSVs; `families` is in every response
whichever one was asked for, so a selector needs no second request, and an unknown family is a 400
naming the ones the run did write. The results screen has a **File** selector beside **Run** and
**Sex**, disabled when there is one file to choose from, and the reduction note names the file it
reduced rather than leaving the reader to trust the selector.

`RunRecord::result_csv` used to pick "the shortest `.csv` name", which is true of every file this
engine writes and is not a rule anything enforces. It classifies by the engine's own rule now.

## CI, per matrix entry

CI_TABLE_PLACEHOLDER

## The same tree locally

CHECK_TABLE_PLACEHOLDER

## The recommended next run

NEXT_RUN_PLACEHOLDER

## What a reader should still be sceptical about

- **India was compared at a hundredth of its cohort**, 12,406 people rather than 1,240,613. Nothing
  in the India result is evidence about the example as shipped
  ([docs/backlog.md](backlog.md) item 4).
- **The two HLM examples' stratum files are empty on both sides.** Nobody in them has an income
  category, so their stratified comparison tests the head counts and nothing else, and it should not
  be read as a check on the 45 columns. `KevinHall_FINCH` is the only example that checks those, and
  it is one country and one pack.
- **Population impact fraction has never met the baseline.** Only the synthetic pack exercises it
  end to end; the one example that uses it cannot run.
- **Four of the six upstream examples can only be compared with `simple` active**, because an
  intervention on the Kevin Hall surface is a no-op upstream (B-25) and this build refuses the
  configuration rather than running it silently.
- **The comparison's floor.** The baseline writes six significant digits, so no comparison is
  tighter than about 10⁻⁵ relative.
- **The harness has been wrong five times**, and its reduction was wrong about four columns until
  the previous run. It has 63 tests, which is better than nothing and is not the same as being
  right. What this run added to it — a family in every key, a per-family section, a family-presence
  check — is thirteen more tests over machinery that is one run old.
- **The second synthetic pack is a `StaticLinear` model paired with an `EBHLM` dynamic one**, which
  is a combination no upstream example uses. It is a combination this engine accepts, and a fixture
  is the right place to find out that it works; it is not evidence about any real configuration.
- **Nineteen end-to-end tests was not coverage and twenty-one is not either.** They cover each
  screen's principal job and the hand-offs between them, in one browser.
