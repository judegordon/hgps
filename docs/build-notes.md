# Build notes — upstream baseline reference build

The baseline (`hgps_main`, Health-GPS `3.0.0.0`, BSD-3-Clause) is the behavioural reference for this
implementation. It has to be buildable and runnable on the development host so that the equivalence
harness (`tests/equivalence/`) has something to compare against. This file records exactly how it
was built and what its test suite reports, so the result can be reproduced without rediscovering
the four macOS workarounds.

**The baseline source tree is read-only.** Nothing under `hgps_main/` was modified. All build
artefacts live under `/tmp/hgps-build/`.

## Host

| | |
|---|---|
| Date built | 2026-09-17 |
| OS | macOS 26.6.2 (Darwin 25.6.0), Apple Silicon (arm64) |
| Compiler | Apple clang 21.0.0 (clang-2100.3.34.2), libc++ |
| CMake | 4.4.3 (Homebrew) |
| Ninja | 1.13.2 |
| vcpkg | pinned to the baseline's declared `builtin-baseline` `bd2b54836beed96e1efbe9aaf8ee800f5448856d` |

## The four shims

The baseline does not build on macOS as shipped — `docs/audit/04-baseline-issues.md` (B-10) records
this as a baseline portability defect. Four workarounds are needed, identical to the ones the audit
used (`docs/audit/00-inventory.md` §4.2). They are build-time only and live in
`/tmp/hgps-build/shim/`:

| # | Obstacle | Workaround | Baseline defect? |
|---|---|---|---|
| 1 | Old vcpkg ports declare `cmake_minimum_required(VERSION <3.5)`, rejected by CMake 4 | `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` | No — environmental (vcpkg baseline age vs CMake 4) |
| 2 | `openssl` fails to compile: empty `VCPKG_OSX_SYSROOT` for the stock `arm64-osx` triplet produces a malformed `-isysroot -g` | overlay triplet `/tmp/hgps-build/triplets/arm64-osx.cmake` setting `VCPKG_OSX_SYSROOT` to the Xcode SDK path | No — environmental |
| 3 | `std::osyncstream` is forward-declared in Apple libc++'s `<iosfwd>` but has no definition and no `<syncstream>` header (6 use sites) | shim header `shim/syncstream` supplying a minimal `std::basic_osyncstream` | **Yes** (B-10) |
| 4 | `#error "Unsupported platform"` in `program_dirs.cpp`; `std::execution::par` needs `-fexperimental-library`; Apple's `libc++.dylib` does not export `__libcpp_atomic_wait` | `-D__linux__=1` plus a forced-include (`shim/macos_compat.h`) redirecting `readlink("/proc/self/exe")` to `_NSGetExecutablePath`; `-fexperimental-library`; one stub object (`shim/atomic_wait_stub.o`) implementing `__libcpp_atomic_wait` as a spurious wake-up — valid because every libc++ caller re-checks its predicate | **Yes** for the platform `#error` (B-10); the libc++ items are environmental |

The new implementation avoids all four by construction: it targets Linux and macOS from the start,
uses no PSTL and no `<syncstream>`, and has one `__APPLE__` branch for executable-path lookup
(`docs/decisions/0013-platforms-linux-and-macos.md`).

## Configure and build

Reproduced by `/tmp/hgps-build/configure-baseline.sh`:

```bash
SHIM=/tmp/hgps-build/shim
cmake -S /Users/jude/work/hpgs/hgps_main -B /tmp/hgps-build/baseline-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/hgps-build/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_OVERLAY_TRIPLETS=/tmp/hgps-build/triplets \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_TESTING=ON \
  -DCMAKE_CXX_FLAGS="-I$SHIM -include $SHIM/macos_compat.h -D__linux__=1 -fexperimental-library" \
  -DCMAKE_EXE_LINKER_FLAGS="-fexperimental-library $SHIM/atomic_wait_stub.o"
cmake --build /tmp/hgps-build/baseline-release
```

**Result: exit 0.** Two binaries:

- `/tmp/hgps-build/baseline-release/src/HealthGPS.Console/HealthGPS.Console` (reports `Version 3.0.0.0`)
- `/tmp/hgps-build/baseline-release/src/HealthGPS.Tests/HealthGPS.Tests`

Compiler warnings from baseline sources, at the project's own `-Wall -Wextra -Wpedantic`: **7
distinct**, none new relative to the audit —

```
analysis_module.cpp:821:17   variable 'processed' set but not used
analysis_module.cpp:1221:17  variable 'processed_income' set but not used
analysis_module.cpp:1847:17  variable 'processed_std' set but not used
kevin_hall_model.h:238:71    private field 'nutrient_ranges_' is not used
kevin_hall_model.h:241:72    private field 'food_prices_' is not used
individual_id_tracking_writer.h:30:5   explicitly defaulted move constructor is implicitly deleted
individual_id_tracking_writer.h:31:33  explicitly defaulted move assignment operator is implicitly deleted
```

The audit reported 4 (counting duplicates across translation units and only
`-Wunused-but-set-variable`); the two `kevin_hall_model.h` and two
`individual_id_tracking_writer.h` warnings are emitted per including translation unit and were not
itemised there. All are baseline issues (B-19 covers the first three); none is caused by the shims.

## Test suite

```
$ ./src/HealthGPS.Tests/HealthGPS.Tests
[==========] Running 471 tests from 48 test suites.
[==========] 471 tests from 48 test suites ran. (252 ms total)
[  PASSED  ] 436 tests.
[  SKIPPED ] 35 tests, listed below:
```

**471 registered / 436 passed / 35 skipped / 0 failed — as expected.** `ctest -N` also reports
`Total Tests: 471`, and `ctest` exits 0.

Skips by suite:

| Suite | Skipped |
|---|---:|
| `KevinHallHeight` | 22 |
| `KevinHallWeightQuantiles` | 7 |
| `ModelParserFinch` | 3 |
| `TestSimulation` | 2 |
| `KevinHallWeightValidation` | 1 |

All 35 have one cause (B-11): a `__FILE__`-relative fixture path
`hgps_main/input-data/data/KevinHall_FINCH` that exists in neither upstream data repository. The
new implementation does not reproduce this — those tests point at the synthetic fixture pack or the
converted FINCH example and **fail rather than skip** when the fixture is missing
(`docs/decisions/0006-validation-strategy.md`).

## Logs

- `/tmp/hgps-build/logs/baseline-build.log`
- `/tmp/hgps-build/logs/baseline-tests.log`

---

## Third run, orientation: what had drifted

Re-checked on **2026-09-18**, before any change, on the same host as above (macOS 26.6.2 /
Darwin 25.6.0, Apple Silicon). Nothing had drifted in a way that needed fixing.

| | Recorded in SUMMARY.md | Measured now |
|---|---|---|
| Baseline build under `/tmp/hgps-build/` | present | **present, not rebuilt** — the console and test binaries from 2026-09-17 were still there and still ran |
| `scripts/check.sh` | exit 0 | **exit 0** |
| Tests, all four presets | 555 | **555, all passing** in release, debug, asan-ubsan and tsan |
| `HLM_France` equivalence, 20 seeds | 31,468 comparisons, 0 out of tolerance | **31,468, 0** |
| `KevinHall_FINCH` equivalence, 20 seeds | 22,679 comparisons, 0 out of tolerance | **22,679, 0** |
| `KevinHall_FINCH` excluded bands | — | 692, matching the stored reference's manifest |

The two stored references were used rather than re-running the baseline (`--use-reference`), which
is what `scripts/check.sh` does; both config hashes matched the references, so neither example
needed the baseline binary at all.

**Preset timings, and why they are slower than the previous run's.** Release 8.7 s, debug 80.6 s,
asan-ubsan 274.6 s, tsan 564.3 s, against 8 / 76 / 219 / 523 s recorded before. The machine was not
idle: this run's own compilation was going on in another shell for most of the sanitizer presets.
The previous run already recorded that a background indexer was worth 20% of a wall-clock
measurement ([docs/performance.md](performance.md)), and this is the same effect. No timing here is
used as a budget; [docs/performance.md](performance.md)'s numbers are taken on an idle machine.

**One accident worth recording, because it turned into evidence.** The release binary was rebuilt —
with the library/CLI split applied — while the `KevinHall_FINCH` sweep was between its 13th and
14th seed. So seeds 1–13 were produced by the pre-split binary and seeds 14–20 by the post-split
one, and all 22,679 comparisons against the one stored baseline reference passed. That is not how
the check was meant to run, and it is a real if unplanned cross-check that the split changed no
number on that example. The deliberate version of the same check — both references re-run against
the split build — is in [docs/equivalence.md](equivalence.md).

### The PIF data release

`KevinHall_PIF` names `pif-data-v5.zip` from the `healthgps-data` releases. It was **fetched and
verified** during orientation: 5,366,395 bytes, SHA-256
`a918e8525cd8c48cab6b13cceb08f51e8593cac80753a6782e4ea2a28d9e02ca`, which is exactly the checksum
the upstream config declares. So the PIF work in this run runs against the real data pack and not
against a synthetic stand-in. What the pack contains is in [docs/examples.md](examples.md).

---

## A second compiler, and the three things it found

Every measurement this project has taken, over three runs, came from one compiler: Apple clang 21 on
one machine. That is a narrow base for a tree compiled with `-Werror` and eleven extra warning
options, and this run added CI on two operating systems, so it needed knowing before a runner said
so.

Homebrew's LLVM is installed on the development host, so the tree was configured a second time
against **Homebrew clang 23.1.1** — same platform, same standard library family, two major versions
newer — and built with `ninja -k 0` to collect every complaint rather than stopping at the first:

```bash
cmake -S . -B /tmp/brewclang -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_TESTING=ON
ninja -C /tmp/brewclang -k 0
```

It found three things, and two of them are real defects that would have turned the new CI red on its
first run.

| | What | Verdict |
|---|---|---|
| 1 | `src/io/paths.h` declares a function taking `std::vector<std::string> &` and does not include `<vector>` | **a real bug.** Apple's libc++ supplies it transitively through `<filesystem>`; a newer one does not. Fixed by including `<vector>`. |
| 2 | `LoadContext{…}` in `build_modules.cpp` omitted its last field | **a real warning, worth acting on.** Named explicitly, with a comment saying why `extra_factors` is empty at that point — the static model has not been loaded yet. |
| 3 | `-Wmissing-designated-field-initializers` on 213 designated initialisers that leave an optional field to its default | **a style warning this tree disagrees with.** Turned off, once, with the reason in `cmake/warnings.cmake`. |

The third is the only `-Wno-*` in the project, and it is a deliberate amendment to the rule in
[ADR 0004](decisions/0004-toolchain-cpp20-cmake-vcpkg-googletest.md) that every warning is fixed
rather than suppressed. clang 19 added it to `-Wextra`, and it fires on
`IssueLocation{.file = path}` — the construct this tree uses 129 times to say "here is the part of
the location that is known", where `field`, `line` and `column` are all optional with defaults.
Satisfying it means writing `IssueLocation{.file = path, .field = {}, .line = {}, .column = {}}` at
every site, which is harder to read and hides what the initialiser was saying. The flag is added only
where the compiler recognises it, so an older clang or a gcc never sees an unknown option.

After the two fixes: **clean build, and the full suite passes** — 629 tests under Homebrew clang
against 630 under Apple clang, the difference being CTest's Python tests, which are not in the
binary.

**What this still does not cover.** GCC, and Linux. The warning set has never been through GCC, and
`-Wconversion`, `-Wsign-conversion` and `-Wold-style-cast` under `-Werror` are where a second
compiler family usually has opinions. `.github/workflows/ci.yml` therefore has an **informational**
GCC job, marked `continue-on-error`, so the first GCC build's complaints are visible without a red
tick in a commit that cannot fix them; [docs/backlog.md](backlog.md) carries promoting it to required.

---

## Fourth run, orientation: what was found running, and what was recovered

Re-checked on **2026-09-18**, on the same host, before any change.

### What was still running from the previous session, and what happened to it

Three things were live on the machine and none of them was wanted:

| | What | Disposition |
|---|---|---|
| 1 | `scripts/check.sh --no-equivalence`, mid-way through the debug preset, with a `ctest` and two `self_check.py` children | **Killed.** Unattended, its output going nowhere, and this run needed to choose when the presets ran. |
| 2 | A full-scale `HLM_India` run of this build, 20 minutes in, chained to a baseline run after it | **Killed**, and this is the one worth recording. It was started by the previous session's Bash tool, so its `/usr/bin/time -l` output was going to a pipe whose reader had died with that session. The run would have completed and the *timings and peak memory would have been lost*, which was the only reason it was running. It was restarted later in this run with its output redirected to a file. |
| 3 | Assorted scratch under `/tmp/hgps-audit-build`, `/tmp/hgps-build`, `/tmp/hgps-self-check` | **Left alone.** `/tmp/hgps-build` is the baseline build this project depends on; the other two are previous runs' working directories and are not read by anything. |

The lesson from (2) is worth keeping: **a long measurement started in the background must write to a
file, not to a pipe.** A backgrounded run outlives the session that started it, and its standard
output does not.

### What was uncommitted, and what was done with it

`git status` showed 18 modified files and 5 new ones: the **population impact fraction**
implementation, complete with its tests, its converted example variants and its documentation. It was
not a work in progress — it was finished work that had not been committed.

It was checked rather than assumed: `scripts/check.sh --no-equivalence` at **666 tests, passing under
all four presets**, and then committed as it stood (`feat(model): population impact fraction, and the
two examples that cannot use it`). **Nothing was discarded.** The only changes made to it afterwards
were documentary — restructuring `docs/examples.md` around a "Does not run — upstream data defect"
heading, and correcting one code comment that cited deviation B-28 where it meant D-04.

`.github/workflows/ci.yml` was also already present and committed, from the previous session.

### How the CI workflow was validated

**By reading, not by running.** Neither [`act`](https://github.com/nektos/act) nor Docker is installed
on this host, so the workflow has never been executed. Every step was instead checked against the
local scripts it stands in for:

| Step | Checked against |
|---|---|
| `cmake --preset <p>` / `--build --preset <p>` / `ctest --preset <p>` | `CMakePresets.json` — all four presets exist in all three of `configurePresets`, `buildPresets` and `testPresets` |
| the checkout layout | `tests/CMakeLists.txt`'s `HGPS_UPSTREAM_EXAMPLES_DIR`, which resolves the examples as a **sibling** of the source tree; the two `path:` values reproduce that |
| `VCPKG_ROOT` reaching the configure step | set with `>> "$GITHUB_ENV"` inside the composite action, which is visible to later *job* steps; `CMakePresets.json` reads `$env{VCPKG_ROOT}` |
| the vcpkg commit | `vcpkg.json`'s `builtin-baseline`, `bd2b548…`, pinned identically in the workflow's `env` |
| the sparse checkout list | the six directories `tests/config/convert_config_test.cpp` converts, plus the FINCH pack that the loader tests read directly |
| `~/.cache/healthgps` as the data cache | `src/io/paths.cpp:58-62` — `$XDG_CACHE_HOME/healthgps` or `~/.cache/healthgps` on Linux. The equivalence job is Linux-only, so this is the right path for it |
| `self_check.py` with no `--new` | its default is `REPO / "out/build/release/src/healthgps"`, which is what the job has just built; the fixture pack it needs is derived from that path |
| the harness's Python tests being covered by `ctest` | `EquivalenceHarness.Rules`, `.SelfConsistencyAcrossSeeds` and `.DetectsADeliberatePerturbation` are CTest entries, so `ctest --preset` runs them |
| `-Wno-missing-designated-field-initializers` under GCC | `cmake/warnings.cmake` adds it only behind `check_cxx_compiler_flag`, so an older clang or a GCC never sees an unknown option |

That is not the same as a green run, and it is not claimed to be. **The first time this workflow
executes will be the first evidence that it works**, and the honest expectation is that something in
it is wrong — most likely in the GCC entries, which is why they are `continue-on-error`
([docs/backlog.md](backlog.md) item 2).

What changed from the shape the previous session left: GCC moved **into the build matrix** as a
compiler axis on Linux rather than sitting in a separate job, and the three verbatim copies of the
checkout-plus-Ninja-plus-vcpkg bootstrap became one composite action at
`.github/actions/prepare/action.yml`. Three copies of a bootstrap is how a pinned commit gets updated
in two places out of three.

## Fifth run: CI actually ran, and what it found

The previous section ends *"the first time this workflow executes will be the first evidence that it
works, and the honest expectation is that something in it is wrong — most likely in the GCC
entries."* The workflow then ran, for the first time, on the commit that pushed this repository to
GitHub. **Every job failed.** The expectation was right about there being something wrong and wrong
about where: the GCC entries were not the problem, and one of the two guesses in that table —
"`-Wno-missing-designated-field-initializers` under GCC" — was the exact thing that broke.

Five causes, each fixed in its own commit, in the order CI surfaced them.

| # | What failed | Where | Why it was invisible locally |
|---|---|---|---|
| 1 | `std::mt19937::result_type` narrowed implicitly | every Linux job, first file compiled | `result_type` is `std::uint_fast32_t`: **32 bits on libc++, 64 on libstdc++**. `return engine_()` from a function returning `std::uint32_t` is an exact, value-preserving narrowing — and an implicit one, which `-Wconversion` rejects on Linux and has no reason to mention on macOS |
| 2 | a constructor parameter shadowing a member | both GCC jobs | **GCC's `-Wshadow` covers constructor parameters and clang's does not**; clang puts that check behind `-Wshadow-field-in-constructor`, which was not on |
| 3 | nineteen missing standard headers | every Linux job | libc++ supplies `<cstdint>` through `<source_location>`, `<stdexcept>` through others, and libstdc++ does not. The first one CI hit was `std::uint_least32_t` in `src/diagnostics/internal_error.h` |
| 4 | `-Wmissing-field-initializers` on 213 designated initialisers | every Linux job, twice | clang 19 has a **narrow** warning for this and the tree turns it off. An older clang and every GCC fold the construct into `-Wextra`'s broad one, and there is no narrow flag to turn off |
| 5 | a stored equivalence reference could not be found | both equivalence jobs | the reference's key is the hash of the derived config — taken **after** absolutising its input paths, so it carried `/Users/jude/work/hpgs/…` and could only match on the machine that wrote it |

Cause 4 took two commits, and the second one is the interesting part. The first added a fallback:
probe for the narrow flag, and turn off the broad one where the narrow one is missing. CI failed
again, in the same place, on GCC. The reason is that **GCC accepts any `-Wno-<anything>` it has never
heard of**, and complains only if some other diagnostic is emitted — so probing
`-Wno-missing-designated-field-initializers` is answered "yes" by a compiler that has never heard of
it, the narrow flag was added, it did nothing, and the fallback was never reached. Probing the
**positive** spelling, `-Wmissing-designated-field-initializers`, is answered honestly by everybody.
That is a property of `check_cxx_compiler_flag` worth remembering: a `-Wno-` probe on GCC tests
nothing.

### What was done about the class rather than the instance

Causes 1–3 are all one shape: **a portability defect the development compiler cannot see.** So each
was fixed by finding every instance rather than the one CI stopped on.

- **Cause 2** — clang's `-Wshadow-all` was run over every translation unit in `src/`, `tests/` and
  `tools/`. It found exactly the two `person.h` constructors GCC had named and nothing else, and
  `-Wshadow-field-in-constructor` is now on wherever the compiler understands it, so the development
  compiler says what the Linux one would.
- **Cause 3** — a scan for `std::` names used without the header that declares them, following each
  file's project-local includes **transitively**, so a header a `.cpp` genuinely inherits is not
  reported. 245 candidates before following includes, **19** after, every one of them real. All 19
  were fixed in one commit and the scan now reports none.
- **Cause 1** — contained to one header by inspection (`grep` for `mt19937`, `uint_fast`), and pinned
  by a `static_assert` that the engine's range is exactly 32 bits, so the cast stays value-preserving
  whatever a future standard library makes `result_type`.

Cause 1 has no local detector and that is worth being honest about: nothing on this machine can see
it, because the types are genuinely the same width here. The `static_assert` protects the *reasoning*
rather than the platform.

### What CI is now worth

It found five real defects in a tree that had been green locally for four runs, four presets and 668
tests, and three of the five were **portability defects invisible to the only compiler that had ever
built it.** The fourth run's summary listed "CI has never run" and "GCC has never built this tree"
among the things a reader should be sceptical about. Both are now false, and the second one is the
more useful: **GCC on Linux is green**, so the `experimental: true` flag on those matrix entries has
done its job and is a candidate for removal ([docs/backlog.md](backlog.md)).

### What is still not tested by CI

- **The baseline is still not built there**, by design (see the workflow's own header comment), so
  the equivalence jobs compare against the checked-in references and `--refresh-reference` remains a
  deliberate local act.
- **`HLM_India` is still not compared there**, for the same reason as before: its reference is at a
  reduced `size_fraction` and a job would be forty minutes.
