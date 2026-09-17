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
deleting the 471-test suite that was the only evidence any of it was correct. Four more baseline
defects were found here, by running code the baseline's own tests never reach; they are in
[docs/deviations.md](docs/deviations.md) as B-21 to B-24.

The one requirement that shapes everything here follows from what the model is *for*. A comparison
you cannot reproduce is not evidence, so:

> **Same config + same seed + same data + same binary ⇒ byte-identical CSV output, every run,
> regardless of thread count.**

That contract is enforced by the type system rather than by review discipline — an unseeded
generator does not compile, an RNG handle cannot reach a worker thread, a probability distribution
cannot be built from an unordered container, and every reduction has a fixed order independent of
the thread count. [docs/design.md](docs/design.md) §4 lists all fourteen clauses and the mechanism
that enforces each one.

## Documentation

| | |
|---|---|
| [docs/design.md](docs/design.md) | module layout, data flow, the determinism contract, the parallelism model, config v2, I/O formats |
| [docs/decisions/](docs/decisions) | one Architecture Decision Record per design choice, with the alternatives that were rejected |
| [docs/deviations.md](docs/deviations.md) | every behaviour that intentionally differs from the baseline, with the audit finding ID and the evidence |
| [docs/build-notes.md](docs/build-notes.md) | how the baseline reference build was produced, and what its test suite reports |
| [docs/examples.md](docs/examples.md) | which upstream examples run here and which are out of scope |
| [docs/equivalence.md](docs/equivalence.md) | the statistical equivalence harness, its tolerances and its results |
| [docs/performance.md](docs/performance.md) | wall time and peak memory, this implementation versus the baseline |
| [docs/test-port-map.md](docs/test-port-map.md) | where each of the baseline's 471 tests went, suite by suite |
| [docs/backlog.md](docs/backlog.md) | what is left, ranked |
| [docs/SUMMARY.md](docs/SUMMARY.md) | what was built, what passes, what is still open |

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
output is byte-identical either way. `--help` lists the rest.

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

## Validating

```bash
export VCPKG_ROOT=/path/to/vcpkg
scripts/check.sh          # every preset, every test, plus the equivalence harness
scripts/check.sh --fast   # release only
```

Validation has two layers ([ADR 0006](docs/decisions/0006-validation-strategy.md)):

- **The baseline's test suite, ported.** Its 471 tests were gone through one by one, keeping each
  test's intent and — wherever the numbers are the point — its expected values unchanged. Where a
  baseline test encoded one of the audit's findings, the expectation is changed and the finding ID
  is named in the test. 408 have a counterpart here; 18 do not because population impact fraction
  is out of scope, and 34 do not because the thing they test does not exist here by design — the
  event bus, the sync channel, the lazy repository, the printed summary boxes.
  [docs/test-port-map.md](docs/test-port-map.md) says which, suite by suite. **The 35 tests the
  baseline skips run here**, and finding out whether they pass is how four defects were found. 220
  tests are new, including byte-for-byte reproducibility at one thread and at N for every
  intervention, a modulo-bias regression test, ordered-sampling tests, and a test that an unseeded
  config is rejected. 548 in total.
- **Statistical equivalence against the baseline** on both reference examples — `HLM_France` for
  the HLM surface and `KevinHall_FINCH` for the FINCH one — over at least 20 seeds, comparing
  means, standard deviations and percentiles per output variable per year per scenario per sex,
  within tolerances argued for in [docs/equivalence.md](docs/equivalence.md). Any divergence that
  is not explained by a recorded deviation fails the check; there is no failure budget.

Bit-exact agreement with the baseline is deliberately **not** a goal: it would require reproducing
four of the audit's confirmed defects on purpose.
