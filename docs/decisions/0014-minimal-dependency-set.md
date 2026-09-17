# 0014 — Three dependencies; implement SHA-256, CSV, matrix and Cholesky; delegate fetch and unzip

## Status

Accepted, 2026-09-17.

## Context

The baseline declares thirteen vcpkg dependencies: `fmt`, `cxxopts`, `eigen3`, `nlohmann-json`,
`jsoncons`, `rapidcsv`, `crossguid`, `gtest`, `tbb`, `libzippp`, `openssl`, `platform-folders`,
`curlpp`.

Two of them were the audit's single biggest build obstacle (audit §4.2): `curlpp` and other ports
declare `cmake_minimum_required(VERSION <3.5)` and fail under CMake 4 without a policy override, and
`openssl` failed to compile because the stock `arm64-osx` triplet left `VCPKG_OSX_SYSROOT` empty,
producing a malformed `-isysroot -g`. Both needed workarounds before a single line of Health-GPS
compiled.

What those dependencies are actually used for here is small: SHA-256 of a data archive; CSV reading;
one dense matrix with one Cholesky decomposition (`Eigen::LLT` at `model_parser.cpp:1546`); HTTP
download; zip extraction; string formatting; JSON; tests.

## Decision

Three vcpkg dependencies: **`fmt`**, **`nlohmann-json`**, **`gtest`**.

Implemented in this repository instead:

- **SHA-256** (`io/sha256.*`) — the FIPS 180-4 algorithm, ~150 lines, with the standard test
  vectors. Replaces `openssl`.
- **CSV reading** (`io/csv_reader.*`) — replaces `rapidcsv`, and is what makes a bad cell a located
  `InputIssue` with a line and a column instead of an exception from a third-party header.
- **`core::Matrix`** with `cholesky_lower()` and fixed-order multiply (`core/matrix.*`) — replaces
  `eigen3` for the one decomposition the model needs (ADR 0023).
- **Command-line parsing** (`app/options.*`) — a few dozen lines, replaces `cxxopts`.
- **Parallel helpers** (`core/parallel.h`) — replaces `tbb`, and gives the fixed-order reduction the
  determinism contract requires (ADR 0026).

Delegated to external executables, invoked with an explicit argv and no shell:

- **HTTP download** → `curl`. **Zip extraction** → `unzip`.

The **SHA-256 check is ours**, computed on the downloaded bytes before extraction, so trust does not
rest on the external tool. Both tools ship with macOS and are present on every mainstream Linux CI
image; a missing tool is a located input issue naming the tool and the source it was needed for.

## Alternatives

- **Link `libcurl` and `libzip`.** More self-contained, and it re-adds the transitive TLS dependency
  that cost the audit the most time. Worth revisiting if fetching ever needs to be in-process.
- **Keep `eigen3` for the matrix.** A large header dependency for one `LLT` call.
- **Keep `rapidcsv`.** Header-only and fine, but its errors are exceptions from someone else's
  header, which cannot carry a `IssueCode` or a column number.
- **Drop `fmt` for `std::format`.** libc++ has it; GCC 11, still common on Linux, does not.

## Consequences

More of this codebase is ours to maintain and to test — hence the SHA-256 vectors, the CSV edge-case
tests and the Cholesky tests. In exchange, `vcpkg install` is three small ports, the build does not
depend on a TLS stack, and none of the audit's four build workarounds is needed. Zip and URL sources
gain a runtime dependency on two ubiquitous binaries; directory sources, which is what CI and the
fixtures use, depend on nothing.
