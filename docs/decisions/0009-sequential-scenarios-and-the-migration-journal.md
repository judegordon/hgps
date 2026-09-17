# 0009 — Scenarios run sequentially; net migration travels in a journal

## Status

Accepted, 2026-09-17. Follows a ruling (sequential execution) plus a design choice (the journal).

## Context

The baseline runs the baseline and intervention scenarios of a trial run on two concurrent
`std::jthread`s that rendezvous every simulated year through a `SyncChannel`: the baseline `send`s a
`NetImmigrationMessage`, the intervention `try_receive`s it under a `sync_timeout_ms` deadline. This
coupling exists because the intervention must see the *same* net migration as the baseline for the
comparison to be attributable to the policy.

It is also the mechanism behind the audit's only observable nondeterminism (B-01): with an
intervention active, three same-seed runs produced three different files, identical after sorting.
`SyncChannel::close()` additionally notifies before setting its flag without holding the mutex
(N-10), so a waiter can miss the wake-up and block until the timeout.

The earlier rewrite made scenarios sequential and the audit verified that fixed the row-order
problem, at a cost that measurement says is small: the baseline's threading bought 37 s versus 39 s
at one and ten threads, because a global mutex serialised each parallel loop body anyway.

## Decision

Scenarios run **sequentially**, baseline first, then intervention. The rendezvous is replaced by a
**migration journal**: while the baseline scenario runs, it appends each year's net migration
figures to a `sim::MigrationJournal`; the intervention scenario then replays that journal instead of
computing its own.

- The journal is keyed by `(run, time)` and is complete before the intervention starts.
- Replaying a year not present in the journal is an `InternalError` — it means the horizons diverged,
  which is a programmer error, not user input.
- `sync_timeout_ms` is removed from the config format; a v1 config carrying it converts with a
  note, and a v2 config carrying it is an error naming the replacement (ADR 0010).
- The journal carries three kinds of payload, not one. Net migration is the obvious one, but the
  baseline's channel also passes the residual-mortality table and the risk-factor adjustment
  tables, and the intervention scenario must see exactly the figures the baseline computed rather
  than recompute them from its own population. Adjustments arrive once per year per call site, so
  they are a queue replayed in recording order with a cursor reset at the start of each run;
  migration and residual mortality are keyed by (run, year).

## Alternatives

- **Keep threads, fix the channel, and sort the rows before writing.** Preserves a concurrency
  hazard and a timeout for a speed-up the audit measured as noise.
- **Keep threads but write results through a single owner.** Removes the row-order defect, keeps the
  rendezvous and the timeout.
- **Recompute migration in the intervention scenario.** Cheapest to implement and wrong: the two
  futures would then differ by sampling noise as well as by the policy, which defeats the method.

## Consequences

A trial run takes the sum of two scenario runs rather than the max. On the audit's measurements that
is close to the whole of the difference, and it buys byte-identical output. The journal is a small
explicit data structure that can be inspected, and it makes the baseline→intervention dependency
visible in the type system instead of implicit in a channel.
