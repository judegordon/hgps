# 0008 — The determinism contract, enforced by types

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

## Context

The baseline's reproducibility holds by convention. Its single RNG stream is consumed by modules in
a fixed order, and every draw site sits in a serial loop *by discipline* — the audit's N-4 records
that adding one `context.random()` call inside any existing `tbb::parallel_for_each` would introduce
both a data race and nondeterminism, and would compile without complaint. The earlier rewrite
removed most of the parallelism but kept the same conventional guarantee, and missed one site
(`demographic.cpp:344`, finding R-04), which is what happens to conventions.

Measured facts from the audit: baseline-only runs are bit-identical across repeats and thread
counts; runs with an active intervention produce **different files every time** because result rows
arrive in thread-completion order (B-01, experiments D/E). The numbers are reproducible; the file is
not.

## Decision

Ruled by the project owner. The contract is:

> Same config + same seed + same data + same binary ⇒ byte-identical CSV output, every run,
> regardless of thread count.

and it is enforced **structurally**. The full table of fourteen clauses and their enforcement
mechanisms is in `docs/design.md` §4. The four that the ruling names specifically:

- **RNG draws cannot happen inside a parallel region.** The parallel helpers take a callable that
  receives no RNG handle, `RandomSource` is neither copyable nor movable, and every draw checks a
  thread-local `in_parallel_region` flag and throws `InternalError` if it is set. A capture that
  smuggles a handle in fails on its first draw with a source location, rather than producing a
  plausible number.
- **`Categorical<T>` is constructible only from an ordered sequence.** The `std::unordered_map`
  constructors are declared and deleted, so the baseline's income-CDF defect (B-05) is a compile
  error here.
- **Person IDs come from a monotonic lifetime-unique counter**, and the initial cohort takes IDs
  `1..N` by index so baseline and intervention align person for person (ADR 0017).
- **Integer sampling is by rejection**, never modulo (ADR 0015).

Plus: fixed-order reductions with a block decomposition independent of thread count; sequential
scenario execution; a single owner per output file emitting rows in a defined order.

## Alternatives

- **Convention plus review**, as in both existing codebases. Demonstrably insufficient: the earlier
  rewrite's determinism campaign missed a site, and the baseline's row order was never noticed at
  all.
- **A global "deterministic mode" flag.** Two behaviours means the untested one rots, and the
  interesting bugs live in the seam.
- **Sorting the output as the fix for row order.** Treats the symptom; the underlying concurrent
  publication remains, and so does the need for every future row-producing site to remember.

## Consequences

`RandomSource` being immovable means it cannot live in a container or be returned by value; it is
owned by `RuntimeContext` and handed out by reference. The parallel-region check costs one
thread-local read per draw, which is not measurable against the arithmetic around it. Some code is
more awkward than the obvious version — that awkwardness is the enforcement.
