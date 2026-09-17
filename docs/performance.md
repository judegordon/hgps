# Performance

Measured, not estimated. Everything below is the converted `HLM_France` reference example —
2010–2050, a cohort of 6,244, both scenarios (`simple` active in both implementations), one trial
run, seed 1 — on:

| | |
|---|---|
| Host | Apple M5, 10 cores, 16 GB, macOS 26.6.2 |
| Compiler | Apple clang 21.0.0, `-O3`, `-ffp-contract=off` |
| Baseline | `/tmp/hgps-build/baseline-release`, built as [docs/build-notes.md](build-notes.md) records |
| This build | `out/build/release`, `--threads 1` |

Each figure is the best of three runs on an otherwise idle machine; the spread across the three is
given where it matters. `/usr/bin/time -l`, so peak memory is the maximum resident set size.

## The numbers

| | Wall | CPU | Peak memory |
|---|---:|---:|---:|
| **Baseline** | 4.79–5.22 s | 7.49–8.19 s | 85.3 MiB |
| **This build** | **2.72–2.73 s** | **2.71–2.77 s** | **57.1 MiB** |

**1.8× faster in wall time, 2.8× less CPU work, and a third less memory** — running its two
scenarios one after the other, on one thread, against a baseline that runs them concurrently.

The CPU column is the one that says something about the code. The baseline's wall time is shorter
than its CPU time because it runs the baseline and intervention scenarios on separate threads; this
build runs them sequentially by design ([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)),
so its wall and CPU times are the same number. Sequential execution was expected to cost about a
factor of two in wall time and to be worth it for byte-identical output. It turned out not to cost
anything, because the work itself is smaller.

Loading, with `--dry-run` — config, both model files, the data index, the disease registry and
every disease's tables:

| | Wall | CPU | Peak memory |
|---|---:|---:|---:|
| Baseline | 1.48 s | 0.60 s | 75.4 MiB |
| This build | 0.52 s | 0.25 s | 38.5 MiB |

This build loads everything a run needs before any worker thread exists, which is how audit finding
B-02 — a data race in a lazily-populated repository — is removed by construction rather than by
locking. That should have made loading *more* expensive than the baseline's lazy path, and it is
still less.

## Where the time went, and what it cost to find out

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

From a two-second profile of the current build, grouped by top of stack (1,540 thread samples,
1,463 of them attributed to a symbol with five or more):

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

The shape of that list says the program is now dominated by **map lookups keyed by strings**, not
by arithmetic. `core::Identifier` compares by string — deliberately, because comparing by the
cached 64-bit hash is audit finding B-04 — and every risk-factor read on every person in every
year is such a comparison. The allocator share has the same root: those maps.

The obvious next step is to resolve each factor and channel name to an index once per run and use
the index on the hot path, which is a contained change to `Person::risk_factors` and `DataSeries`.
It is in [docs/backlog.md](backlog.md) rather than done, for two reasons: the program is already
faster than the baseline it has to be comparable to, and an index-keyed store is exactly the kind
of change that can reorder a reduction without anyone noticing. Doing it would want the equivalence
harness re-run, which is cheap, and the determinism tests to stay green, which they should.

## Threads

`--threads N` sets the worker count for the parallel sections, which are the RNG-free ones only
([ADR 0026](decisions/0026-parallelism-and-fixed-order-reductions.md)). On this example the number
makes no measurable difference to wall time: the reductions that were parallelised are not where
the time is, and everything that draws randomness is sequential by construction (determinism clause
D3). That is the expected result and the honest one — the parallelism exists so that the contract
holds when someone uses it, not because it currently buys anything.

The output is byte-identical at any thread count, which `tests/sim/reproducibility_test.cpp`
asserts at 1 and at 4.

## Reproducing this

```bash
# Both implementations on the same config, three runs each.
S=/tmp/hgps-perf; mkdir -p $S/out-new $S/out-baseline
# ... derive the two configs as tests/equivalence/run.py does, then:
/usr/bin/time -l /tmp/hgps-build/baseline-release/src/HealthGPS.Console/HealthGPS.Console \
    --config $S/baseline.json -T 1
/usr/bin/time -l ./out/build/release/src/healthgps --config $S/new.json --threads 1

# A profile of a run in progress.
./out/build/release/src/healthgps --config $S/new.json --threads 1 >/dev/null &
sample $(pgrep -n healthgps) 3 1 -f $S/profile.txt
```

`tests/equivalence/run.py` writes the derived configs into its working directory, so the quickest
way to get a like-for-like pair is to run it once with `--seeds 1 --stop-time 2015` and take
`$TMPDIR/hgps-equivalence/HLM_France/{baseline,new}/config-seed-1.json`.
