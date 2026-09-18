# Performance

Measured, not estimated. Two examples: the converted `HLM_France` reference example — 2010–2050, a
cohort of 6,244, 6 diseases, 11 risk factors — and the converted `KevinHall_FINCH` example —
2022–2032, a cohort of 6,817, 15 diseases, 34 risk factors, with the S1 policy model active in the
intervention scenario. Both run both scenarios, one trial run, seed 1, on:

| | |
|---|---|
| Host | Apple M5, 10 cores, 16 GB, macOS 26.6.2 |
| Compiler | Apple clang 21.0.0, `-O3`, `-ffp-contract=off` |
| Baseline | `/tmp/hgps-build/baseline-release`, built as [docs/build-notes.md](build-notes.md) records |
| This build | `out/build/release`, `--threads 1` |

The exact configs are the ones `tests/equivalence/run.py` derives, so the two implementations are
given the same inputs in the same layout as in [docs/equivalence.md](equivalence.md). Every figure
is three runs on an otherwise idle machine and all three are given, because the spread is part of
the measurement. `/usr/bin/time -l`, so peak memory is the maximum resident set size.

A note on the machine: the first attempt at these numbers was taken while Spotlight was indexing
the working directories, and it put `HLM_France` at 3.38 s rather than 2.78 s — a 20% error, larger
than any difference discussed below. The figures here were taken after `mds_stores` went quiet.

## The numbers

**HLM_France**, 2010–2050:

| | Wall | CPU | Peak memory |
|---|---:|---:|---:|
| **Baseline** | 4.78 / 4.88 / 5.26 s | 7.69 / 7.90 / 8.22 s | 85.2 MiB |
| This build, before the index-keyed store | 2.74 / 2.74 / 2.76 s | 2.68 / 2.70 / 2.71 s | 57.0–57.1 MiB |
| **This build** | **2.41 / 2.41 / 2.42 s** | **2.36 / 2.36 / 2.37 s** | **52.4 MiB** |

**KevinHall_FINCH**, 2022–2032:

| | Wall | CPU | Peak memory |
|---|---:|---:|---:|
| **Baseline** | 15.19 / 15.52 / 15.52 s | 24.05 / 24.31 / 24.46 s | 197.8–198.7 MiB |
| This build, before the index-keyed store | 11.85 / 11.91 / 12.04 s | 11.66 / 11.76 / 11.89 s | 195.3–195.8 MiB |
| **This build** | **7.71 / 7.73 / 7.89 s** | **7.58 / 7.63 / 7.76 s** | **77.5 MiB** |

Against the baseline: on the HLM surface **2.0× faster in wall time, 3.3× less CPU work, 38% less
memory**; on the FINCH surface **2.0× faster, 3.2× less CPU, and 2.6× less memory** — running its two
scenarios one after the other, on one thread, against a baseline that runs them concurrently.

The CPU column is the one that says something about the code. The baseline's wall time is shorter
than its CPU time because it runs the baseline and intervention scenarios on separate threads; this
build runs them sequentially by design ([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)),
so its wall and CPU times are the same number. Sequential execution was expected to cost about a
factor of two in wall time and to be worth it for byte-identical output. It costs nothing on either
example, because the work itself is smaller.

### What the index-keyed store bought

The middle row of each table is this build immediately before
[ADR 0037](decisions/0037-index-keyed-risk-factor-store.md) and the bottom row immediately after, both
measured on an idle machine in one session, alternating the two binaries run by run so that any drift
in the machine falls on both equally:

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

`sample` at 1 ms over five seconds of a `KevinHall_FINCH` run, grouped by top of stack, 3,758
attributed samples. The previous profile of the same example is below it for comparison.

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

Two things the profile now points at, both with a measurement behind them rather than a guess. They
are [docs/backlog.md](backlog.md) items rather than this run's work, because the ruling for this run
was the store and the store is done:

1. **`find_index` is a linear scan, and 55 entries is where that stops being free.** It is the right
   shape for France's 11 factors — contiguous, one or two cache lines, no mispredicted branch — and at
   FINCH's 55 it averages 27 integer comparisons per lookup and is now the largest single item in the
   profile. A per-person array indexed directly by factor index would make it O(1) for about the same
   memory, at the cost of a second vector for the present-index list that iteration and `size()` need.
2. **The remaining 31% is names being resolved at the *call site*.** The store no longer compares
   strings; its callers still hand it an `Identifier`, which costs a hash probe, and some of them
   *construct* one per person per year — which is what `validate_identifier` and `is_alnum` in a
   profile mean. The fix is the same idea one level up: the linear model's coefficient list and the
   Kevin Hall model's nutrient names hold resolved indices, and the name never reaches the hot loop.

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
