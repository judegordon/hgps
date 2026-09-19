# Reading guide

For somebody who is going to **own** this code, reading it end to end for the first time.

The order below is the one that costs the least backtracking: what the project claims, then what it
found, then how the claims are tested, then how it is built, then the code. Each entry says what
the thing is, why it exists, roughly how long it takes, and the one question you should be able to
answer when you put it down. If you cannot answer that question, the fault is the document's and
it is worth an issue.

**The whole path is about two and a half days of reading** — roughly six hours of documents, a day
on the fifty decision records, and a day on 30,000 lines of C++. It is written to be taken in
order, but the four stopping points below are real: each is a place where you know enough to be
useful.

| If you have | Read | You will be able to |
|---|---|---|
| **20 minutes** | [briefing](#1-docsbriefingmd), then *State of the project* at the top of [SUMMARY](#2-docssummarymd) | say what this is, what it proves, and what is blocked on somebody else |
| **half a day** | items 1–5 | defend or attack any number this project quotes |
| **two days** | items 1–9 | change the model code without breaking the comparison |
| **everything** | items 1–14 | own it |

Nothing here is a substitute for `scripts/check.sh`. Run it once, early, on a machine with
`VCPKG_ROOT` set: it is the fastest way to find out whether the tree in front of you is the tree
these documents describe. `--fast` is the release preset alone and its tests are a minute; the full
run is the long one, because the four presets are 54, 511, 1,620 and 972 seconds of tests before
the frontend, the browser and the equivalence harness ([docs/SUMMARY.md](SUMMARY.md), *The same
tree locally*).

---

## Part one — the claims and the findings

### 1. [docs/briefing.md](briefing.md)

**What it is.** The note written for the upstream Health-GPS authors: what a second implementation
proved, what it found in the baseline, and the two questions only they can answer.

**Why it exists.** Everything else here is addressed to a maintainer of *this* repository. This one
is addressed to the people who own the model, and it is therefore the only document that states the
project's purpose without assuming you accept its premises.

**How long.** Ten minutes. It is deliberately the shortest document in the repository.

**The question.** *Why does a second implementation of an existing simulation exist at all, and
what has it produced that the first one could not?*

### 2. [docs/SUMMARY.md](SUMMARY.md)

**What it is.** What was built, what is proven, and where it stops — the tenth build run's summary,
with the state of the project at the top. Previous runs' summaries are in
`git log -p docs/SUMMARY.md` and are not in the file.

**Why it exists.** It is the one document that carries the counts: tests, comparisons, CI jobs,
findings, and the census of 1,500 runs. When a number is quoted anywhere else in this repository,
this is usually where it came from.

**How long.** Thirty minutes, and read the tables rather than skimming them — the census table and
the *What a reader should still be sceptical about* list are the two places the project argues
against itself.

**The question.** *What has this build been compared against, how many times, and what did the
comparison fail to cover?*

### 3. [docs/deviations.md](deviations.md)

**What it is.** Every behaviour that intentionally differs from the baseline, with the audit finding
that motivated it, the evidence, and the test that pins it. Three tiers: defects fixed that change
numbers, design differences that change the output's shape, and internal differences that change
nothing.

**Why it exists.** Bit-exact agreement with the baseline is not a goal
([ADR 0024](decisions/0024-deviations-recorded-baseline-bugs-fixed.md)), so something has to say
which differences were chosen. Without this file a divergence found by the comparison cannot be
attributed, and the comparison stops being evidence.

**How long.** Fifteen minutes. Read the flag table at the top properly; the rest is a reference.

**The question.** *When the harness reports a difference, how do I tell a deliberate one from a
defect?*

### 4. [docs/upstream-reports.md](upstream-reports.md)

**What it is.** Seven findings that belong to whoever owns the baseline and the example data,
written as bug reports — each with the one command that reproduces it against *their* binary and
their data, not ours.

**Why it exists.** Five of the seven cannot be fixed here without inventing a number for somebody
else's fitted model. Two of them this build has guarded against, and they are still reported
because a guard in a reimplementation does nothing for anybody running the original.

**How long.** Forty minutes. Reports 1, 6 and 7 are the ones that matter most: report 1 blocks two
of the six upstream examples outright, report 6 is the body-fat pole, and report 7 is the
`NaN`-counted-as-zero in the analysis module.

**The question.** *Which of this project's open problems are somebody else's to solve, and what
exactly is being asked of them?*

Read [docs/findings/seed-80.md](findings/seed-80.md) straight after this one — twenty minutes, the
full trace of report 6, term by term, including the point at which the baseline's own statements
compiled alone return the same impossible number to fifteen significant figures. It is the single
best worked example of how a finding is established in this project.

## Part two — how anything here is known to be true

### 5. [docs/equivalence-method.md](equivalence-method.md)

**What it is.** The comparison method alone, with no results in it: how a run's CSV files are
reduced to series, what is left out of the reduction and why, the rule that decides pass or fail,
the special case for series that live on a lattice, and the self-consistency suite.

**Why it exists.** Two Monte Carlo runs cannot agree exactly, so every equivalence claim this
project makes rests on a statistical rule. Section 4 is written so that you can check the rule
without reading the script, and section 3 is the honest list of everything the comparison does not
look at.

**How long.** Ninety minutes, and it is the densest reading in the repository. Section 4 twice.

**The question.** *What does "the two implementations agree" mean here, precisely — and what is the
rate at which this rule cries wolf?*

Its companion is [docs/equivalence.md](equivalence.md) (1,473 lines), which is the *results* rather
than the method. Do not read it end to end on a first pass. Read *Verdict* and the three
per-example result sections, about forty minutes, and come back to the rest when you need a
specific number.

Two smaller documents finish the picture and are worth twenty minutes together:
[docs/examples.md](examples.md), which upstream examples run here and the two that neither
implementation can run; and [docs/test-port-map.md](test-port-map.md), where each of the baseline's
471 tests went, suite by suite, including the 34 with no counterpart and why.

## Part three — how it is built

### 6. [docs/design.md](design.md)

**What it is.** The architecture: the ten modules and their one-way dependencies, the data flow
through a run, the determinism contract's fourteen clauses, the parallelism model, the diagnostics
scheme, config v2, the data store, and the I/O formats.

**Why it exists.** It is the map the source tree assumes you have. Reading `src/` without §2 and §4
of this document is the main way to waste a day here.

**How long.** An hour. §2 (module layout), §4 (the determinism contract) and §6 (diagnostics) are
the three that the code will not make sense without; §3, §7, §8 and §9 can be consulted rather than
read.

**The question.** *Why can this program produce byte-identical output at one thread and at sixteen,
and which mechanism enforces that rather than which convention?*

### 7. [docs/decisions/](decisions) — the fifty ADRs

**What it is.** One Architecture Decision Record per design choice, each with its context, the
decision, the alternatives rejected and the consequences. They are numbered in the order they were
taken and are never rewritten.

**Why it exists.** A decision record answers *why not the obvious thing* — which is the question a
new owner asks most often and the one the code cannot answer. Several of these records exist
specifically because the decision looks wrong from outside.

**How long.** 38,000 words — most of a day if read end to end, which is worth doing once. If you
have an hour instead, read 0005, 0008, 0024, 0032, 0041, 0048, 0049 and 0050: those eight carry the
shape of everything else.

**The question.** *For any surprising thing in this codebase, which record argues for it, and what
was the alternative?*

[docs/decisions/README.md](decisions/README.md) is the index, and `scripts/check.sh` fails if it
drifts from the directory. The same list, in the same order, is below — grouped by theme, numbered
in order within each group.

**Provenance, licence, and how a difference from the baseline is recorded**

- [0001 — Record architecture decisions](decisions/0001-record-architecture-decisions.md)
- [0002 — This is a BSD-3-Clause derivative of the baseline](decisions/0002-licence-bsd-3-clause-derivative.md)
- [0003 — The four source folders are read-only; the baseline builds out of tree](decisions/0003-read-only-sources-and-out-of-tree-baseline-build.md)
- [0024 — Baseline bugs are fixed, not reproduced; every deviation is recorded](decisions/0024-deviations-recorded-baseline-bugs-fixed.md)
- [0041 — A deliberate deviation is always switchable, so its effect can be measured](decisions/0041-deliberate-deviations-are-switchable.md)

**Toolchain, platforms and dependencies**

- [0004 — C++20, CMake presets, pinned vcpkg, GoogleTest, warnings as errors](decisions/0004-toolchain-cpp20-cmake-vcpkg-googletest.md)
- [0013 — Linux and macOS supported from the start; Windows not targeted](decisions/0013-platforms-linux-and-macos.md)
- [0014 — Three dependencies; implement SHA-256, CSV, matrix and Cholesky; delegate fetch and unzip](decisions/0014-minimal-dependency-set.md)
- [0019 — Split the monoliths into one unit per concern](decisions/0019-split-the-monolith-translation-units.md)
- [0023 — A small dense matrix with an explicit Cholesky, instead of Eigen](decisions/0023-own-matrix-and-cholesky.md)

**Module layout, the published API, and the three hosts**

- [0005 — Module layout: nine layers, dependencies one way](decisions/0005-module-layout.md)
- [0032 — The engine is a library with a published API; the CLI is a client of it](decisions/0032-library-and-a-thin-cli.md)
- [0033 — Progress is an event stream the simulation cannot see](decisions/0033-an-event-stream-the-simulation-cannot-see.md)
- [0042 — `hgps serve`: a local JSON server, in the same binary, on cpp-httplib](decisions/0042-a-local-server-in-the-same-binary.md)
- [0043 — A plain TypeScript frontend, with Vite and no framework](decisions/0043-a-plain-typescript-frontend.md)

**Determinism, randomness and identity**

- [0008 — The determinism contract, enforced by types](decisions/0008-determinism-contract-enforced-by-types.md)
- [0015 — RNG: no default constructor, rejection sampling, explicit 53-bit doubles](decisions/0015-rng-design.md)
- [0016 — `Categorical<T>`: sampling from an ordered sequence, by construction](decisions/0016-categorical-ordered-sampling.md)
- [0017 — Monotonic lifetime-unique person IDs, initial cohort `1..N` by index, O(1) free slots](decisions/0017-person-ids-monotonic-and-free-slots.md)
- [0025 — `Identifier` compares its string; the hash is for bucketing only](decisions/0025-identifier-string-equality.md)
- [0026 — Sequential by default; parallel only for RNG-free work, with fixed-order reductions](decisions/0026-parallelism-and-fixed-order-reductions.md)

**Diagnostics, and validating the input before anything runs**

- [0007 — Two-tier diagnostics: internal errors thrown, input issues accumulated](decisions/0007-two-tier-diagnostics.md)
- [0018 — No swallowing catch blocks; every name is validated at load time](decisions/0018-no-swallowing-catch-load-time-validation.md)
- [0022 — Validate config in code, not with an embedded JSON-Schema validator](decisions/0022-hand-written-config-validation.md)
- [0028 — A model file's relative paths resolve against the model file, and its generated factors count as known names](decisions/0028-model-files-resolve-their-own-relative-paths.md)
- [0035 — An intervention the configured model would ignore is refused at load time](decisions/0035-refuse-an-intervention-no-model-applies.md)

**Configuration, data, and the stores**

- [0010 — Define config v2 and ship a v1→v2 converter](decisions/0010-config-v2-and-a-converter.md)
- [0011 — Do not vendor the disease data; fetch with a required checksum, and generate synthetic fixtures](decisions/0011-data-fetched-not-vendored.md)
- [0012 — `pulmonary` is canonical; the registry is validated against the tree](decisions/0012-disease-naming-pulmonary.md)
- [0037 — A person's risk factors are keyed by index, not by name](decisions/0037-index-keyed-risk-factor-store.md)
- [0038 — Population impact fraction, loaded from the manifest and validated at load](decisions/0038-population-impact-fraction.md)
- [0039 — A scratch directory copies what it may write and links only what it reads](decisions/0039-scratch-directories-copy-what-they-may-write.md)
- [0040 — A bounded binary search for the long vectors, and a scan for the short ones](decisions/0040-a-bounded-search-for-the-long-vectors.md)

**The model surfaces, scenarios and interventions**

- [0009 — Scenarios run sequentially; net migration travels in a journal](decisions/0009-sequential-scenarios-and-the-migration-journal.md)
- [0021 — Scope for this run: the FINCH and HLM_France surfaces, with the `simple` intervention](decisions/0021-scope-finch-and-hlm-france.md)
- [0029 — One shape for the five age-banded interventions, and a parameter struct for the model families](decisions/0029-one-banded-intervention-shape.md)
- [0030 — The converter selects a policy scenario for the FINCH example, rather than the example being edited](decisions/0030-policy-scenario-selection-for-the-broken-finch-example.md)
- [0049 — The energy balance is integrated only as far as the model is defined](decisions/0049-the-energy-balance-is-integrated-only-where-it-is-defined.md)

**Output, and the record a run leaves behind**

- [0020 — One owner per output file; rows in a defined order](decisions/0020-output-single-owner-defined-row-order.md)
- [0034 — Every run writes a manifest beside its results](decisions/0034-a-run-manifest-beside-the-results.md)
- [0050 — No output carries a number that cannot exist](decisions/0050-no-output-carries-a-number-that-cannot-exist.md)

**Testing strategy and the fixture packs**

- [0006 — Validation: port the 471 tests, then statistical equivalence](decisions/0006-validation-strategy.md)
- [0036 — The equivalence harness is tested against itself, including a deliberately wrong build](decisions/0036-the-harness-is-tested-against-itself.md)
- [0044 — Two synthetic fixture packs, and every test that runs a configuration runs against both](decisions/0044-two-fixture-packs-and-a-parameterised-suite.md)
- [0045 — End-to-end tests in a real browser, with Playwright](decisions/0045-end-to-end-tests-in-a-real-browser.md)
- [0046 — What runs under which sanitizer, and why it is not everything](decisions/0046-what-runs-under-which-sanitizer.md)
- [0047 — The second fixture pack carries income, region, ethnicity and sector](decisions/0047-the-second-pack-carries-the-stratified-dimensions.md)

**The equivalence comparison**

- [0027 — The equivalence comparison excludes the age bands immigration cannot fill, and tests a rate where normal theory does not hold](decisions/0027-equivalence-excludes-the-bands-only-one-side-fills.md)
- [0031 — Each intervention is compared on its own, and FINCH borrows its definitions](decisions/0031-comparing-one-intervention-at-a-time.md)
- [0048 — The comparison's threshold is a stated false-positive rate, not an allowance](decisions/0048-a-comparison-with-a-stated-false-positive-rate.md)

### 8. [docs/api.md](api.md) and `include/hgps/`

**What it is.** The published C++ API: four calls, the handles they return, the event stream,
cancellation, the results, the compatibility flags, and what it throws. Six headers,
743 lines, and that is the entire surface — `hgps::engine` is a library and everything else in
this repository is a client of it ([ADR 0032](decisions/0032-library-and-a-thin-cli.md)).

**Why it exists.** It is the narrowest complete description of what this program does. If you read
only one thing before the source, read the six headers: they tell you the shape of a run in about a
page, and the CLI, the server and the tests all go through them.

**How long.** Thirty minutes for the document, twenty more to read `include/hgps/` itself. The
minimal host in the document is compiled and run by the test suite, so it cannot have rotted.

**The question.** *What are the four calls a host makes, and what does each of them own?*

## Part four — the source, in dependency order

29,978 lines of C++ under `src/` and `include/`, plus 18,454 in `tests/`. The modules below are in
the order `src/` depends on them — nothing depends on anything above it in the table — which is
also the order to read them in. [docs/design.md](design.md) §2 has the same graph drawn.

Read each module's headers first and its `.cpp` files only where the header raises a question. The
whole of part four is about a day; the three long entries are marked.

| # | Module | Size | What it is |
|---:|---|---:|---|
| 9 | `src/core/` | 25 files, 2,186 lines | the vocabulary: `Identifier`, `DataTable`, the column types, intervals, the matrix and its Cholesky, string and byte helpers |
| | `src/diagnostics/` | 6 files, 401 lines | the two tiers — an accumulated, located `Issue` for anything the input got wrong, a thrown `InternalError` for anything this program got wrong |
| | `src/random/` | 6 files, 500 lines | the seeded generator that has no default constructor, `Categorical<T>`, and the seed policy |
| 10 | `src/io/` | 12 files, 1,779 lines | CSV, JSON, SHA-256, path handling and process launching. No model knowledge at all |
| | `src/config/` | 11 files, 4,912 lines | config v2, its hand-written validator, the v1→v2 converter, and one loader per model family under `models/` |
| | `src/data/` | 4 files, 1,469 lines | the data index and the content-addressed store the disease data is fetched into |
| 11 | **`src/model/`** | 56 files, 11,101 lines | **the longest read — half a day on its own.** The person, the population, the mapping, and the four model surfaces |
| 12 | `src/sim/` | 5 files, 1,504 lines | the run loop: the year, the scenarios in sequence, the migration journal, and the interventions |
| | `src/output/` | 2 files, 451 lines | one owner per output file, rows in a defined order. Small, and load-bearing for every byte-identity claim |
| 13 | `src/engine/` | 11 files, 1,923 lines | the top of the library: the session, module construction, the manifest, the compatibility flags, the diagnostics bridge |
| 14 | `src/app/`, `src/server/`, `tools/`, `web/` | 3,009 lines of C++ in the CLI and the server, 1,468 in the two tools, 3,136 of TypeScript in the frontend | the three hosts and the two tools. Clients of the library, none of them privileged |

### 11 — `src/model/`, and the two files to read closely

This is where the science is, and it is the only module where reading the `.cpp` files is not
optional. Take it in this order:

1. `person.h`, `population.h`, `factor_values.h`, `mapping.h` — what a person is, and why risk
   factors are keyed by index rather than by name
   ([ADR 0037](decisions/0037-index-keyed-risk-factor-store.md)).
2. `demographic.cpp` (557 lines) — ageing, birth, death, and net migration.
3. `riskfactor/` — the model surfaces.
4. `disease/` — incidence, remission, mortality, and the population-impact-fraction tables.
5. `analysis/` — the year's means, the burden statistics, and the income strata. This is the last
   place a value passes through before it becomes output, which is what makes upstream report 7
   matter as much as it does.

**Two files deserve a careful hour each**, because they are the two model surfaces every comparison
in this project is a comparison *of*, and because both of them are where the last two runs' findings
live:

- **[`src/model/riskfactor/hlm_model.cpp`](../src/model/riskfactor/hlm_model.cpp)** — the
  hierarchical linear model and its dynamic form. This is the surface `HLM_France` and `HLM_India`
  run on, the only surface on which an intervention actually applies today (upstream report 2), and
  therefore the surface deviation B-24 was measured on.
- **[`src/model/riskfactor/kevin_hall/energy_balance.cpp`](../src/model/riskfactor/kevin_hall/energy_balance.cpp)**
  — the Kevin Hall two-compartment energy balance, 458 lines, solved in closed form once a year.
  Read it with [docs/findings/seed-80.md](findings/seed-80.md) open beside it. The partition
  coefficient `p = C / (C + F)` has a pole at `F = −2.001012658227848 kg`, the bounded step that
  stops at the zero crossing is [ADR 0049](decisions/0049-the-energy-balance-is-integrated-only-where-it-is-defined.md),
  and the whole of the tenth run is about four seeds in five hundred that reach it.

### And the harness

**[`tests/equivalence/run.py`](../tests/equivalence/run.py)** — 2,125 lines in one file, and the
single most important script in the repository. It runs both implementations, reduces every CSV each of
them writes to a series per (family, scenario, year, sex, variable), classifies each series,
applies the test the classification calls for, corrects every p-value from every family in one Holm
pass, and prints a verdict. Every equivalence claim in every document here is this script's output.

Read [docs/equivalence-method.md](equivalence-method.md) first — the script is the method, and the
method is the readable form of it. Then read `run.py`'s reduction and classification, which are the
two parts where a mistake would say PASS. Its six companions are smaller and each does one thing:
`self_check.py` (the harness against itself and against a deliberately wrong build,
[ADR 0036](decisions/0036-the-harness-is-tested-against-itself.md)), `sweep.py` (many seeds, stored
reductions), `null_check.py` and `calibrate.py` (the false-positive rate, measured), `seed_scan.py`
(the census: many seeds of one implementation, every output file read rather than the exit code),
and `run_test.py` (94 tests over all of it).

Allow two hours for `run.py` and one for the rest. Budget more if you intend to change the rule.

### The rest of `tests/`

74 files, 18,454 lines and 885 tests, laid out to mirror `src/`. Two things to know before you open
them: every test that runs a configuration runs against **two** synthetic fixture packs that differ
in every way a program might have assumed they did not
([ADR 0044](decisions/0044-two-fixture-packs-and-a-parameterised-suite.md)), and
[docs/test-port-map.md](test-port-map.md) says which of the baseline's 471 tests each file descends
from. Read `tests/support/` first; it is what the rest is written in terms of.

## The reference shelf

Not part of the path. Go to them when you have the question.

| | |
|---|---|
| [docs/glossary.md](glossary.md) | every project term a reader meets, one sentence each, with a pointer to where it is defined properly. Seventy of them; keep it open for the first day |
| [docs/backlog.md](backlog.md) | what is left, ranked, with what was closed and what the numbers used to be |
| [docs/server-api.md](server-api.md) | the local server's JSON API, endpoint by endpoint |
| [docs/performance.md](performance.md) | wall time and peak memory against the baseline, and where the time goes |
| [docs/build-notes.md](build-notes.md) | how the baseline reference build was produced, including the four macOS shims |
| [docs/audit/](audit) | the audit of the upstream baseline that preceded this project. The source of every `B-nn`, `N-nn` and `D-nn` finding ID |

---

## The decisions you are most likely to disagree with

Read these arguments before you overturn them. Each is a choice a competent reader could reasonably
have made differently, and each has a record that says what the alternative costs.

1. **Baseline defects are fixed rather than reproduced, so this build never agrees with the
   baseline bit for bit.** The obvious objection is that it makes the comparison harder to
   interpret and every difference arguable. The answer is that reproducing a confirmed defect on
   purpose is worse, and that every deviation carries a flag that puts the old behaviour back so
   its cost is measured rather than argued —
   [ADR 0024](decisions/0024-deviations-recorded-baseline-bugs-fixed.md) and
   [ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md).

2. **The energy balance's guard freezes a person's body composition at zero fat and carries on.**
   Integrating only to the zero crossing uses the model's own trajectory at the edge of the model's
   own domain and invents no coefficient — but a body that has run out of fat then stays at exactly
   zero for the rest of the run, and nobody upstream has ratified that. It is worth at most 0.85%
   of one band's `std_weight` on the three seeds where it fires and the run completes either way —
   [ADR 0049](decisions/0049-the-energy-balance-is-integrated-only-where-it-is-defined.md).

3. **A non-finite value in an output stops the whole run.** The opposite choice — substitute and
   carry on — is what the baseline does, and it is how a `NaN` becomes a zero in a published mean.
   The objection is that a long run dies on one person. The answer is that nothing downstream knows
   what a `NaN` BMI should have been, so choosing a correction would be a modelling decision made
   in the wrong place — [ADR 0050](decisions/0050-no-output-carries-a-number-that-cannot-exist.md).

4. **An intervention the configured model would ignore is refused at load time.** This makes four
   of the six upstream examples unrunnable here with their own policy. The alternative is running
   them and handing back something that looks like a policy evaluation and is not, which is exactly
   what the baseline does — [ADR 0035](decisions/0035-refuse-an-intervention-no-model-applies.md).

5. **Scenarios run one after another rather than in parallel**, which gives up close to half the
   available wall time on a two-scenario run. It buys determinism and it removes the race that makes
   the baseline exit on a signal about one FINCH run in 250 —
   [ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md) and
   [ADR 0026](decisions/0026-parallelism-and-fixed-order-reductions.md).

6. **The comparison excludes the age bands only one implementation can fill.** It is the largest
   exclusion in the method and it removes real cells from the comparison. The argument is that those
   bands measure deviation B-21 rather than agreement, and that including them would make every run
   fail for a reason already recorded —
   [ADR 0027](decisions/0027-equivalence-excludes-the-bands-only-one-side-fills.md).

7. **Pass or fail is a family-wise false-positive rate of 1% under Holm, not a tolerance on a
   number.** A reader who wants "within 0.1%" will find this unsatisfying, and Holm over every test
   a run performs is conservative. The reason is in the record: the previous rule gave between 0
   and 45 failures from the same build against the same baseline depending on which twenty seeds
   were drawn — [ADR 0048](decisions/0048-a-comparison-with-a-stated-false-positive-rate.md).

8. **Risk factors are keyed by index rather than by name**, which costs readability at every call
   site in `src/model/`. It was the single largest performance decision in the project and
   [docs/performance.md](performance.md) has the before and after —
   [ADR 0037](decisions/0037-index-keyed-risk-factor-store.md).

9. **There is a matrix and a Cholesky decomposition in this repository instead of Eigen.** The
   argument is determinism rather than dependency count: a fixed evaluation order is a property this
   project has to be able to state — [ADR 0023](decisions/0023-own-matrix-and-cholesky.md), with
   [ADR 0014](decisions/0014-minimal-dependency-set.md) for the general rule.

10. **The double square root in the demographic standard deviations is reproduced, not fixed.**
    `std_region` and three others are the square root of a number that is already a standard
    deviation, in both implementations, deliberately. It is upstream report 5: a standard deviation
    is a number somebody may have published, so correcting it is the model owners' decision and not
    a reimplementation's — [docs/upstream-reports.md](upstream-reports.md) §5.
