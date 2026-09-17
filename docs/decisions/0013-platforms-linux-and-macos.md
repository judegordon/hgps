# 0013 — Linux and macOS supported from the start; Windows not targeted

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

## Context

Neither existing codebase builds on macOS. The baseline needs four workarounds (audit §4.2,
finding B-10): a `#error "Unsupported platform"` in `program_dirs.cpp`, which handles only
`__linux__` and `_WIN32`; `std::execution::par`, which Apple libc++ gates behind
`-fexperimental-library`; `std::osyncstream`, which Apple libc++ forward-declares but does not
define; and a missing `__libcpp_atomic_wait` export. The baseline's CI targets Linux and Windows.

The audit's estimate: macOS support is cheap to design in (one `__APPLE__` branch, avoiding PSTL and
`<syncstream>`) and tedious to retrofit. This run's development host is macOS.

## Decision

Ruled by the project owner:

- **Linux and macOS are supported from the start.** This run's host is macOS, so macOS is
  continuously exercised rather than claimed.
- **No PSTL** (`std::execution::*`) and **no `<syncstream>`**. Population sweeps use this project's
  own `core/parallel.h`, which needs neither; diagnostic output is serialised by its single owner
  rather than by a synchronised stream.
- **One `__APPLE__` branch**, for executable-path lookup: `_NSGetExecutablePath` on macOS,
  `/proc/self/exe` on Linux. It lives in exactly one function.
- **Windows is not targeted.** No `_WIN32` branches are written speculatively; the code stays free
  of POSIX-only calls outside that one function so a future port is small.

## Alternatives

- **Linux only**, as the earlier rewrite effectively is. Would make this run's own development host
  a second-class platform, and the baseline shows that retrofitting is where the `#error` came from.
- **All three platforms now.** Windows cannot be tested here, and an untested platform branch is a
  claim rather than a feature.

## Consequences

`std::thread`-based parallelism instead of PSTL is slightly more code, and it is the same code that
makes the fixed-order reduction possible (ADR 0026), so the cost is already paid. Because Windows is
not targeted, path handling still goes through `std::filesystem` and no `\`-separator assumptions are
made, which keeps the eventual port mechanical.
