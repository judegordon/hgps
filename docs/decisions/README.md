# Architecture Decision Records

Fifty records, one per design choice, each stating the context, the decision, the alternatives that
were rejected and the consequences ([ADR 0001](0001-record-architecture-decisions.md)). They are
numbered in the order they were taken, which is roughly the order the project happened in, and they
are never rewritten: a decision that was later changed is superseded by a new record rather than
edited.

**This file is the index, and it is checked.** `scripts/check.sh` fails if an ADR file exists with
no line here, or if a line here names a file that does not exist, so the index cannot drift from
the directory. [docs/READING-GUIDE.md](../READING-GUIDE.md) carries the same index in reading
order and is checked against the same list.

Below, the records are grouped by theme and numbered in order within each group. A reader going
through them end to end can take the groups in this order; a reader looking for one decision can
search for the number.

## Provenance, licence, and how a difference from the baseline is recorded

- [0001 — Record architecture decisions](0001-record-architecture-decisions.md) — why there is an ADR directory at all, and what belongs in one.
- [0002 — This is a BSD-3-Clause derivative of the baseline](0002-licence-bsd-3-clause-derivative.md) — the licence, the notice, and the statement of derivation.
- [0003 — The four source folders are read-only; the baseline builds out of tree](0003-read-only-sources-and-out-of-tree-baseline-build.md) — the upstream checkouts are evidence, so nothing here may write to them.
- [0024 — Baseline bugs are fixed, not reproduced; every deviation is recorded](0024-deviations-recorded-baseline-bugs-fixed.md) — bit-exact agreement is explicitly not a goal, and the price is a register of differences.
- [0041 — A deliberate deviation is always switchable, so its effect can be measured](0041-deliberate-deviations-are-switchable.md) — every deviation that moves a number gets a compatibility flag, which is what makes its cost a measurement.

## Toolchain, platforms and dependencies

- [0004 — C++20, CMake presets, pinned vcpkg, GoogleTest, warnings as errors](0004-toolchain-cpp20-cmake-vcpkg-googletest.md) — the build, and why every preset is in the one script.
- [0013 — Linux and macOS supported from the start; Windows not targeted](0013-platforms-linux-and-macos.md) — two platforms from day one, because one platform hides assumptions.
- [0014 — Three dependencies; implement SHA-256, CSV, matrix and Cholesky; delegate fetch and unzip](0014-minimal-dependency-set.md) — what is written here rather than pulled in, and the rule that decided each case.
- [0019 — Split the monoliths into one unit per concern](0019-split-the-monolith-translation-units.md) — why the baseline's 2,000-line translation units are not reproduced.
- [0023 — A small dense matrix with an explicit Cholesky, instead of Eigen](0023-own-matrix-and-cholesky.md) — a determinism argument, not a dependency-count one.

## Module layout, the published API, and the three hosts

- [0005 — Module layout: nine layers, dependencies one way](0005-module-layout.md) — the dependency order every reader should hold in their head; [docs/design.md](../design.md) §2 draws it.
- [0032 — The engine is a library with a published API; the CLI is a client of it](0032-library-and-a-thin-cli.md) — `include/hgps/` is the whole surface, and the CLI cannot see past it.
- [0033 — Progress is an event stream the simulation cannot see](0033-an-event-stream-the-simulation-cannot-see.md) — how a host watches a run without the run knowing it is watched.
- [0042 — `hgps serve`: a local JSON server, in the same binary, on cpp-httplib](0042-a-local-server-in-the-same-binary.md) — loopback only, no authentication, and why that is the safe combination.
- [0043 — A plain TypeScript frontend, with Vite and no framework](0043-a-plain-typescript-frontend.md) — what the frontend is allowed to be, given that it is not the product.

## Determinism, randomness and identity

- [0008 — The determinism contract, enforced by types](0008-determinism-contract-enforced-by-types.md) — the fourteen clauses, and the mechanism that makes each a compile error rather than a review comment.
- [0015 — RNG: no default constructor, rejection sampling, explicit 53-bit doubles](0015-rng-design.md) — an unseeded generator does not compile, and a modulo bias cannot come back.
- [0016 — `Categorical<T>`: sampling from an ordered sequence, by construction](0016-categorical-ordered-sampling.md) — a distribution cannot be built from an unordered container.
- [0017 — Monotonic lifetime-unique person IDs, initial cohort `1..N` by index, O(1) free slots](0017-person-ids-monotonic-and-free-slots.md) — identity that survives death and immigration without a scan.
- [0025 — `Identifier` compares its string; the hash is for bucketing only](0025-identifier-string-equality.md) — the baseline's hash-only equality is finding B-04, and this is the fix.
- [0026 — Sequential by default; parallel only for RNG-free work, with fixed-order reductions](0026-parallelism-and-fixed-order-reductions.md) — why `--threads N` cannot change a number.

## Diagnostics, and validating the input before anything runs

- [0007 — Two-tier diagnostics: internal errors thrown, input issues accumulated](0007-two-tier-diagnostics.md) — the distinction everything else in this area rests on.
- [0018 — No swallowing catch blocks; every name is validated at load time](0018-no-swallowing-catch-load-time-validation.md) — a bad name is a located error before the first simulated year, not a zero in a result file.
- [0022 — Validate config in code, not with an embedded JSON-Schema validator](0022-hand-written-config-validation.md) — what a hand-written validator buys that a schema cannot.
- [0028 — A model file's relative paths resolve against the model file, and its generated factors count as known names](0028-model-files-resolve-their-own-relative-paths.md) — the rule that makes an upstream model file loadable where it sits.
- [0035 — An intervention the configured model would ignore is refused at load time](0035-refuse-an-intervention-no-model-applies.md) — upstream report 2 turned into a refusal; the alternative is a silent no-op that looks like a policy evaluation.

## Configuration, data, and the stores

- [0010 — Define config v2 and ship a v1→v2 converter](0010-config-v2-and-a-converter.md) — the upstream examples are read through a converter rather than edited.
- [0011 — Do not vendor the disease data; fetch with a required checksum, and generate synthetic fixtures](0011-data-fetched-not-vendored.md) — a licence constraint that produced the fixture packs everything is tested on.
- [0012 — `pulmonary` is canonical; the registry is validated against the tree](0012-disease-naming-pulmonary.md) — one disease name, and a check that the registry and the data agree.
- [0037 — A person's risk factors are keyed by index, not by name](0037-index-keyed-risk-factor-store.md) — the single largest performance decision in the project; [docs/performance.md](../performance.md) has the before and after.
- [0038 — Population impact fraction, loaded from the manifest and validated at load](0038-population-impact-fraction.md) — how PIF is loaded, and why it has still never met the baseline.
- [0039 — A scratch directory copies what it may write and links only what it reads](0039-scratch-directories-copy-what-they-may-write.md) — how many runs share one data pack without any of them mutating it.
- [0040 — A bounded binary search for the long vectors, and a scan for the short ones](0040-a-bounded-search-for-the-long-vectors.md) — a lookup decision made from a measurement rather than a preference.

## The model surfaces, scenarios and interventions

- [0009 — Scenarios run sequentially; net migration travels in a journal](0009-sequential-scenarios-and-the-migration-journal.md) — the design that removes the baseline's two-thread scenario race, which is upstream report 4.
- [0021 — Scope for this run: the FINCH and HLM_France surfaces, with the `simple` intervention](0021-scope-finch-and-hlm-france.md) — the historical scope record; the surface is wider now, and this is where it started.
- [0029 — One shape for the five age-banded interventions, and a parameter struct for the model families](0029-one-banded-intervention-shape.md) — five upstream intervention types that are one shape with different numbers.
- [0030 — The converter selects a policy scenario for the FINCH example, rather than the example being edited](0030-policy-scenario-selection-for-the-broken-finch-example.md) — what to do when an upstream config names files its own pack does not contain.
- [0049 — The energy balance is integrated only as far as the model is defined](0049-the-energy-balance-is-integrated-only-where-it-is-defined.md) — the body-fat pole, the bound that stops at it, and why that particular bound and no other. Flag `B-29`.

## Output, and the record a run leaves behind

- [0020 — One owner per output file; rows in a defined order](0020-output-single-owner-defined-row-order.md) — the rule that makes byte-identical output possible at all.
- [0034 — Every run writes a manifest beside its results](0034-a-run-manifest-beside-the-results.md) — config hash, data checksum, the seed actually used, the commit, and every located warning.
- [0050 — No output carries a number that cannot exist](0050-no-output-carries-a-number-that-cannot-exist.md) — upstream report 7: the run stops where nothing downstream could know the right answer. Flag `B-30`.

## Testing strategy and the fixture packs

- [0006 — Validation: port the 471 tests, then statistical equivalence](0006-validation-strategy.md) — the two layers, and why neither is sufficient alone.
- [0036 — The equivalence harness is tested against itself, including a deliberately wrong build](0036-the-harness-is-tested-against-itself.md) — a harness mistake says PASS, so the harness needs its own suite.
- [0044 — Two synthetic fixture packs, and every test that runs a configuration runs against both](0044-two-fixture-packs-and-a-parameterised-suite.md) — a test cannot pass by assuming what one pack happens to say.
- [0045 — End-to-end tests in a real browser, with Playwright](0045-end-to-end-tests-in-a-real-browser.md) — what a browser catches that a unit test on the same code does not.
- [0046 — What runs under which sanitizer, and why it is not everything](0046-what-runs-under-which-sanitizer.md) — the reason the CTest count under TSan is smaller than under release.
- [0047 — The second fixture pack carries income, region, ethnicity and sector](0047-the-second-pack-carries-the-stratified-dimensions.md) — what made every output family this engine can write reachable from a fixture.

## The equivalence comparison

- [0027 — The equivalence comparison excludes the age bands immigration cannot fill, and tests a rate where normal theory does not hold](0027-equivalence-excludes-the-bands-only-one-side-fills.md) — the first and largest exclusion, and the honest reason for it.
- [0031 — Each intervention is compared on its own, and FINCH borrows its definitions](0031-comparing-one-intervention-at-a-time.md) — why interventions are not compared as a set.
- [0048 — The comparison's threshold is a stated false-positive rate, not an allowance](0048-a-comparison-with-a-stated-false-positive-rate.md) — the rule the whole comparison now rests on, and the null it was measured against before being adopted.
