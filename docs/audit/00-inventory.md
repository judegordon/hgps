# 00 — Inventory

Audit run date: **2026-09-17**. Host: macOS 26.0 (Darwin 25.6.0), Apple Silicon (arm64),
Apple Clang 21.0.0 / libc++.

All four source folders were treated as read-only throughout. Nothing in them was edited,
reformatted, committed to, or checked out. All build trees live under `/tmp/hgps-audit-build/`.

---

## 1. Repository provenance — important limitation

**None of the four source folders is a git repository.** Each is a plain directory tree:

```
$ git -C hgps_main rev-parse --is-inside-work-tree
fatal: not a git repository
```

The same is true of `hgps_main_data`, `hgps_main_examples` and `hpgs_og_rewrite`. There is no
`.git` directory, no packed refs, and no vendored VCS metadata of any kind.

**Consequences for this audit, stated up front:**

| Task | Intended source | Actual status |
|---|---|---|
| Record commit hash and date of each repo | `git log -1` | **Not possible.** No commit hashes or author dates exist locally. |
| Trace *when and why* the rewrite's changes happened (task 7) | old rewrite git history | **Not possible.** Rationale is reconstructed from code comments and structure only, and is marked `inferred` unless a comment or file in the repo states it. |
| Compare rewrite against upstream commits since divergence (task 9) | baseline git history | **Not possible.** Divergence is inferred by differential code reading against the baseline snapshot as it stands. |

Where a task depended on history, this is called out again in the document that needed it
(`06-rewrite-changes.md`, `08-rewrite-issues.md`). Everything that did not depend on history was
completed in full.

The closest available substitutes for a commit identity are recorded below: declared project
versions, declared dependency baselines, and file modification times.

---

## 2. Folder inventory

### 2.1 `hgps_main` — the baseline

| Field | Value |
|---|---|
| Path | `/Users/jude/work/hpgs/hgps_main` |
| Role | Upstream Health-GPS C++ microsimulation. The reference for this audit. |
| Remote URL | Not recoverable from the folder (no git). Declared homepage: `https://github.com/imperialCHEPI/healthgps` (`CMakeLists.txt:27`) |
| Commit hash / date | **Unavailable** (not a git repo) |
| Declared version | `3.0.0.0` (CMake `project(... VERSION 3.0.0.0)`, `CMakeLists.txt:25`) |
| vcpkg manifest version | `1.2.2.0` (`vcpkg.json`) — **inconsistent with the CMake version**; see `04-baseline-issues.md` (B-11) |
| Size on disk | 7.5 MB, 444 files |
| File mtimes | 2026-08-11 (source) |
| Languages | C++20 (128 `.cpp`, 138 `.h`), Python (12, docs tooling), R (docs), JSON schemas (35) |
| Build system | CMake ≥ 3.20 + Ninja, with `CMakePresets.json` (presets for linux/windows debug+release) |
| Package manager | vcpkg manifest mode, `builtin-baseline: bd2b54836beed96e1efbe9aaf8ee800f5448856d` |
| Test framework | GoogleTest (`gtest` + `gmock`), custom `main()` in `src/HealthGPS.Tests/TestMain.cpp`, registered via CTest |
| Licence | **BSD 3-Clause**, `LICENSE.txt` — see §3 |

Source layout and size:

| Module | Lines (`.cpp`+`.h`) | Responsibility |
|---|---:|---|
| `src/HealthGPS` | 19,446 | Simulation engine, models, scenarios, events |
| `src/HealthGPS.Tests` | 10,571 | GoogleTest suite |
| `src/HealthGPS.Input` | 6,171 | Config parsing, schema validation, data loading |
| `src/HealthGPS.Core` | 2,650 | Shared primitives: datatable, identifier, intervals, math |
| `src/HealthGPS.Console` | 1,428 | CLI host, event monitor, result writers |
| `src/external/adevs` | 1,148 | Vendored cut-down adevs DEVS simulator (4 headers) |
| **Total** | **41,414** | |

Declared dependencies (`vcpkg.json`): `fmt` (≥10.2.1), `cxxopts`, `eigen3`, `nlohmann-json`,
`jsoncons`, `rapidcsv`, `crossguid`, `gtest`, `tbb`, `libzippp`, `openssl`, `platform-folders`,
`curlpp`.

There is also a stray top-level folder `disease_check_scripts- Mahima/` (note the space and
personal name) containing ad-hoc analysis scripts. See `04-baseline-issues.md` (B-12).

### 2.2 `hgps_main_data` — the data repository

| Field | Value |
|---|---|
| Path | `/Users/jude/work/hpgs/hgps_main_data` |
| Remote URL | Not recoverable. Referenced upstream as `imperialCHEPI/healthgps-data` |
| Commit hash / date | **Unavailable** |
| Size on disk | 60 MB, 1,908 files |
| File mtimes | 2026-07-20 |
| Content | 1,898 CSV, 2 JSON (`data/index.json`, `data/diseases/Metadata.json`), 1 XLSX |
| Languages | Data + 4 R scripts (data preparation / checking), `requirements.txt` |
| Build system | None (data only) |
| Test framework | None |
| Licence | **BSD 3-Clause**, `LICENSE`, "Copyright (c) 2024, Centre for Health Economics & Policy Innovation" |

Detailed content breakdown is in `02-data-and-examples.md`.

### 2.3 `hgps_main_examples` — the examples repository

| Field | Value |
|---|---|
| Path | `/Users/jude/work/hpgs/hgps_main_examples` |
| Remote URL | Not recoverable. Referenced upstream as `imperialCHEPI/healthgps-examples` |
| Commit hash / date | **Unavailable** |
| Size on disk | 286 MB, 245 files |
| File mtimes | 2026-07-20 |
| Content | 6 example model configurations + 1 committed results directory (`HLM_France_results-50sims`) + R post-processing scripts |
| Build system | None. Shell helpers: `zip_dir.sh`, `make_release_artifacts.sh`, `example-jobscript.sh` (PBS) |
| Test framework | None |
| Licence | **BSD 3-Clause**, `LICENSE` |

Detailed content breakdown is in `02-data-and-examples.md`.

### 2.4 `hpgs_og_rewrite` — the earlier rewrite

| Field | Value |
|---|---|
| Path | `/Users/jude/work/hpgs/hpgs_og_rewrite` |
| Remote URL | None. Not a git repo; no remote recorded anywhere in the tree. |
| Commit hash / date | **Unavailable** |
| Declared version | `1.0.0` (CMake `project(HealthGPS VERSION 1.0.0)`) and `vcpkg.json` `"version": "1.0.0"` (self-consistent) |
| Size on disk | 42 MB, 2,016 files |
| File mtimes | 2026-09-17 — i.e. all files carry the date this working copy was created, not authoring dates |
| Languages | C++20 only |
| Build system | CMake ≥ 3.20 + Ninja, `CMakePresets.json`, vcpkg manifest mode |
| vcpkg baseline | `bd2b54836beed96e1efbe9aaf8ee800f5448856d` — **identical to the baseline's** |
| Test framework | **None.** There is no test target, no test directory, and no `gtest` dependency. |
| Licence | **No licence file of any kind.** See §3. |

Source layout and size:

| Module | Purpose |
|---|---|
| `src/hgps_core` | Shared primitives, split into `data/`, `diagnostics/`, `interfaces/`, `types/`, `utils/` |
| `src/hgps_input` | Split into `config/`, `data/`, `io/`, `models/` |
| `src/hgps` | Split into `analysis/`, `data/`, `disease/`, `events/`, `models/`, `scenarios/`, `simulation/`, `types/`, `utils/` |
| `src/hgps_console` | CLI host |
| `src/external/adevs` | Vendored adevs (same 4 headers as baseline) |
| **Total** | **23,569 lines** across `.cpp`/`.h` |

Non-source content: `data/undb/` (a partial copy of the upstream data repository),
`models/kevinhall_finch/` (a model input pack), `schemas/` (JSON schemas, restructured relative to
the baseline).

Dependencies (`vcpkg.json`): identical to the baseline **except `gtest` is absent** — consistent
with there being no test suite.

### 2.5 `hgps_new_rewrite` — audit output

Initialised as a git repository on branch `main` (`git init -b main`). Contains only
`docs/audit/`. No implementation code was written during this run.

---

## 3. Licences

### Baseline (`hgps_main/LICENSE.txt`) — recorded in full as required

```
BSD 3-Clause License

Copyright (c) 2021, Centre for Health Economics & Policy Innovation, Imperial College London;
INRAE, France.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

**What BSD-3-Clause permits for the new rewrite.** Derivative and redistributed works are allowed,
including closed-source ones, provided the copyright notice, the conditions list and the disclaimer
are retained in source and binary distributions (clauses 1 and 2), and provided the names of
Imperial College London / INRAE and contributors are not used to endorse or promote the derived
work without prior written permission (clause 3). This is why short excerpts from the baseline are
quoted in these documents where a defect cannot be shown otherwise: the licence permits it, and each
excerpt is attributed by file and line.

### Other licences

- `hgps_main_data/LICENSE` — BSD 3-Clause, "Copyright (c) 2024, Centre for Health Economics &
  Policy Innovation". Note that the *data content itself* carries separate third-party terms
  declared inside `data/index.json`: UN World Population Prospects data under a Creative Commons
  licence, and IHME disease data under **CC BY-NC-ND 4.0** (non-commercial, no derivatives).
  The IHME terms are more restrictive than the repository's BSD licence and constrain redistribution
  of the disease CSVs. Flagged as an open question in `09-ideas-and-questions.md`.
- `hgps_main_examples/LICENSE` — BSD 3-Clause.
- `hpgs_og_rewrite` — **no licence file present.** Given that the code is a derivative of the
  BSD-3-Clause baseline, clauses 1 and 2 require the baseline notice to be retained in
  redistributions; it currently is not, anywhere in the tree. This is recorded as an issue in
  `08-rewrite-issues.md` (R-01) and as a question in `09-ideas-and-questions.md`.

---

## 4. Build and test results

### 4.1 Toolchain provisioned for this audit

The host had no C++ build toolchain beyond a compiler. The following were installed:

| Tool | Version |
|---|---|
| Apple Clang | 21.0.0 (clang-2100.3.34.2), libc++ |
| CMake | 4.4.3 (Homebrew) |
| Ninja | 1.13.2 |
| cppcheck | 2.21.0 |
| clang-tidy | LLVM (Homebrew) |
| vcpkg | cloned and pinned to `bd2b5483…`, the baseline's declared builtin-baseline |

### 4.2 Baseline build — **succeeded, with four documented deviations**

Configuration actually used:

```
cmake -S hgps_main -B /tmp/hgps-audit-build/baseline-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_OVERLAY_TRIPLETS=/tmp/hgps-audit-build/triplets \
  -DBUILD_TESTING=ON
```

Four obstacles were hit. Each is recorded here because each is itself evidence about the baseline's
portability, and because the workarounds mean the audit binary is **not** a stock baseline binary.

| # | Obstacle | Cause | Workaround used | Is it a baseline defect? |
|---|---|---|---|---|
| 1 | `curlpp` and other ports failed to configure | CMake 4.4 removed compatibility with `cmake_minimum_required(VERSION <3.5)`, which old vcpkg ports still declare | `CMAKE_POLICY_VERSION_MINIMUM=3.5` | No — environmental (vcpkg baseline age vs CMake 4) |
| 2 | `openssl` failed to compile: `fatal error: 'inttypes.h' file not found`, from a malformed `-isysroot -g` flag | `VCPKG_OSX_SYSROOT` was empty for the default `arm64-osx` triplet | Custom overlay triplet setting `VCPKG_OSX_SYSROOT` to `xcrun --show-sdk-path` | No — environmental |
| 3 | `error: no member named 'osyncstream' in namespace 'std'` (6 sites) | Apple libc++ forward-declares `std::basic_osyncstream` in `<iosfwd>` but ships **no definition** and no `<syncstream>` header | Audit-only shim header supplying a minimal `std::basic_osyncstream` | **Yes** — baseline portability defect, see B-08 |
| 4 | `#error "Unsupported platform"`; `no member named 'par' in namespace 'std::execution'`; unresolved `std::__1::__libcpp_atomic_wait` | `program_dirs.cpp` handles only `__linux__`/`_WIN32`; Apple libc++ needs `-fexperimental-library` for PSTL; Apple's shipped `libc++.dylib` does not export `__libcpp_atomic_wait` | `-D__linux__=1` plus a forced-include redirecting `readlink("/proc/self/exe")` to `_NSGetExecutablePath`; `-fexperimental-library`; one stub object supplying `__libcpp_atomic_wait` as a spurious-wakeup sleep (valid: every libc++ caller re-checks its predicate) | **Yes** for the platform `#error`, see B-08. The libc++ items are environmental. |

Shim files live in `/tmp/hgps-audit-build/shim/` and are reproduced in `04-baseline-issues.md`.
No file under `hgps_main/` was modified.

**Result: `BUILD_EXIT=0`.** Two binaries produced:
`src/HealthGPS.Console/HealthGPS.Console` and `src/HealthGPS.Tests/HealthGPS.Tests`.

Compiler warnings at the project's own level (`-Wall -Wextra -Wpedantic`): **4**, all of one kind:

```
analysis_module.cpp:821:17:  warning: variable 'processed' set but not used
analysis_module.cpp:1221:17: warning: variable 'processed_income' set but not used
analysis_module.cpp:1847:17: warning: variable 'processed_std' set but not used
```
(the fourth is a duplicate emitted for a second TU). See B-10.

### 4.3 Baseline test suite — **all passing**

```
$ ./HealthGPS.Tests
[==========] 471 tests from 48 test suites ran. (236 ms total)
[  PASSED  ] 436 tests.
[  SKIPPED ] 35 tests
```

**471 registered, 436 passed, 35 skipped, 0 failed.**

All 35 skips have a single cause. They call `GTEST_SKIP()` when a FINCH input pack is absent:

```cpp
// KevinHallHeight.Test.cpp:21-26
std::filesystem::path healthgps_repo_root() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}
std::filesystem::path finch_data_root() {
    return healthgps_repo_root() / "input-data/data/KevinHall_FINCH";
}
```

The path is derived from `__FILE__` and points *inside the source tree* at
`hgps_main/input-data/data/KevinHall_FINCH`, a directory that exists in neither
`hgps_main_data` nor `hgps_main_examples` (the examples repo has `KevinHall_FINCH` at its top
level, not under `data/`). Because the source folders are read-only for this audit, the directory
was not created and these tests were left skipped. This is itself a finding — see B-09.

Skipped tests by suite: `KevinHallHeight` 22, `KevinHallWeightQuantiles` 7,
`ModelParserFinch` 3, `TestSimulation` 2, `KevinHallWeightValidation` 1.

### 4.4 Old rewrite build — **not attempted as a test run; no test suite exists**

The old rewrite has **no test target**. `grep` for `test`, `gtest` or `CTest` across its
`CMakeLists.txt`, `src/CMakeLists.txt` and all four module `CMakeLists.txt` files returns nothing,
and `gtest` is absent from its `vcpkg.json`. There is therefore **no test suite to run**: pass/fail
counts are not applicable, not merely unavailable. This is recorded as R-02 in
`08-rewrite-issues.md` and is the single most consequential difference from the baseline.

Build status is recorded in `08-rewrite-issues.md` alongside the compile diagnostics it produced.

### 4.5 Sanitizers and static analysis

| Tool | Target | Result |
|---|---|---|
| ASan + UBSan (`-fsanitize=address,undefined`, `-fno-sanitize-recover=undefined`) | test suite (471 tests) | **Clean.** 436 passed, 35 skipped, 0 sanitizer reports. |
| ASan + UBSan | full France simulation, 2010–2015 | **Clean.** 0 UBSan runtime errors, 0 ASan errors. |
| ThreadSanitizer | France simulation, baseline + intervention | See `04-baseline-issues.md` (B-02). |
| cppcheck 2.21 (`warning,performance,portability`) | `src/`, excluding `external/` and tests | See `04-baseline-issues.md` §Tool output. |
| clang-tidy | `src/`, via `compile_commands.json` | See `04-baseline-issues.md` §Tool output. |

Per the audit rules, all tool output was treated as leads only; nothing appears as a finding in
`04-baseline-issues.md` or `08-rewrite-issues.md` without confirmation by reading the code.

### 4.6 Reference run — reproducibility check

A reference configuration derived from `hgps_main_examples/HLM_France/config.json` was run
repeatedly against the local data store with a fixed seed (`123456789`). Full method and results are
in `03-baseline-determinism.md`. Headline:

- **Baseline-only runs: bit-identical** across 3 repeats, and across thread counts 1/2/4/10,
  at both 6k and 125k population and up to a 25-year horizon.
- **Baseline + intervention runs: output files differ between repeats**, but sorting the rows
  makes them byte-identical. The *numbers* are reproducible; the *row order* in the result file is
  not. See B-01.
