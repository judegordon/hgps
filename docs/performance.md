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
| **This build** | **2.78 / 2.90 / 3.23 s** | **2.76 / 2.85 / 3.22 s** | **56.7–56.9 MiB** |

**KevinHall_FINCH**, 2022–2032:

| | Wall | CPU | Peak memory |
|---|---:|---:|---:|
| **Baseline** | 15.19 / 15.52 / 15.52 s | 24.05 / 24.31 / 24.46 s | 197.8–198.7 MiB |
| **This build** | **11.63 / 11.69 / 11.79 s** | **11.61 / 11.65 / 11.74 s** | **195.5–195.9 MiB** |

On the HLM surface: **1.7× faster in wall time, 2.8× less CPU work, a third less memory**. On the
FINCH surface: **1.3× faster in wall time, 2.1× less CPU, and the same memory to within 1%** —
running its two scenarios one after the other, on one thread, against a baseline that runs them
concurrently.

The CPU column is the one that says something about the code. The baseline's wall time is shorter
than its CPU time because it runs the baseline and intervention scenarios on separate threads; this
build runs them sequentially by design ([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)),
so its wall and CPU times are the same number. Sequential execution was expected to cost about a
factor of two in wall time and to be worth it for byte-identical output. It costs nothing on either
example, because the work itself is smaller.

The previous run's figure for `HLM_France` was 2.72–2.73 s and 57.1 MiB, and the budget set for
this run was that figure plus 10%. At 2.78–3.23 s and 56.8 MiB it holds: the FINCH surface —
`StaticLinear`, `KevinHall`, five more interventions, the derived-predictor resolver, region and
ethnicity — added nothing measurable to an example that uses none of it, which is what one would
want from code that is selected by the model family named in the config.

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
risk tables make each person's share of it six times France's. It is not compared against the
baseline ([docs/equivalence.md](equivalence.md)), and one run of it at twenty seeds in each
implementation would be about a day, which is the real reason it is not.

Recorded because "it runs" is worth qualifying: this is the example where the index-keyed store in
[docs/backlog.md](backlog.md) would be worth an hour of anybody's time rather than a footnote.

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
time column, seen from the other side. The index-keyed store in [docs/backlog.md](backlog.md) would
move both numbers.

The baseline peaks at 198 MiB on the same example, so this is not a regression against it; it is a
property of the data structure both implementations chose.

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

The obvious next step is to resolve each factor and channel name to an index once per run and use
the index on the hot path, which is a contained change to `Person::risk_factors` and `DataSeries`.
It is in [docs/backlog.md](backlog.md) rather than done, for two reasons: the program is already
faster than the baseline it has to be comparable to on both examples, and an index-keyed store is
exactly the kind of change that can reorder a reduction without anyone noticing. Doing it would
want both equivalence references re-run, which is an hour, and the determinism tests to stay
green, which they should.

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
