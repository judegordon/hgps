# Health-GPS audit — summary

Audit date **2026-09-17**. Host: macOS 26.0, Apple Silicon, Apple Clang 21 / libc++.
Subject: the upstream Health-GPS baseline, its data and examples repositories, and an earlier
deterministic C++ rewrite. No implementation code was written; the four source folders were not
modified.

---

## Repository states

| Folder | What it is | Version | Size | Git |
|---|---|---|---:|---|
| `hgps_main` | Upstream Health-GPS baseline | CMake `3.0.0.0` / vcpkg `1.2.2.0` | 7.5 MB, 444 files, 41,414 LOC | **none** |
| `hgps_main_data` | Upstream data | — | 60 MB, 1,908 files | **none** |
| `hgps_main_examples` | Upstream examples | — | 286 MB, 245 files | **none** |
| `hpgs_og_rewrite` | Earlier rewrite | `1.0.0` | 42 MB, 2,016 files, 23,569 LOC | **none** |
| `hgps_new_rewrite` | This audit's output | — | docs only | initialised, `main` |

**No source folder is a git repository.** No commit hashes or dates exist to record, and the tasks
that depended on history — tracing *when and why* the rewrite changed things, and diffing the
rewrite against upstream commits since divergence — could not be done as specified. They were done
by differential code reading instead, and every rationale is marked `inferred` unless the repository
itself evidences it. This limitation is stated wherever it bites.

Licences: baseline, data and examples are all **BSD-3-Clause** (Imperial College London / INRAE,
2021; CHEPI 2024). The disease data carries a separate, more restrictive **CC BY-NC-ND 4.0**. The
old rewrite has **no licence file at all**.

## Build and test status

| | Build | Tests |
|---|---|---|
| **Baseline** | **Pass**, with 4 documented deviations | **471 registered, 436 passed, 35 skipped, 0 failed** |
| **Old rewrite** | **Pass**, 5 warnings | **No test suite exists** — not a failure, an absence |

The baseline does not build on macOS as shipped: `#error "Unsupported platform"`, plus
`std::execution::par` and `std::osyncstream`, neither in Apple libc++. Four audit-only shims were
supplied (a `<syncstream>` header, a `readlink`→`_NSGetExecutablePath` redirect,
`-fexperimental-library`, and one stub symbol). All 35 test skips have one cause: an undocumented
`__FILE__`-relative fixture path matching neither data repository's layout.

ASan + UBSan: **clean** on the full test suite and on a complete simulation. cppcheck: 15
diagnostics. clang-tidy: 987 warnings, mostly `const`-correctness and narrowing. ThreadSanitizer:
1,762 reports, almost all false positives inside uninstrumented oneTBB — but 27 implicate
`repository.cpp` and are real.

## Top baseline issues

19 findings in `04-baseline-issues.md`; the six high-severity ones:

1. **Result row order is nondeterministic** when an intervention is active. Demonstrated: three
   same-seed runs produced three different files, but sorting them made all three byte-identical.
   The numbers are reproducible; the file is not.
2. **Data race on the shared disease-definition cache.** A fast path commented
   `// lock-free multiple readers` reads a `std::map` outside the mutex while another thread inserts
   into it. Confirmed by ThreadSanitizer: locked write at `repository.cpp:134`, unlocked read at
   `:57`, same address.
3. **Undefined behaviour in character handling** — 11 `<cctype>` calls take a plain signed `char`.
4. **`Identifier::operator==` compares only a hash** while its defaulted `<=>` compares the string,
   so equality and ordering contradict each other.
5. **A probability CDF is built by iterating an `unordered_map`**, so the same seed can assign a
   different income category on a different standard library.
6. **An unseeded run is silent, and the results file records the seed as `0`** — a provenance record
   that is actively wrong in the one case where it matters.

Separately, the data and examples are in poor repair: `HLM_India` fails outright on a disease-name
inconsistency internal to the data repository, `KevinHall_FINCH` references a model input file that
does not exist, no primary example config carries the `project_requirements` block that current
behaviour is gated on, and 177 MB of committed simulation output makes up 62% of the examples repo.

## What the old rewrite changed, at a glance

It is a coherent determinism-and-robustness effort. **Twelve of the nineteen baseline issues are
fixed**, five inherited, one half-fixed, one made worse.

- **Determinism.** Scenarios run sequentially instead of on concurrent threads — verified
  byte-identical across three same-seed runs with an intervention active, where the baseline was
  not. Most data parallelism removed. Measured cost is small, because the baseline's parallelism was
  not buying much either (37 s vs 39 s at one and ten threads).
- **RNG.** The entropy-seeded default constructor is deleted, so an unseeded generator cannot be
  constructed. Two distribution bugs fixed. Integer sampling reimplemented — which fixes an
  out-of-range edge but introduces modulo bias, and shifts every draw, so rewrite and baseline
  output can never be compared.
- **Correctness.** The repository race fixed twice over; `Identifier` equality fixed; all
  `<cctype>` calls made `unsigned char`-safe; the income CDF rebuilt over an explicitly ordered
  vector with a numerically stable softmax.
- **Structure.** `model_parser.cpp` (2,252 lines) split into 11 units, `analysis_module.cpp` (2,202)
  into 5, `data_manager` into 7. A new structured input-diagnostics system accumulates located,
  coded issues instead of throwing on the first one.
- **Data and config.** Vendors a partial data copy and one FINCH-derived model pack, so it runs
  offline. Four incompatible config-schema changes mean **no upstream example runs unmodified**.

## Top old-rewrite issues

15 findings in `08-rewrite-issues.md`; the five high-severity ones:

1. **No licence file**, in a derivative of BSD-3-Clause code whose terms require the notice to be
   retained.
2. **No test suite** — 471 passing tests deleted. Combined with the RNG change (its output cannot be
   compared to the baseline's) and the config change (no shared configuration exists), the rewrite is
   **unverifiable by any means currently available**. It is demonstrably deterministic; nothing
   demonstrates it is correct.
3. **Person IDs are slot-based and reused**, so per-person tracking output silently conflates a dead
   person with the newborn that takes their slot. The baseline uses a monotonic never-reused counter.
4. **One floating-point reduction was left parallel** (`demographic.cpp:344`) when the same pattern
   was removed from five other sites — an incomplete determinism campaign.
5. **Derived predictor resolution is absent**, and a swallowing catch substitutes an expected value
   when a lookup fails, so a misspelled coefficient name produces plausible numbers instead of an
   error.

Also: all PIF data removed while the code and schema remain; no provenance for any vendored data;
19 comment lines in 13,059.

## Recommended starting points for the new rewrite

1. **Port the baseline's 471 tests first, before writing model code.** They are the only
   independent evidence of correctness that exists, and the old rewrite demonstrates precisely what
   happens without them.
2. **Take the old rewrite's diagnostics design, and extend it** to CSV and data-index loading, which
   it does not yet cover.
3. **Adopt its determinism fixes wholesale** — sequential execution, no entropy-seeded RNG, ordered
   sampling, `unsigned char`-safe character handling, string-based `Identifier` equality — and
   finish the job at the one site it missed.
4. **Reject its regressions**: use a monotonic person-ID counter, rejection sampling instead of
   modulo, and no swallowing catch blocks.
5. **Make the invariants structural rather than conventional.** Both codebases keep RNG draws out of
   parallel regions by discipline alone; both would compile if that discipline broke. Encode it.
6. **Write the reproducibility test neither codebase has.** Nothing in 471 tests runs a simulation
   twice and compares. That test is cheap and would have caught the baseline's one observable
   nondeterminism.
7. **Fix the reference example before relying on it.** `KevinHall_FINCH` is broken upstream and is
   also what 35 skipped tests depend on.

**Before any of that, seven questions need your ruling** — see `09-ideas-and-questions.md`. The two
that block design rather than detail are: *how will the new rewrite be validated?* (bit-exact
against the baseline forecloses most improvements; statistical equivalence does not), and *is
cross-platform bit-reproducibility a requirement?* (half the recommendations above are motivated by
assuming it is).
