# 0046 — What runs under which sanitizer, and why it is not everything

## Status

Accepted, 2026-09-18. Narrows what [ADR 0044](0044-two-fixture-packs-and-a-parameterised-suite.md)
runs under ThreadSanitizer, and leaves it running everywhere else.

## Context

The previous run added a second synthetic configuration and parameterised every test that runs a
configuration over both ([ADR 0044](0044-two-fixture-packs-and-a-parameterised-suite.md)). It found
three defects on its first run and it is the best value this project has bought with test code.

It also doubled the number of *simulations* the suite runs, and a simulation under ThreadSanitizer
is seconds rather than a fifth of one. The measured split of the tsan preset's 2,529 seconds of
local test time was:

| | Tests | Time | Share |
|---|---:|---:|---:|
| `Packs/` — the parameterised suite | 184 | 1,975 s | 78% |
| the harness's two self-checks | 2 | 501 s | 20% |
| everything else | 662 | 51 s | 2% |

In CI, `macos · appleclang · tsan` went from **48m08s to 88m08s** and became the workflow's long
pole by a wide margin: the next longest entry was 43m13s and the median was under six minutes. Every
push waits for it, and a check that takes an hour and a half is a check people learn to push past.

## Decision

**ThreadSanitizer runs the whole suite against the first fixture pack and at six self-check seeds.
Every other preset runs everything.**

- `tests/support/fixture_packs.cpp` returns one pack under TSan and two otherwise. The condition is
  asked of the compiler — `__has_feature(thread_sanitizer)` or `__SANITIZE_THREAD__` — rather than
  of the build system, so a preset that adds `-fsanitize=thread` by any route gets the same answer.
- `tests/CMakeLists.txt` passes `--seeds 6` to the two `EquivalenceHarness` self-checks when
  `CMAKE_CXX_FLAGS` carries `-fsanitize=thread`, and 20 otherwise.
- **Nothing is dropped from the release preset**, and nothing is dropped from debug or from
  AddressSanitizer either. Both packs and twenty seeds run in three of the four presets and in
  eleven of the fifteen CI jobs.

## Why this shape

**A race is a property of the code, not of the configuration that reaches it.** The threading in
this program is the parallel sections, the server's request handling and the run queue — the same
code on both packs. What the second pack buys is a *logic* check: an assumption about one
configuration's file layout, output name, scenario set or disease set, which is what its three
findings were. That check is worth running wherever it is cheap, and it is cheap in release
(0.15 s a test) and under AddressSanitizer. It is not worth forty minutes of every push to run the
same races a second time.

**Twenty seeds is a statistical choice about a comparison against the baseline.** What the two
self-checks run is not that comparison: it is the harness's own logic — does it pass when it should,
does it fail when it should — over a simulation TSan is watching. Six seeds still answers both
questions, and that was checked rather than assumed before the number was changed: at six seeds the
perturbation is detected in `mean_bmi`, `mean_energy` and `std_energy`, the same three variables as
at twenty, and the seeds mode still makes 1,074 comparisons. The full count runs in the other three
presets.

**The self-checks are a fifth of the preset for two tests**, which is the sort of ratio worth
looking at before dropping anything. Their cost is 40 simulations each; six seeds makes it 12.

## What it costs

**A race reachable only through the second pack's configuration and not the first's would not be
found by TSan.** That is the whole of the risk and it is worth stating plainly rather than
explaining away. Two things bound it: nothing found in six runs has been of that shape — the races
this project has had were in the server's lifetime, the analysis module's shared table and the
baseline's own scenario threading, none of them configuration-dependent — and both packs still run
under AddressSanitizer, which catches a different class of the same mistakes.

**And the risk is asymmetric in the useful direction**: the coverage that was dropped is a duplicate
of coverage that still runs, while what is bought is a check the whole team will actually wait for.

## Alternatives rejected

**`ctest -j` on the sanitizer presets.** The obvious answer, and it remains
[docs/backlog.md](../backlog.md) item 11 rather than this run's answer: some of these tests are
*about* threads — the byte-identity-at-N-threads tests spawn workers, the server tests bind sockets,
the stress test runs several clients — so the right degree of parallelism has to be found rather
than assumed, and under a sanitizer the memory cost multiplies too. It is also orthogonal: doing it
later multiplies whatever this leaves.

**Drop the TSan job to a nightly or a weekly.** It would cut the push-to-answer time to nothing and
it would also mean a race lands on `main` and sits there. The four defects TSan has caught in this
project were all caught on the push that introduced them.

**Run the second pack under TSan but with a shorter horizon.** A configuration with a different
horizon is a third configuration, and the packs' own rule is that a test asks the loaded
configuration rather than assuming one ([ADR 0044](0044-two-fixture-packs-and-a-parameterised-suite.md)).
Shortening a pack for one preset would make the two presets' failures mean different things.

**Keep it as it was.** 88 minutes is not fatal; it is the thing a person works around. The reason
not to keep it is what a slow check does to the people using it, which is not a technical argument
and is the real one.

## Consequences

- The TSan suite is 761 tests rather than 853. The difference is exactly the second pack's
  parameterised instantiations, and the count is in [docs/SUMMARY.md](../SUMMARY.md).
- `macos · appleclang · tsan` and `linux · clang · tsan` fall by roughly half; the measured figures
  are in [docs/SUMMARY.md](../SUMMARY.md), read per job with `gh run view`.
- Anything added to the parameterised suite from now on is, by construction, half-covered under
  TSan. That is the intended reading: put a *threading* test somewhere other than the parameterised
  suite, and it will run under TSan against everything.
