# 0004 — C++20, CMake presets, pinned vcpkg, GoogleTest, warnings as errors

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

## Context

The baseline is C++20, CMake + Ninja with presets, vcpkg in manifest mode pinned to
`builtin-baseline: bd2b54836beed96e1efbe9aaf8ee800f5448856d`, and GoogleTest with a custom `main()`.
Its 471 tests are the only independent evidence of correctness that exists, so the test framework
choice here is constrained by the wish to keep the ports close to their originals.

The baseline builds at `-Wall -Wextra -Wpedantic` and emits seven warnings it has not fixed (audit
B-19 and `docs/build-notes.md`).

## Decision

Ruled by the project owner:

- **C++20**, no compiler extensions.
- **CMake with presets**: `release`, `debug`, `asan-ubsan`, `tsan`.
- **vcpkg** in manifest mode, pinned to a `builtin-baseline`.
- **GoogleTest**, matching the baseline so ports stay close.
- **`-Wall -Wextra -Wpedantic -Werror`** on this project's own targets. Every warning is fixed, not
  suppressed: there are no blanket `-Wno-*` flags and no `#pragma` suppressions.
- Comments record invariants and the reasons for non-obvious choices. No Doxygen boilerplate.

`-ffp-contract=off` is added for all targets, so a compiler may not fuse `a*b+c` into an FMA behind
our back (audit N-14).

## Alternatives

- **Catch2 or doctest.** Nicer in places, but every one of 471 ported tests would need its assertion
  macros rewritten, adding risk to the one thing that must be trustworthy.
- **FetchContent instead of vcpkg.** Fewer moving parts, but no pinned transitive set, and the
  baseline's own pin is a useful precedent.
- **Warnings as warnings.** The baseline shows where that ends: seven live warnings, two of them
  pointing at genuinely dead state.

## Consequences

A stale vcpkg baseline eventually stops working with a current CMake — exactly what the audit hit
(`CMAKE_POLICY_VERSION_MINIMUM=3.5`). Keeping the dependency set tiny (ADR 0014) is what makes that
a small problem here. `-Werror` will occasionally break the build on a new compiler version; that is
the intended trade.
