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
