# Health-GPS — deterministic reimplementation

A reimplementation of [Health-GPS](https://github.com/imperialCHEPI/healthgps), the agent-based
population-health microsimulation from Imperial College London's Centre for Health Economics &
Policy Innovation and INRAE. It builds a synthetic national cohort from survey microdata and
demographic projections, ages it a year at a time, evolves each person's risk factors, applies
disease incidence, remission and mortality, and reports burden-of-disease statistics — comparing a
**baseline** future against a **policy intervention** future run from the same seed.

This is a BSD-3-Clause derivative of that work; see [LICENSE](LICENSE) for the notice and the
statement of derivation.

## Why it exists

An audit of the upstream baseline, its data and examples, and an earlier rewrite is in
[docs/audit/](docs/audit) — start with [SUMMARY.md](docs/audit/SUMMARY.md). It found 19 confirmed
defects in the baseline, six of them high severity, and a rewrite that fixed twelve of them while
deleting the 471-test suite that was the only evidence any of it was correct. Seven more baseline
defects were found here, by running code the baseline's own tests never reach; they are in
[docs/deviations.md](docs/deviations.md) as B-21 to B-24 and B-26 to B-28.

The one requirement that shapes everything here follows from what the model is *for*. A comparison
you cannot reproduce is not evidence, so:

> **Same config + same seed + same data + same binary ⇒ byte-identical CSV output, every run,
> regardless of thread count.**

That contract is enforced by the type system rather than by review discipline — an unseeded
generator does not compile, an RNG handle cannot reach a worker thread, a probability distribution
cannot be built from an unordered container, and every reduction has a fixed order independent of
the thread count. [docs/design.md](docs/design.md) §4 lists all fourteen clauses and the mechanism
that enforces each one.

## Three ways to use it

The simulation is a **library**; everything else is a client of it
([ADR 0032](docs/decisions/0032-library-and-a-thin-cli.md)). All three produce byte-identical
output from the same inputs, and a test asserts that rather than claiming it.

| | What it is | Start with |
|---|---|---|
| **Library** | `hgps::engine`, whose whole surface is `include/hgps/`. Load a configuration, resolve its data, build a run, execute it — with an event stream, a cancellation token and located diagnostics | [docs/api.md](docs/api.md) |
| **Command line** | `healthgps --config FILE`. A thin client of the library: it parses arguments, subscribes to the events, prints, and chooses an exit code | [Running](#running), below |
| **Graphical** | `healthgps serve --web web/dist`, then a browser. A JSON API over the library and a single-page app on top of it: edit a configuration against the published schema, start a run and watch it, read the results as tables and charts. Driven end to end by a browser in CI ([ADR 0045](docs/decisions/0045-end-to-end-tests-in-a-real-browser.md)) | [docs/server-api.md](docs/server-api.md) |

```bash
# The graphical host: build the frontend once, then one binary and one folder.
(cd web && npm ci && npm run build)
./out/build/release/src/healthgps serve --web web/dist
#   → http://127.0.0.1:8080
```

`hgps serve` binds to **loopback only** and refuses anything else before opening the socket. There
is no authentication, and that is only safe because it cannot be reached from another machine: a
configuration names files to read and a folder to write
([ADR 0042](docs/decisions/0042-a-local-server-in-the-same-binary.md)).

`scripts/dev.sh` runs the engine and the frontend's dev server together, for working on the latter.

## Documentation

| | |
|---|---|
| [docs/design.md](docs/design.md) | module layout, data flow, the determinism contract, the parallelism model, config v2, I/O formats |
| [docs/api.md](docs/api.md) | the public C++ API: the four calls, the handles, the event stream, cancellation, what it throws |
| [docs/server-api.md](docs/server-api.md) | the local server's JSON API: every endpoint, the event stream, and what it deliberately does not do |
| [docs/decisions/](docs/decisions) | one Architecture Decision Record per design choice, with the alternatives that were rejected |
| [docs/deviations.md](docs/deviations.md) | every behaviour that intentionally differs from the baseline, with the audit finding ID and the evidence |
| [docs/build-notes.md](docs/build-notes.md) | how the baseline reference build was produced, and what its test suite reports |
| [docs/examples.md](docs/examples.md) | which upstream examples run here, and the two that neither implementation can run |
| [docs/equivalence.md](docs/equivalence.md) | the statistical equivalence harness, its tolerances and its results |
| [docs/equivalence-method.md](docs/equivalence-method.md) | the method alone: the reduction, the exclusions, the allowance and how it is derived |
| [docs/performance.md](docs/performance.md) | wall time and peak memory, this implementation versus the baseline |
| [docs/test-port-map.md](docs/test-port-map.md) | where each of the baseline's 471 tests went, suite by suite |
| [docs/backlog.md](docs/backlog.md) | what is left, ranked |
| [docs/SUMMARY.md](docs/SUMMARY.md) | what was built, what passes, what is still open |
| [docs/briefing.md](docs/briefing.md) | the short form for the upstream authors: what this proves, what it found in the baseline, and what only they can decide |
| [docs/upstream-reports.md](docs/upstream-reports.md) | four findings written up as bug reports against the baseline, each with the command that reproduces it there |

## Building

Linux and macOS. C++20, CMake ≥ 3.24, Ninja, and vcpkg in manifest mode. Windows is not a target
([ADR 0013](docs/decisions/0013-platforms-linux-and-macos.md)).

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset release
cmake --build --preset release
```

Presets: `release`, `debug`, `asan-ubsan`, `tsan`. Dependencies are `fmt`, `nlohmann-json` and
`gtest` and nothing else — SHA-256, CSV parsing, the dense matrix and its Cholesky decomposition are
implemented here, and HTTP download and zip extraction are delegated to `curl` and `unzip`
([ADR 0014](docs/decisions/0014-minimal-dependency-set.md)).

The build produces a library and a program. `hgps::engine` is the simulation; `healthgps` is a
command-line client of it that parses arguments, prints, and does nothing else
([ADR 0032](docs/decisions/0032-library-and-a-thin-cli.md)).

## Using it as a library

```cmake
add_subdirectory(hgps_new_rewrite)
target_link_libraries(my_host PRIVATE hgps::engine)
```

The whole API is `include/hgps/`, and it is four calls: load a configuration, resolve its data, build
a run, execute it.

```cpp
#include "hgps/engine.h"

hgps::api::Report report;
const auto configuration = hgps::api::load_configuration(path, {}, report);
if (!configuration) { std::cerr << report.to_string(); return 3; }

const auto data = hgps::api::resolve_data(*configuration, report);
auto run = hgps::api::build_run(*configuration, *data, report);
const auto summary = hgps::api::execute(*run, {}, &my_subscriber, token, report);
```

Each of the first three accumulates every problem it finds into the `Report` and returns nothing if
any of them is an error, so a host can validate a configuration in milliseconds without touching the
network and show the user everything that is wrong at once. `execute` takes an `EventSubscriber` —
run started, scenario started, year completed, run completed — and a `CancellationToken` that stops
the run at the end of its current year. The library writes to neither `stdout` nor `stderr`; there is
no stream in its API.

[docs/api.md](docs/api.md) is the contract: what the handles own, what the events guarantee, and what
is not there yet. The example above is abridged; the one in that document is compiled and run by the
test suite.

## Running

```bash
# Convert an upstream config (v1) to this repository's config v2 format
./out/build/release/tools/convert-config \
    --input  ../hgps_main_examples/HLM_France/config.json \
    --output examples/HLM_France/config.json --rebase --check

# Run it
./out/build/release/src/healthgps --config examples/HLM_France/config.json

# Validate everything without simulating: config, models, data index, disease registry
./out/build/release/src/healthgps --config examples/HLM_France/config.json --dry-run
```

`--threads N` sets the worker count for the RNG-free parallel sections; the default is one, and the
output is byte-identical either way. `--progress` prints a line as each simulated year finishes.
`--help` lists the rest.

Every run writes a **manifest** JSON beside its results — the config hash, the data checksum, the seed
actually used, the engine version and commit, the host platform, the start and end times, and the
scenarios that ran ([ADR 0034](docs/decisions/0034-a-run-manifest-beside-the-results.md)). The result
CSVs are unchanged by its existence.

`convert-config` also takes `--policy-scenario S1..S7`, which selects one of the seven modelled
policy scenarios the FINCH data pack ships. It matters for exactly one example, whose static model
names a policy file the pack does not contain
([ADR 0030](docs/decisions/0030-policy-scenario-selection-for-the-broken-finch-example.md)).

Disease data is **not** vendored here: it is CC BY-NC-ND and is fetched on demand from a release
URL with a required SHA-256 checksum, then cached content-addressed
([ADR 0011](docs/decisions/0011-data-fetched-not-vendored.md)). A local data directory works too.
For offline runs there is a synthetic fixture pack:

```bash
./out/build/release/tools/gen-fixtures --output tests/fixtures/pack
```

Its numbers are invented. It exists so that tests and CI can run without the network, and it must
never be used for analysis.

`gen-fixtures` writes **two** configurations over that one data store: `model/`, and `model-b/`,
which differs from it in its file layout, output folder and file name, scenario set, disease set,
seed, horizon and age range. Every test that runs a configuration runs against both, so a test
cannot pass by assuming what one of them happens to say
([ADR 0044](docs/decisions/0044-two-fixture-packs-and-a-parameterised-suite.md)).

## Validating

```bash
export VCPKG_ROOT=/path/to/vcpkg
scripts/check.sh            # every preset, the frontend, the browser, the equivalence harness,
                            # and the column inventory of every output family
scripts/check.sh --fast     # release only, and the frontend without its browser tests
scripts/check.sh --no-web   # the C++ only
```

`.github/workflows/ci.yml` is the same work split across jobs: four presets on Linux and macOS,
Linux with both clang and GCC, the frontend's type-check and unit tests, a browser driving the built
frontend against a real server, the equivalence harness against the checked-in references, and an
indicative Linux timing. It does not build the baseline — see the comment at the top of that file
for why.

Validation has two layers ([ADR 0006](docs/decisions/0006-validation-strategy.md)):

- **The baseline's test suite, ported.** Its 471 tests were gone through one by one, keeping each
  test's intent and — wherever the numbers are the point — its expected values unchanged. Where a
  baseline test encoded one of the audit's findings, the expectation is changed and the finding ID
  is named in the test. **426 have a counterpart here**, and 34 do not because the thing they test
  does not exist here by design — the event bus, the sync channel, the lazy repository, the printed
  summary boxes. [docs/test-port-map.md](docs/test-port-map.md) says which, suite by suite. **The 35
  tests the baseline skips run here**, and finding out whether they pass is how four defects were
  found. Of the **863** tests here, most are in files the baseline has no counterpart for —
  byte-for-byte reproducibility at one thread and at N for every intervention, a modulo-bias
  regression test, ordered-sampling tests, and a test that an unseeded config is rejected, among
  others. A further **63** test the equivalence harness's own statistics, because a mistake there
  says PASS rather than producing a wrong number. Every test that runs a configuration runs against
  **two** synthetic packs, which differ in every way a program might have assumed they did not
  ([ADR 0044](docs/decisions/0044-two-fixture-packs-and-a-parameterised-suite.md)) — one of them
  under ThreadSanitizer, where running the same races twice was the largest single cost in CI
  ([ADR 0046](docs/decisions/0046-what-runs-under-which-sanitizer.md)). The second pack's static
  model is `StaticLinear`, which is what gives a person an income category, a region, an ethnicity
  and a sector, and therefore what makes **every output family this engine can write** produced by
  a fixture ([ADR 0047](docs/decisions/0047-the-second-pack-carries-the-stratified-dimensions.md)).
- **Statistical equivalence against the baseline** on three examples — `HLM_France` for the HLM
  surface, `KevinHall_FINCH` for the FINCH one, and `HLM_India` for the `EBHLM` dynamic model at a
  reduced cohort — over at least 20 seeds and again at 60, comparing means, standard deviations and
  percentiles per output variable per year per scenario per sex, within tolerances argued for in
  [docs/equivalence.md](docs/equivalence.md). Any divergence that is not explained by a recorded
  deviation fails the check. There is one failure budget, of **3 comparisons on `KevinHall_FINCH`**
  out of 111,836, and it exists because the allowance's width is estimated from the same twenty
  draws it is judging: a series at a small signed offset well inside its allowance fails in every
  year at once when a seed set happens to give a tight sample. Three are out of tolerance at 20
  seeds and four at 60, and no cell fails in both.
  [docs/equivalence.md](docs/equivalence.md) records the measurement that sized it and
  [docs/backlog.md](docs/backlog.md) item 6 is the work that removes it.

Bit-exact agreement with the baseline is deliberately **not** a goal: it would require reproducing
several of the audit's confirmed defects on purpose. One of them is now visible in the numbers —
the `HLM_India` comparison measures deviation B-24, the baseline's double-applied food-labelling
impact, at about +0.2% of mean BMI in the intervention scenario.
