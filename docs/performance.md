# Performance

Measured, not estimated. Two examples: the converted `HLM_France` reference example — 2010–2050, a
cohort of 6,244, 6 diseases, 11 risk factors — and the converted `KevinHall_FINCH` example —
2022–2032, a cohort of 6,817, 15 diseases, 34 risk factors, with the S1 policy model active in the
intervention scenario. Both run both scenarios, one trial run, seed 1, on:

| | |
|---|---|
| Host | Apple M5, 10 cores, 16 GB, macOS 26.6.2 — every figure here but the Linux ones below |
| Compiler | Apple clang 21.0.0, `-O3`, `-ffp-contract=off` |
| Baseline | `/tmp/hgps-build/baseline-release`, built as [docs/build-notes.md](build-notes.md) records |
| This build | `out/build/release`, `--threads 1` |

The exact configs are the ones `tests/equivalence/run.py` derives, so the two implementations are
given the same inputs in the same layout as in [docs/equivalence.md](equivalence.md). That matters
for reading the France figures: the derived config activates the `simple` intervention, so France
runs **two** scenarios here, where the shipped `examples/HLM_France/config.json` ships
`active_type_id` null and runs one. `scripts/measure.sh`, the Linux job below and the A/B in *Names
resolved at the call site* use the shipped config, which is why France is about 1.3 s there and
about 2.4 s here. FINCH ships an active intervention and runs two scenarios either way.

The three rows of each table below are **one session**, five runs of each binary, alternating the
three run by run so that any drift in the machine falls on all of them equally. All five are given,
because the spread is part of the measurement. `/usr/bin/time -l`, so peak memory is the maximum
resident set size.

A note on the machine: the first attempt at these numbers was taken while Spotlight was indexing
the working directories, and it put `HLM_France` at 3.38 s rather than 2.78 s — a 20% error, larger
than any difference discussed below. The figures here were taken after `mds_stores` went quiet.

## The numbers

**HLM_France**, 2010–2050:

| | Wall | CPU | Peak memory |
|---|---:|---:|---:|
| **Baseline** | 4.86 / 4.96 / 4.99 / 5.11 / 5.25 s | 7.76 / 7.96 / 7.99 / 8.04 / 8.11 s | 83.1–85.6 MiB |
| This build, before *names resolved at the call site* | 2.44 / 2.45 / 2.46 / 2.52 / 2.52 s | 2.43 / 2.44 / 2.44 / 2.50 / 2.50 s | 52.6–52.9 MiB |
| **This build** | **2.44 / 2.46 / 2.46 / 2.48 / 2.50 s** | **2.43 / 2.43 / 2.44 / 2.45 / 2.49 s** | **52.6–52.7 MiB** |

**KevinHall_FINCH**, 2022–2032:

| | Wall | CPU | Peak memory |
|---|---:|---:|---:|
| **Baseline** | 15.88 / 16.17 / 16.28 / 16.81 / 17.54 s | 25.00 / 25.35 / 25.62 / 25.78 / 25.97 s | 196.5–197.8 MiB |
| This build, before *names resolved at the call site* | 6.14 / 6.23 / 6.26 / 6.34 / 6.35 s | 6.12 / 6.21 / 6.22 / 6.32 / 6.33 s | 78.1–78.5 MiB |
| **This build** | **4.53 / 4.56 / 4.63 / 4.70 / 4.79 s** | **4.51 / 4.54 / 4.61 / 4.68 / 4.77 s** | **78.1–78.6 MiB** |

Against the baseline, best of five against best of five: on the HLM surface **2.0× faster in wall
time, 3.2× less CPU work, 37% less memory**; on the FINCH surface **3.5× faster, 5.5× less CPU, and
2.5× less memory** — running its two scenarios one after the other, on one thread, against a
baseline that runs them concurrently.

**The middle row is this run's change and nothing else**, so the two bottom rows of each table are
the A/B that *Names resolved at the call site* reports below, measured in the same session as the
baseline row rather than spliced in from another one. The earlier comparison — this build before and
after the index-keyed store — used to be the middle row here; it has its own table in *What the
index-keyed store bought*, because the binary it needs no longer exists and re-measuring it in this
session is not possible.

The CPU column is the one that says something about the code. The baseline's wall time is shorter
than its CPU time because it runs the baseline and intervention scenarios on separate threads; this
build runs them sequentially by design ([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)),
so its wall and CPU times are the same number. Sequential execution was expected to cost about a
factor of two in wall time and to be worth it for byte-identical output. It costs nothing on either
example, because the work itself is smaller.

### What the index-keyed store bought

This build immediately before [ADR 0037](decisions/0037-index-keyed-risk-factor-store.md) and
immediately after, measured on an idle machine in one session, alternating the two binaries run by
run so that any drift in the machine falls on both equally. It is **not** the same session as the
table at the top of this file — it is three runs older, and the pre-ADR-0037 binary is gone — so
read the ratio and not the absolute numbers:

| | Wall | Peak memory |
|---|---:|---:|
| `HLM_France` | 2.75 → 2.41 s, **1.14×** | 57.1 → 52.4 MiB, **−8%** |
| `KevinHall_FINCH` | 11.93 → 7.78 s, **1.54×** | 195.5 → 77.5 MiB, **−60%** |

The difference between the two examples is the point rather than a curiosity: a FINCH person carries
many times the risk factors a France person does, so FINCH did many times as many lookups per person
per year and had many times as many red-black tree nodes to allocate. A store that replaces a tree of
string comparisons with a flat vector of integers therefore pays over and over on FINCH and once on
France.

**How many times over was stated wrongly here, and the correction matters.** This section used to say
"France's people carry 11 risk factors and FINCH's carry 55", which are the counts the two configs
*declare*. Instrumenting the store for one whole run of each says what a person actually holds:

| | Longest vector a person holds |
|---|---:|
| `HLM_France` | **6** |
| `KevinHall_FINCH` | **121** |

A fifth of the declared count on one and more than double it on the other. The ratio between the two
examples is therefore about twenty, not five, which is both why FINCH gained 1.54× where France gained
1.14× and why the scan those vectors were searched with had to be reconsidered — see
*A bounded search for the long vectors* below.

### A bounded search for the long vectors

The profile taken after the index-keyed store put `FactorValues::position_of` at **18.0% of all
samples** on `KevinHall_FINCH`, the largest single item in it. At 121 entries a linear scan averages
sixty integer comparisons, once per factor per person per year.

[ADR 0040](decisions/0040-a-bounded-search-for-the-long-vectors.md) scans a vector of sixteen entries
or fewer and binary-searches a longer one over the bounded prefix `[0, index]`. Five runs of each
binary, **alternating run by run** so that drift falls on both equally:

| | CPU, best of 5 | Median of 5 | Peak memory |
|---|---|---|---|
| `HLM_France` | 3.13 → 3.37 s | 3.48 → 3.63 s | 52.7 → 53.2 MiB |
| `KevinHall_FINCH` | 10.65 → **7.38 s, 1.44×** | 11.04 → 8.39 s | 78.2 → 78.1 MiB |

**These figures were taken on a machine that was not idle**, and the absolute numbers are worse than
the table at the top of this file for that reason: Android Studio was using 346% of CPU and Spotlight
was indexing, which is the same effect this document already records as worth 20%. The *relative*
figure survives it, because the two binaries were interleaved and met the same load.

France's apparent 7% regression is measurement noise, and it is worth saying why that is a fact rather
than a hope: France's longest vector is 6, which is below the sixteen-entry threshold, so after this
change France executes **character-for-character the scan it executed before**. There is no mechanism
by which it can have got slower. `HLM_India` carries the same 11 declared factors as France and is the
example where a constant factor is paid 1.24 million times a year, which is why the threshold exists
at all rather than binary-searching everything.

**And the output did not change.** Not statistically: *byte for byte*. The result CSV and every
income-stratified CSV of both examples are identical before and after, because the one place that
multiplies over a person's factors now iterates a name-ordered list built once per disease model
instead of the person's own map, so the same numbers are multiplied in the same order. Both stored
equivalence references therefore remain valid, and both comparisons were re-run to confirm it: 31,468
and 22,679 comparisons, zero out of tolerance, the same counts as before.

That check earned its keep immediately. The first version of the change made
`Person::try_risk_factor_value` ask for a name's index *before* the lazily built predictor table had
interned the nineteen derived-predictor names — so on the very first call of a run, `age` looked
un-interned and went straight past the dispatcher to the fallback resolver. `HLM_France` moved by a
last bit and `KevinHall_FINCH` did not, because the window depends on which name a run happens to
resolve first. Nothing else would have caught it: the suite passed, and a statistical comparison over
twenty seeds would have called a last-bit difference agreement.

### Names resolved at the call site

The item [docs/backlog.md](backlog.md) carried for three runs, and the half
[ADR 0037](decisions/0037-index-keyed-risk-factor-store.md) left behind. The profile after ADR 0040
put about **31% of the `KevinHall_FINCH` run** in names being resolved where they were used: a hash
probe per coefficient per person per year, three string predicates on the same coefficient, and —
worst of it — an `Identifier` *constructed* per factor per person per year from a string
concatenation, which is what `validate_identifier` and `chars::is_alnum` at 8.3% of a profile mean.

That 31% is the *call-site* part only. Name handling of every kind is a larger number — the
whole-run profile in *After the call-site change* puts it at 65.6% — and the two are not the same
measurement:
the 31% counted a narrower set of symbols over a five-second window. What the change was aimed at is
the 31%.

Three places, all of them "do it once when the model is built":

- `LinearModelParams` carries a `ResolvedPredictor` per coefficient, in the map's own order, which is
  the summation order. It holds the risk-factor index, the age power, and whether the name is the
  `gender2` dummy or one of the five metadata rows. `evaluate_linear_model` reads no string at all
  unless the missing-predictor fallback is reached.
- `StaticLinearModel` builds its `<factor>_residual`, `_policy`, `_policy_residual`, `_trend` and
  `_income_trend` names once instead of concatenating and validating them per person per year.
- `KevinHallModel` resolves the food-to-nutrient and nutrient-to-energy equations to indices, and
  `FactorValues` gains `at_index_or_insert` so the writing half of that loop needs no name either.

Five runs of each binary, **alternating run by run** so that any drift in the machine falls on both
equally, on an idle machine. These are the two bottom rows of the tables at the top of this file —
the equivalence-derived configs, both scenarios on both examples:

| | Wall, best of 5 | Median of 5 | CPU, best of 5 | Peak memory |
|---|---|---|---|---|
| `HLM_France` | 2.44 → 2.44 s, **1.00×** | 2.46 → 2.46 s | 2.43 → 2.43 s | 52.7 → 52.6 MiB |
| `KevinHall_FINCH` | 6.14 → **4.53 s, 1.36×** | 6.26 → 4.63 s | 6.12 → **4.51 s, 1.36×** | 78.2 → 78.2 MiB |

The same A/B on the shipped `examples/*/config.json` — what `scripts/measure.sh` and the Linux job
run, where France runs one scenario rather than two:

| | Wall, best of 5 | Median of 5 | Peak memory |
|---|---|---|---|
| `HLM_France` | 1.28 → 1.28 s, **1.00×** | 1.28 → 1.29 s | 42.7 → 42.7 MiB |
| `KevinHall_FINCH` | 6.08 → **4.50 s, 1.35×** | 6.11 → 4.57 s | 78.2 → 78.2 MiB |

1.36× and 1.35× on two different configurations of the same example, which is the only thing the
second table is for.

**Memory does not move**, and it should not have: the change stores one small vector per model
instead of asking a name per person, so nothing per person got bigger or smaller. A performance
change that moved memory would be a change doing something it had not said it was doing.

**France is unchanged and had to be.** It is the HLM family, and none of the three places above is on
its path. A change that had moved it would have been a change doing something other than what it
says.

**And the output did not change.** Not statistically: *byte for byte*, on all three runnable
examples, every result CSV and every income-stratified CSV — nine files on `HLM_France` and
`KevinHall_FINCH`, and four more on `HLM_India` **at the cohort it ships**, 1,240,613 people over
2010–2050, 16,564 rows and 29 MiB of CSV. That last one is half an hour a side and is the reason to
do it: India is where a constant factor is paid 1.24 million times a year, so it is the example most
likely to expose a change that reordered an accumulation.

**That check did not find the one defect this change had**, which is worth stating because this
document has twice recorded it finding one. `resolve_predictors` used `find`, which answers `unknown`
for a name nothing has interned *yet* as well as for one that never will — and `unknown` means "only
the string resolver can answer this", frozen in for the life of the model. A whole `KevinHall_FINCH`
run was byte-identical with the defect present, because something else happened to have interned
every name that run uses before the models were built. What found it was a unit test that resolves a
model before building the person it is evaluated against: it failed in release and passed in debug,
because a different test had interned the name first.

### Linux, for the first time

Every other number in this document is macOS and Apple clang, on one laptop, and that has been under
*what a reader should be sceptical about* since the document existed. A CI job now runs
`scripts/measure.sh` — the same script a person runs — on the Linux runner and uploads its JSON.
Three runs of each example, clang, release, `ubuntu-latest`. These are from run **35386188262**,
the one this document's other numbers were finalised against; the job runs on every push and its
numbers move:

| | Wall, best of 3 | Median | CPU, best | Peak memory |
|---|---:|---:|---:|---:|
| `HLM_France` | 2.93 s | 2.96 s | 2.92 s | **30.2 MiB** |
| `KevinHall_FINCH` | 11.98 s | 12.13 s | 11.97 s | **64.9 MiB** |

**Read these as indicative only.** A GitHub-hosted runner is a shared virtual machine with
neighbours, and this document already records a 20% measurement error from Spotlight indexing on a
machine nobody else was using. Nothing in the job compares against a stored number or can fail the
build, because a regression test on these would fail on the weather.

Two things in them are worth having anyway, and both survive the noise.

**The ratio between the two examples does.** FINCH is 4.09× France on the Linux runner and 3.52× on
the laptop, taking best-of-run against best-of-run on the same shipped configs — the same shape of
workload on machines that differ by about two and a half in absolute speed, and as close as two
numbers carrying this much noise are going to get. That is the thing a single job can say.

**And the memory is lower on Linux than on macOS**, by 29% on France (30.2 against 42.7 MiB) and 17%
on FINCH (64.9 against 78.2 MiB). The same binary, the same inputs, the same allocations: what
differs is glibc's allocator against libmalloc and how each returns pages. It is not a property of
this code and it is not worth chasing; it is worth knowing before anybody quotes one of these
figures as *the* memory this program uses.

### Loading

`--dry-run` in both: config, both model files, the data index, the disease registry, every
disease's tables, and both scenarios' module sets. It is the same flag in both implementations and
stops at the same point.

| | Wall | CPU | Peak memory |
|---|---:|---:|---:|
| HLM_France, baseline | 1.31 / 1.36 / 1.54 s | 0.60 s | 75.2 MiB |
| HLM_France, this build | **0.23 / 0.24 / 0.25 s** | **0.23 s** | **36.9 MiB** |
| KevinHall_FINCH, baseline | 0.89 / 0.99 / 1.05 s | 0.09 s | 20.4 MiB |
| KevinHall_FINCH, this build | **0.05 / 0.05 / 0.09 s** | **0.04 s** | **19.5 MiB** |

This build loads everything a run needs before any worker thread exists, which is how audit finding
B-02 — a data race in a lazily-populated repository — is removed by construction rather than by
locking. That should have made loading *more* expensive than the baseline's lazy path, and it is
less on both examples, by 5× on France and by 18× on FINCH.

France costs more to load than FINCH in both implementations for one reason: its `static_model.json`
is 18.8 MB against FINCH's 12 KB, because the FINCH model keeps its tables in CSVs beside it.

### The one that is not small

`HLM_India` is the largest thing this build runs: 35 diseases, 11 risk factors and a cohort of
**1,240,613** over 2010–2050, both scenarios, one thread.

| | Wall | CPU | Peak memory |
|---|---:|---:|---:|
| This build | 2,535 s (42 min) | 2,496 s | 2,555 MiB |

That is 199 times `HLM_France`'s cohort for 900 times its wall time, so it is not linear — the
disease module's per-person, per-disease work grows with the cohort while the 35-disease relative
risk tables make each person's share of it six times France's.

**Re-measured after the index-keyed store and the bounded lookup**, and this time with the baseline
run on it as well — which had never been done, on any machine, before this run:

| | Wall | CPU | Peak memory | Rows per CSV |
|---|---:|---:|---:|---:|
| This build, before ADR 0037 | 2,535 s (42.3 min) | 2,496 s | 2,555 MiB | 16,564 |
| **This build** | **2,068 s (34.5 min)** | **2,038 s** | **1,872 MiB** | 16,564 |
| **Baseline** | 2,079 s (34.7 min) | 4,154 s | 3,011 MiB | 16,564 |

So ADR 0037 and ADR 0040 together are worth **1.23× and 27% of the memory** on the largest example —
which matters because France gained 1.14× from the first and nothing from the second, and FINCH
gained 1.54× and then 1.44×. India has France's factor count and FINCH's cohort problem, and it is
the example where the constant factors are paid 1.24 million times a year.

**Against the baseline, India is the one example where the wall times are the same**, and the reason
is worth stating because it is the clearest illustration of what the CPU column is for. On
`HLM_France` and `KevinHall_FINCH` this build is about 2× faster in wall time *and* 3× cheaper in CPU.
On `HLM_India` it is **2.04× cheaper in CPU and 1.61× smaller in memory, at the same wall time** —
because the baseline runs its two scenarios on two threads and this build runs them one after the
other on one ([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)). The
baseline spends two cores to finish when this build finishes on one.

That is the trade being made, seen at the size where it is visible: sequential scenarios cost nothing
on the small examples because the work itself is smaller, and on the large one they cost exactly the
parallelism — no more. A host that wants India's wall time halved can run the two scenarios as two
processes and get byte-identical output, which is not something the baseline's shared, lazily
populated repository allows (audit B-01, B-02).

**This is the first time the baseline has been run on `HLM_India` at all**, on any machine, in any
run of this project. It exits 0 and writes the same 16,564 rows to each of the same four files.

The row count is the same 16,564 at every cohort size, because the output is per (year, sex, age
band) and not per person. That is worth knowing before sizing a disk for a full-scale sweep: the
result files do not grow with the cohort.

`HLM_India` is compared against the baseline at **one hundredth of this cohort**
([docs/equivalence.md](equivalence.md)); at full scale twenty seeds of both implementations would be
about a day, which is why.

### Where FINCH's 195 MiB is

FINCH loads 19.5 MiB and peaks at 195. That is not accumulation over the horizon, and it is worth
saying where it goes, because 195 MiB for a cohort of 6,817 is a large number and the equivalence
result depends on none of it:

| Configuration | Peak |
|---|---:|
| `--dry-run` — everything loaded, nothing simulated | 19.5 MiB |
| one scenario, one simulated year | 95.4 MiB |
| both scenarios, one simulated year | 172.6 MiB |
| both scenarios, all eleven years | 195.6 MiB |

So it is **≈76 MiB per scenario**, charged at that scenario's first simulated year, plus about
2.3 MiB per scenario per additional year. Eleven years of horizon account for 23 MiB of the 195;
the cohort and its per-scenario result series account for 152. Per person that is about 11 KB,
which for 34 risk factors and 15 diseases held in `std::map`s keyed by `core::Identifier` — every
key a string, every node separately allocated — is the same cost the profile below finds in the
time column, seen from the other side.

The baseline peaks at 198 MiB on the same example, so this was not a regression against it; it was a
property of the data structure both implementations had chosen. **It has since been measured again:**
the index-keyed store took this example from 195.5 MiB to 77.5 MiB, which is 60% of it, by replacing
55 separately allocated tree nodes per person with one vector. The table above is the old shape and is
kept because the *breakdown* — how much is per scenario, how much per year — is unchanged by it.

## Where the time went, and what it cost to find out

This section is history, from the run that first got `HLM_France` working. It is kept because two
of the three causes below are the kind of thing that comes back.

The first measurement of this build was **8.8 s wall, 8.8 s CPU, 144 MiB** — nearly twice the
baseline's wall time and 1.7× its memory. A profile (`sample`, 7 seconds of a run) explained all
of it, and none of the three causes were where I would have guessed.

### 1. Exception-driven control flow in the result writer — 8.8 s to 3.3 s

41% of the profile was `dyld`, `libunwind`, `__gxx_personality_v0` and `_platform_memcmp` sitting
above my own functions: the unmistakable shape of exceptions being thrown in a loop. The call graph
named the site immediately:

```
2238 hgps::output::ResultWriter::write
 1098 hgps::output::ResultWriter::write_income_rows
  1098 hgps::model::DataSeries::at(Gender, Income, string) const
   1087 std::__throw_out_of_range
    1085 __cxa_throw
```

The writer asked for every income-stratified channel of every stratum of every row, and caught the
`out_of_range` that `at` throws when a channel has no stratified counterpart — which is most
channels, most of the time. `DataSeries::find` answers the same question by returning a pointer.

Worth stating plainly: this is the same anti-pattern the audit criticised in the earlier rewrite
(its twenty swallowing `catch` blocks), written here by the same reflex — "the lookup can fail, so
handle the failure" — in the one place where the failure is the common case. It cost 5.5 seconds of
a 8.8-second run, and no amount of reading the code would have found it. It is the argument for
profiling rather than reasoning about performance.

### 2. String work in the standard-deviation pass — 3.3 s to 2.7 s

The next profile put `__tolower`, `chars::to_lower`, `core::to_lower` and
`DataSeries::at(Gender, string)` together at about a fifth of the remaining time. The
standard-deviation accumulator was called once per person per channel per year, and on each call it
built two strings (`"mean_" + name`, `"std_" + name`), lower-cased both, and looked each up in a
`std::map<std::string, …>`. The channel set does not change during a year, so the vectors are now
resolved once, up front, and the per-person loop adds to them.

### 3. Holding the fitted-model file twice — 144 MiB to 57 MiB

France's `static_model.json` is 18.8 MB, and almost all of it is `residuals` and `fittedValues` —
480,000 numbers of per-observation diagnostics from the R fit that the simulation never reads.
Three things were wrong:

- `io::read_json` read the whole file into a `std::string` and parsed *that*, so the text and the
  tree were resident at the same time. It now parses the stream, and re-reads the file only to turn
  a byte offset into a line and column when a parse actually fails — which is never, on a run that
  works.
- Those two members are now discarded during the parse, through nlohmann's parse callback, so no
  tree is built for them.
- The model file was being read **twice**: once by `read_model_name` to find its `ModelName`, then
  again to load it. Once.

38.5 MiB of the remaining 57 MiB is loaded data; the other 19 MiB is the cohort, its diseases and
the result series.

## Where the time goes now

Three profiles of the same example, newest last. This first one is `sample` at 1 ms over five
seconds of a `KevinHall_FINCH` run, grouped by top of stack, 3,758 attributed samples: it is the
state **after** the index-keyed store and **before** both the bounded search and this run's
call-site change, and it is the profile those two were made from. *After the call-site change*,
below, is where the code stands now; the profile from before the index-keyed store is at the end of
this file.

| Share | What |
|---:|---|
| 18.0% | `FactorValues::find_index` — the flat store's scan. Integer comparisons, no strings |
| 11.7% | `_platform_memcmp`, plus 2.7% in its stub |
| 8.3% | `Identifier` construction: `validate_identifier`, `chars::to_lower`, `chars::is_alnum`, `core::to_lower` |
| 6.5% | `__tolower` and its stub |
| 5.0% | `Person::try_risk_factor_value` itself |
| 4.1% | the name→index hash probe |
| 3.9% + 3.8% + 1.9% | `FactorValues::find(Identifier)` and `operator[]` — the name-keyed entry points |
| 3.1% | `case_insensitive::equals` |
| 2.3% | `is_metadata_predictor` |
| 1.8% | the Kevin Hall model's derived expected values |
| the rest | the analysis module, the relative-risk lookups, `libm`, the RNG, allocation |

**Compared with before**: string comparison and identifier handling were **52.3%** and are now about
**31%**, and the single largest named function — `DiseaseModelBase::relative_risk_for_risk_factors`,
at 13% of all samples — has left the top eighteen entirely. What replaced it at the top is integer
work.

Two things the profile pointed at, both with a measurement behind them rather than a guess. The
second is **done this run** — *Names resolved at the call site*, above — and the first was answered
by [ADR 0040](decisions/0040-a-bounded-search-for-the-long-vectors.md):

1. **`find_index` is a linear scan, and 55 entries is where that stops being free.** Answered: a scan
   below sixteen entries and a bounded binary search above it, worth 1.44× on FINCH.
2. **The remaining 31% is names being resolved at the *call site*.** Done. *Names resolved at the
   call site* has the change and the A/B, and *After the call-site change*, immediately below, is
   the profile that says where the time actually went.

### After the call-site change

`sample` at 1 ms over the **whole** run of `KevinHall_FINCH`, both binaries, back to back in one
session, on the shipped config. Whole runs rather than a fixed window: the faster binary gets
further through a window of the same length, so every share in it rises for the wrong reason. Over a
whole run a sample is worth a millisecond of that run, and the two columns can be subtracted.

| | Before | After |
|---|---:|---:|
| Thread samples, ≈ ms of run | 5,407 | 4,238 |
| of those, name handling at top of stack | **3,549** | **2,486** |

**The run lost 1,169 samples and 1,063 of them are name handling — 91%.** That is what the change
claimed, measured rather than assumed: the time did not move elsewhere in the program, it stopped
being spent.

The 65.6% and 58.7% those two rows work out to are **not** comparable with the 31% quoted in *Names
resolved at the call site* or the 52.3% in the section above. Both of those counted a narrower set
of symbols over a five-second window; this counts every name-shaped symbol over a whole run. What is
comparable is the pair of columns here, because they were taken the same way an hour apart.

The symbols say it more precisely. Leaf samples, before against after:

| Symbol | Before | After |
|---|---:|---:|
| `case_insensitive::equals` | 203 | **24** |
| `chars::to_lower` | 178 | **87** |
| `validate_identifier` | 141 | **26** |
| `is_metadata_predictor` | 112 | **15** |
| `chars::is_alnum` | 87 | **35** |
| the name→index hash probe | 303 | **129** |
| `Person::try_risk_factor_value` | 189 | 269 |
| `FactorValues::find_index` | 116 | 163 |
| `evaluate_linear_model` | 84 | 124 |
| `_platform_memcmp` | 795 | **832** |

The five predicates and the hash probe are what went; the three that rose are where the work went
instead — the index path, which is a larger share of a smaller run.

**`memcmp` did not move at all, and that is the next run's finding.** 795 samples before, 832 after.
The linear models stopped comparing strings entirely, so whatever is doing it now is somewhere this
change did not reach. Attributing the *after* profile's name handling to the caller that asked for
it:

| Caller | Samples |
|---|---:|
| the analysis module's `DataSeries`, keyed by `std::string` | 868 |
| derived predictors — an `Identifier` built from a concatenation | 456 |
| the linear models | 298 |
| the Kevin Hall model's expected values | 233 |
| the disease models, loading, the result writer | 50 |
| `memcmp` `sample` could not attribute to a caller | 888 |

The linear models were **1,398** in that table before this change and are 298 now, which is the same
1,100 samples from the other direction. The analysis module is untouched by this change and is now
the largest named consumer: `DataSeries::at(Gender, Income, std::string)` is a string lookup per
series per person per year, the same shape of problem one layer up. It is
[docs/backlog.md](backlog.md) item 9, and it now has a number on it rather than a suspicion.

Two cautions on that last table. The 888 unattributed samples are a fifth of the run, so treat the
split as indicative; and its counts are inclusive call-graph counts, which are not additive with the
leaf counts above.

## Where the time went before the index-keyed store

Kept because it is the measurement the change above was made from, and because the two profiles
together are the argument: the thing that was 52% of the run is now 31%, and the function that was
the single largest is no longer in the top eighteen.

Two profiles, one per example, `sample` at 1 ms, grouped by top of stack.

**HLM_France**, two seconds of a run — 1,540 thread samples, 1,463 of them attributed to a symbol
with five or more:

| Share | What |
|---:|---|
| 39.5% | `_platform_memcmp` — string comparison, nearly all of it `std::map` lookups keyed by `core::Identifier` or by channel name |
| 10.5% | allocator traffic and memory moves |
| 9.3% | the analysis module's two passes over the population |
| 6.6% | the disease module: incidence, remission, and the relative-risk lookups |
| 5.7% | the dynamic HLM's per-person, per-factor regression |
| 5.3% | population bookkeeping and migration |
| 3.9% | name lower-casing, still, in the resolution that remains |
| 2.3% | the RNG |
| 11.9% | other named symbols: output formatting, `std::map` tree operations, the weight model |
| 5.0% | below the five-sample cutoff |

**KevinHall_FINCH**, six seconds of a run — 4,635 thread samples, 4,540 of them (98%) attributed:

| Share | What |
|---:|---|
| 52.3% | string comparison and identifier handling: `_platform_memcmp`, `__tolower`, `case_insensitive::equals`, `Identifier::validate_identifier` |
| 15.2% | the disease module, almost all of it `DiseaseModelBase::relative_risk_for_risk_factors` |
| 11.1% | the static linear model and predictor resolution |
| 4.7% | allocator traffic and `std::map` node construction |
| 3.1% | the analysis module |
| 2.7% | person and result-series lookups not already counted as string work |
| 1.8% | the Kevin Hall model itself |
| 1.2% | `libm` — `pow` and `log`, the Box-Cox and the energy balance |
| 0.6% | the RNG |
| 7.4% | everything else named |

**The two profiles say the same thing, and FINCH says it louder.** The program is dominated by
**map lookups keyed by strings**, not by arithmetic. `core::Identifier` compares by string —
deliberately, because comparing by the cached 64-bit hash is audit finding B-04 — and every
risk-factor read on every person in every year is such a comparison. FINCH has 34 risk factors
against France's 11 and 15 diseases against 6, so it does three times as many of them per person
and the share rises from 40% to 52%. The allocator share and the 195 MiB have the same root.

The single largest named function in the FINCH profile is
`DiseaseModelBase::relative_risk_for_risk_factors` at 13% of all samples — a per-person,
per-disease lookup over tables that do not change during a run, which is item 14 of the previous
run's backlog, found again from the other end.

The conclusion that profile reached — resolve each factor name to an index once and use the index on
the hot path — is what [ADR 0037](decisions/0037-index-keyed-risk-factor-store.md) did, and *Where the
time goes now* above is the result. The worry it recorded was the right one: an index-keyed store is
exactly the kind of change that can reorder a reduction without anyone noticing, and the answer was to
make the one order-sensitive site iterate its own name-ordered list so that the arithmetic could be
checked byte for byte rather than argued about. It was checked, and the first attempt failed it.

`DataSeries` is still keyed by channel name and is still on the list; the profile above puts the
analysis module and the result writer well below the per-person work, so it is the smaller half.

## Threads

`--threads N` sets the worker count for the parallel sections, which are the RNG-free ones only
([ADR 0026](decisions/0026-parallelism-and-fixed-order-reductions.md)). On either example the
number makes no measurable difference to wall time: the reductions that were parallelised are not where
the time is, and everything that draws randomness is sequential by construction (determinism clause
D3). That is the expected result and the honest one — the parallelism exists so that the contract
holds when someone uses it, not because it currently buys anything.

The output is byte-identical at any thread count, which `tests/sim/reproducibility_test.cpp`
asserts at 1 and at 4.

## Reproducing this

`tests/equivalence/run.py` writes a like-for-like config pair for each implementation into its
working directory, so that is where the configs below come from:

```bash
# One seed is enough to get the pair written; the runs themselves are the measurement.
tests/equivalence/run.py --example HLM_France --seeds 3 --stop-time 2015 --workdir /tmp/hgps-perf
C=/tmp/hgps-perf/HLM_France

# Three runs each. The inner shell is so that the program's own output goes to /dev/null while
# time's report does not — `/usr/bin/time -l cmd 2>/dev/null` discards both.
for i in 1 2 3; do
  /usr/bin/time -l /bin/sh -c '"$0" "$@" >/dev/null 2>/dev/null' \
      /tmp/hgps-build/baseline-release/src/HealthGPS.Console/HealthGPS.Console \
      --config $C/baseline/config-seed-1.json -T 1
  /usr/bin/time -l /bin/sh -c '"$0" "$@" >/dev/null 2>/dev/null' \
      ./out/build/release/src/healthgps --config $C/new/config-seed-1.json --threads 1
done

# A profile of a run in progress.
./out/build/release/src/healthgps --config $C/new/config-seed-1.json --threads 1 >/dev/null &
sample $(pgrep -n healthgps) 6 1 -f /tmp/hgps-perf/profile.txt
```

Check that the machine is actually idle first — `ps -A -o %cpu,comm -r | head` — because a
background indexer is worth 20% of the wall time, which is more than anything this document
discusses.
