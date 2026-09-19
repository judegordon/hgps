# Summary of the tenth build run

## State of the project

For somebody opening this repository cold, in five sentences.

A deterministic C++20 reimplementation of the Health-GPS microsimulation — the whole upstream model
surface, usable as a library, from a command line and from a browser — written to be compared
against the original run for run. **What is proven** is that three of the six upstream examples run
in both implementations and agree statistically, at 62,030 tests over two examples' stored
references this run with none failing and no failure budget, on a build whose 885 C++ tests pass
under release, debug, ASan+UBSan and ThreadSanitizer with 16 of 16 CI jobs green. **What is open**
is scale and coverage rather than correctness: `HLM_India` has only ever been compared at a
hundredth of the cohort it ships, population impact fraction has never met the baseline because the
one example that uses it cannot run, and no comparison here is tighter than the six significant
digits the baseline prints. **What needs an upstream decision** is two things, both of them
blocking: where a policy should apply on the Kevin Hall surface, which leaves four of the six
examples runnable only with a no-op intervention, and whether the newborn weight bound or the
quantile curve is wrong in `KevinHall_India`, which stops the other two in their first simulated
year. Read [docs/READING-GUIDE.md](READING-GUIDE.md) if you are going through the repository end to
end, [docs/briefing.md](briefing.md) if you have twenty minutes, and the rest of this file for the
numbers behind every claim above.

## What this file is

What was built, what is proven, and where it stops. Written at the end of the run it describes.
Earlier runs' summaries are in the history of this file: the first covered the HLM surface, the
second the FINCH one, the third the library split and the index-keyed store, the fourth `HLM_India`
and the first CI workflow, the fifth the local server, the frontend and switchable deviations, the
sixth a second fixture pack, a browser in CI, and nine findings, the seventh the weight categories,
the ThreadSanitizer split and the analysis module's channels, the eighth the 45 empty columns and
the comparison over every output family, the ninth the comparison's false-positive rate. A comment
in the code that cites "`docs/SUMMARY.md`, finding N" means the run that wrote the comment;
`git log -p docs/SUMMARY.md` is where to find it.

## The short version

A deterministic C++20 reimplementation of the Health-GPS microsimulation, with the whole upstream
model surface implemented, three examples compared against the baseline, and three ways to use it:
as a library, from a command line, and from a browser.

**This run answered the one question the project could not answer about itself.** The ninth run
found a seed on which this build's energy balance diverged to −1.7×10²⁸³ kg and the baseline
completed, and could not say whose instability it was. It is the **model's**, it is **four seeds in
five hundred rather than one**, and on three of those four **both implementations complete and
write the impossible person out**.

> **The term that fails is not the one the run stops on.** `p = C / (C + F)` is a coefficient of
> the relaxation the yearly step solves, and it has a pole at **F = −2.001012658227848 kg**. Body
> fat goes negative a year before anything breaks, and neither implementation looks: both floor a
> negative estimate at *initialisation*, with a comment saying it is to keep `p` finite, and
> neither floors it in the update. Past the pole `p`, the determinant and the time constant all
> change sign, and `exp(−365/tau)` at half a day of negative `tau` is 652 e-foldings. The
> baseline's own statements, in a program sharing no code with this project, return
> `-1.7019180456941172e+283` against this build's `-1.7019180456941046e+283`.

| | |
|---|---:|
| Tests, C++ | **885** in 105 suites — **889** CTest entries — passing under release, debug, ASan+UBSan and TSan, where **794** of them run ([ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md)) |
| Tests, the equivalence harness's own | **94** |
| Tests, the frontend | **48** unit, **21** end to end in a browser |
| Comparisons against the baseline this run | **62,030** tests over two examples' stored references, **0** failures, **no budget** — plus a **1,000-run census** of both implementations and **1,060 paired runs** compared byte for byte |
| Source | `src/` 153 files, `include/` 6; `tests/` 74 files; 48,432 lines of C++ between them; the equivalence harness 4,656 lines of Python in seven files; `web/src/` 18 TypeScript files and `web/e2e/` 6, 3,136 lines |
| Documents | **16**, plus **1 finding write-up** and **50 ADRs** behind an index `scripts/check.sh` keeps in sync |
| CI | **16 jobs** — see below |
| Findings this run | **6** |

## The seven tasks, and how each ended

| | Task | Outcome |
|---:|---|---|
| 1 | Orientation, pre-flight, CI on HEAD, the 500-seed sweeps started | **16 of 16 green** on `7ee75ba`, run 35437864699, read per job with `gh`. Both censuses started before anything else, and they were the run's long pole exactly as expected: 88 minutes for the baseline's 500 seeds and 37 for this build's 1,000 runs. |
| 2 | Reproduce and trace seed 80 | **Done, and the answer is that it is not ours.** Traced term by term in a separate build tree so the running census was not disturbed; the mechanism is a pole in the model's own partition coefficient, and the baseline's own statements reproduce the number digit for digit. [docs/findings/seed-80.md](findings/seed-80.md). **Two findings, one of them a retraction of the ninth run's.** |
| 3 | Fix, classified | **ADR 0041's third case**: an instability the fitted model admits and the baseline does not mask. A bounded guard, flag **B-29**, 15 tests. Byte-identical on every reference seed of all three examples against the binary that predates it. |
| 4 | The invariant guard | **Done, and it found a second baseline defect on the way** — the `NaN`-to-zero substitution the guard replaces. Flag **B-30**, 7 tests, [ADR 0050](decisions/0050-no-output-carries-a-number-that-cannot-exist.md). |
| 5 | The sweep results | **1,500 runs.** Table below. The headline is that four seeds in five hundred reach the boundary and only one of them fails, so the silent case is three times as common as the loud one. |
| 6 | Equivalence after the fix | All stored references re-scored and none fails; **60 of 60** reference-seed runs byte-identical to the pre-fix binary across every output family; **496 of 499** census seeds byte-identical with the deviation on and off. |
| 7 | Docs | Two ADRs, a findings write-up, [docs/upstream-reports.md](upstream-reports.md) grown from five reports to seven, deviations, briefing, equivalence, design, and a backlog whose only correctness item is now closed. |

## The six findings, and what found each

| | What | Found by |
|---|---|---|
| 1 | **The seed-80 divergence is the model's, not this implementation's, and the term that fails is body fat rather than weight.** `p = C / (C + F)` has a pole at −2.001 kg; body fat reaches −2.224 kg a year before the weight reaches −1.7×10²⁸³ kg. The baseline's own statements, compiled alone, return the same numbers to fifteen significant figures | instrumenting the energy balance for one person in a separate build tree, then compiling the baseline's arithmetic by itself and feeding it the state that came out |
| 2 | **Four seeds in five hundred, not one — and three of them are silent.** Body fat goes below zero on seeds 80, 143, 178 and 208; only 80 passes the pole. On the other three the unguarded run completes and writes the person out, which is the case that reaches a results file | a census that read every output file of every run rather than only the exit code, because a weight above the configured maximum leaves both implementations exiting zero |
| 3 | **The analysis module counts a `NaN` risk factor as zero in the year's means, and says nothing.** An infinity it does not look at at all. It is the last place a value passes through before becoming output and the last that knows whose it is, so it converts every upstream defect into a quietly wrong mean | asking what *should* have caught finding 1, and reading the code that would have |
| 4 | **The ninth run's "three-year precursor" is withdrawn.** It was a maximum over a three-person band of 96-year-old men in the intervention arm; it happens on seeds that never diverge; and in the baseline arm the band means are pinned flat by the weight calibration, identical to the last printed digit across seeds. The true statement is worse — the aggregate shows nothing at all | trying to quote the previous run's numbers and failing to reproduce them |
| 5 | **The weight calibration actively hides a diverging person.** It sets each (sex, age) band's mean weight to the `FactorsMean` table, so one man's 22 kg deficit is not merely diluted among his 46 band-mates — it is *spread onto them*, and the reported mean is restored exactly. Only the dispersion moves, which is why the three seeds the fix changes move `std_weight` and `std_bmi` and no mean at all | comparing the same band's mean across seeds that do and do not diverge |
| 6 | **The baseline's signal flake is one run in 250, not one in 45.** Two of 500 single-threaded `KevinHall_FINCH` runs exited on a signal and both succeeded on the retry, against the 4 of 180 the audit measured | running the baseline five hundred times, which is more than twice what anyone had |

## The census: 1,500 runs

Five hundred seeds of `KevinHall_FINCH` per column, every output file of every run read rather than
only the exit code (`tests/equivalence/seed_scan.py`).

| Over 500 seeds | this build, before | this build, after | the baseline |
|---|---:|---:|---:|
| runs refused | **1** — seed 80 | **0** | **0** |
| non-finite value in any output file | 0 | 0 | 0 |
| a weight above the configured maximum, exiting zero | 1 — seed 48 | 1 — seed 48 | 0 |
| a body fat mass below zero | **4** — seeds 80, 143, 178, 208 | 4, every one bounded and named in the manifest | not measurable from outside: identical arithmetic, and nothing in it looks at body fat |
| runs lost to the baseline's signal flake | — | — | **2**, both succeeding on the retry |

**The baseline's zero is a measured zero rather than a broken detector.** Running
`KevinHall_FINCH` with `Weight.range` set to `[1, 50]` makes the baseline print **67,668**
`[WEIGHT RANGE WARNING]` lines, write its results and return **0** — which is both the positive
control for that column and the reason report 6 matters at four seeds in five hundred.

### What the guard is worth, over every seed

Each of the 500 seeds was run twice, with and without `--baseline-compat B-29`, and the two runs'
output files compared byte for byte:

| | |
|---|---:|
| seeds where both runs complete | 499 |
| **byte-identical** | **496** |
| moved | **3** — seeds 143, 178, 208 |
| completes only with the guard | 1 — seed 80 |

| Seed | cells differing, of 586,080 | largest | where |
|---|---:|---:|---|
| 143 | 7 | 0.76% | `std_bmi`, males aged 31, 2032 |
| 178 | 27 | 0.09% | `std_weight`, males aged 40, 2029 |
| 208 | 28 | 0.85% | `std_weight`, males aged 25, 2029 |

**Every differing cell is a `std_`, and no mean moves at all** — finding 5, and the first thing the
dispersion test the ninth run added could have caught and nothing else could.

### Byte identity against the binary that predates the fix

Rebuilt from the commit before it, so it does not know `B-29` or `B-30` exist:

| Example | seeds | identical | differ |
|---|---:|---:|---:|
| `KevinHall_FINCH` | 1–20 | **20** | 0 |
| `HLM_France` | 1–20 | **20** | 0 |
| `HLM_India` *(at 1e-5)* | 1–20 | **20** | 0 |

Sixty runs, every output family each, re-checked on the final tree with both guards in. No stored
reference can have moved, and the harness runs in `scripts/check.sh` confirm it rather than
inferring it.

## The two guards, and why they differ

They are the same run's work and they behave oppositely on purpose.

| | the energy balance's bound (ADR 0049) | the output invariant (ADR 0050) |
|---|---|---|
| when | a year's step would take body fat below zero | a value is about to become a mean and is not finite, or not a description of a person |
| what it does | integrates the year **only to the instant fat reaches zero** — `exp(-t*/tau) = F* / (F* - F0)`, the model's own trajectory at the edge of the model's own domain — and carries on | **stops the run**, naming the person, the year and the term |
| why the difference | something downstream knows what should have happened: the model's own solution, evaluated where it is still defined | nothing knows what a `NaN` BMI should have been, and choosing a correction would be a modelling decision made in the wrong place |
| flag | `B-29` | `B-30` |

That rule — *bound and record where something knows the answer, stop where nothing does* — is the
part of this run worth keeping if the rest is forgotten.

## What went upstream

[docs/upstream-reports.md](upstream-reports.md) went from five reports to seven, and the two new
ones are the two worth sending first, because both are defects in code that is still running
upstream rather than questions about a fitted model.

| | |
|---|---|
| **6** | the energy balance admits a body fat mass below zero and overflows a year later. Reproducible from the state in the report without a seed, because your cohorts are not ours. The reason it matters at four in five hundred is the sign: `validate_weight_in_config_range` throws below the configured minimum and only **warns** above the maximum, so a runaway upward exits zero with the number in the file |
| **7** | a risk factor that is `NaN` is counted as zero in the year's means, and an infinity is not looked at at all |

## The tool the census needed

`run.py` compares two implementations; `sweep.py` stores many seeds' reductions so a study can
score them many ways. Neither answers "did this run finish, and is what it wrote sane?" over
hundreds of seeds, and two things about that question are awkward:

- **the two sides must be scanned independently.** `sweep.py --tolerate-failures` drops a failing
  seed from *both*, because its two samples have to be the same set of runs. A census of which
  seeds each implementation refuses must not — "the baseline completes seed 80" is the fact being
  established;
- **a run that finishes still has to be read.** Both implementations only warn above the
  configured maximum weight, so a runaway of the upward sign leaves no trace in an exit code.

So `tests/equivalence/seed_scan.py`: many seeds of one implementation, every output file scanned
for a non-finite or impossible value, the log and the run metrics read for the events that do not
stop a run, and the manifest read for a guard's located warnings. With `--against` it runs each
seed twice under two compatibility sets and compares the files byte for byte, which is how a
deviation's effect over five hundred seeds becomes a measurement.

**One thing it deliberately does not do**, and the reason is in the code: the baseline is retried
three times and this build once. The baseline exits on a signal about one FINCH run in 250, and
counting that as "the baseline refuses this seed" would fill a census of *deterministic* refusals
with a defect that is already recorded. This build has no such flake and gets one attempt, so a
refusal here is deterministic by construction — which is what makes "seed 80 fails here and not
there" a fact about the model rather than about luck.

## CI, per matrix entry

Sixteen jobs. Run **35452631992** on `80a211b`, this run's final tree, every entry read with
`gh run view` rather than from the run's own summary. **16 of 16 success.** The last column is the
same job on `9e0c724`, the commit carrying every line of code this run wrote, run 35449681187 —
also 16 of 16, as was run 35446674088 on the fix before it.

| Job | Result | Time | The previous run |
|---|---|---:|---:|
| `linux · clang · release` | **success** | 6m17s | 6m16s |
| `linux · clang · debug` | **success** | 20m04s | 20m30s |
| `linux · clang · asan-ubsan` | **success** | 28m05s | 29m05s |
| `linux · clang · tsan` | **success** | 16m35s | 15m02s |
| `linux · gcc · release` | **success** | 5m24s | 5m17s |
| `linux · gcc · debug` | **success** | 13m22s | 9m47s |
| `macos · appleclang · release` | **success** | 4m48s | 5m00s |
| `macos · appleclang · debug` | **success** | 17m31s | 17m03s |
| `macos · appleclang · asan-ubsan` | **success** | 41m54s | 46m02s |
| `macos · appleclang · tsan` | **success** | 32m22s | 25m12s |
| `equivalence · HLM_France · 20 seeds` | **success** | 7m40s | 6m57s |
| `equivalence · KevinHall_FINCH · 20 seeds` | **success** | 8m56s | 9m05s |
| `column coverage · three examples` | **success** | 3m38s | 3m53s |
| `web · typecheck, test, build` | **success** | 0m10s | 0m14s |
| `web · end-to-end` | **success** | 3m05s | 3m07s |
| `performance · linux · indicative` | **success** | 2m49s | 5m26s |

**Nothing in this table is this run's cost.** The two AddressSanitizer jobs are *shorter* than the
previous commit's, Linux by a minute and macOS by four; `macos · appleclang · tsan` is seven
minutes longer and `linux · gcc · debug` three and a half, in the other direction, on code that
did not change between the two runs at all. Twenty-two new tests are 14 milliseconds of arithmetic
and one 23-millisecond cohort. What moves these numbers is runner weather, and the three runs
above show it moving them both ways on identical code.

This table describes `80a211b`. The one-line commit that installs it starts one more run, on the
same code and the same matrix — a fixed point a summary of its own tree cannot reach. What is
quoted is the newest run that had reported, and that run is on the tree everything but this
paragraph is in.

## The same tree locally

`scripts/check.sh` with nothing skipped, on the tree these commits leave behind — **exit 0**:

| | Result | Time |
|---|---|---:|
| release | **889 / 889** | 54 s |
| debug | **889 / 889** | 511 s |
| asan-ubsan | **889 / 889** | 1,620 s |
| tsan | **794 / 794** | 972 s |
| frontend, type-check and unit | **48 / 48** | under a second |
| frontend, end to end in a browser | **21 / 21** | 7.2 s |
| equivalence, `HLM_France` | **16,486** tests over four families, **0** failed | — |
| equivalence, `KevinHall_FINCH` | **45,544** tests over five families, **0** failed, **no budget** | — |
| column coverage, three examples | **13 families** compared column by column, **0** findings | — |

The counts are **889** and **794** against the ninth run's 867 and 773: twenty-two new C++ tests,
all but one of them about the two guards. Eight over every branch of the energy balance's bounded
step, two pinning the seed-80 trajectory to the numbers the trace recorded, one driving a cohort
into the boundary through the real FINCH model with the flag on and off, seven over the output
invariant, two over the warning collector and one asserting a manifest says `"total": 0` rather
than leaving the field out.

## What a reader should still be sceptical about

- **The baseline's `0 of 500` is about its cohorts, not its arithmetic.** The two implementations
  draw different random streams, so no seed is the same cohort on both sides. What is established
  is that the *arithmetic* is identical — the baseline's own statements produce the same 10²⁸³ on
  the same state — not that the baseline would refuse a seed at the same rate. Nobody has
  instrumented the baseline to count how often its own cohorts reach the boundary, and that is
  the one measurement this run wanted and did not take.
- **Four in five hundred is a rate estimated from four events.** The interval around it is roughly
  1 in 170 to 1 in 1,700. It is four times better than the ninth run's one observation and it is
  still a small number of events.
- **The guard's bound is a modelling choice, even though it is the least of them.** Integrating to
  the zero crossing uses the model's own trajectory at the edge of the model's own domain and
  invents no coefficient — but freezing a person's composition there is still a decision nobody
  upstream has ratified, and a body that has run out of fat stays at exactly zero for the rest of
  the run. On the three seeds where it fires and the run completes either way it is worth at most
  0.85% of one band's `std_weight`, which is the honest size of it.
- **The output invariant's bounds are stated, not derived.** A gram and a tonne, 1 and 300 cm:
  they are far outside anything a cohort produces, which makes them safe, and they are also
  therefore weak. A weight of 900 kg passes.
- **`HLM_India` was compared at a hundredth of its cohort**, 12,406 people rather than 1,240,613,
  and it is now the top item on the backlog partly because it is where the boundary would next
  appear at a different scale.
- **Population impact fraction has never met the baseline.** Only the synthetic pack exercises it
  end to end; the one example that uses it cannot run.
- **Four of the six upstream examples can only be compared with `simple` active**, because an
  intervention on the Kevin Hall surface is a no-op upstream (B-25).
- **The comparison's floor.** The baseline writes six significant digits, so no comparison is
  tighter than about 10⁻⁵ relative.
- **The harness has been wrong six times**, and this run did not add a seventh — but it did find
  that a *previous run's finding* was an artefact of reading a maximum over three-person bands,
  which is the same class of mistake one level up. The thing that caught it was trying to quote
  the number rather than trusting it.
- **Nineteen end-to-end tests was not coverage and twenty-one is not either.**

## The recommended next run

**`HLM_India` at the cohort it ships** — backlog item 2, promoted from 5 now that the correctness
item above it is closed. It is machine time rather than work: twenty seeds of both implementations
at 1.24 million people is about a day, and it is the largest single gap in this project's
validation, because every India number quoted anywhere came from a hundredth of the cohort.

**It is also the cheapest real test of what this run found.** Four seeds in five hundred is 500
draws of one pack's 6,858 people. India at full scale is a hundred times the people per seed on a
different country's fitted intakes, and `seed_scan.py` answers "how often does the energy balance
reach its own boundary there?" in one command — which is the difference between "a property of
the FINCH pack" and "a property of the Kevin Hall model". The guard means those runs now finish.

**If a modelling answer arrives before then, item 1 still outranks it**: interventions on the
Kevin Hall surface are a question for the upstream authors, four of the six examples can only be
run with a no-op policy until it is answered, and it has been first on this list for five runs
because it is work nobody here can do.

**And the two new upstream reports are worth more than anything on the list.** They are defects in
code other people are running today, both reproducible from the report alone, and neither needs a
decision from anybody: one is a missing floor that the same file already applies fifty lines
earlier, and the other is deleting four lines that turn a `NaN` into a zero.
