# 0033 — Progress is an event stream the simulation cannot see

## Status

Accepted, 2026-09-18.

## Context

Something has to tell a person that a forty-year run is a third of the way through. Until this run
that something was `std::cout` in `main()`, which printed one line when the run finished.

Two reasons to change it. The library now has hosts other than a terminal
([ADR 0032](0032-library-and-a-thin-cli.md)), and a library that prints is a library a GUI cannot
use. And a forty-year `HLM_India` run takes forty-two minutes with no output at all until it ends,
which is indistinguishable from a hang.

The obvious implementations are all wrong in the same way. `fmt::print` inside the year loop puts the
presentation inside the engine. A `std::function<void(std::string)>` logger makes the engine decide
the wording. And the baseline's own answer — an event bus with a concurrent queue that the analysis
module publishes results onto — is audit finding B-01: it is why its output row order depends on
thread scheduling, and it is explicitly not coming back
([ADR 0020](0020-output-single-owner-defined-row-order.md)).

The real constraint is narrower than "don't print". The top requirement of this project is that the
same config, seed, data and binary produce byte-identical output every run
([docs/design.md](../design.md) section 4). **Anything that watches a run must be incapable of
changing it.** That is not a promise to keep by being careful; it has to be a property of the
interface.

## Decision

**Typed events through a subscriber interface, one virtual function per event, every one with an
empty default.** `api::EventSubscriber` has six: run started, scenario started, year completed,
scenario completed, run completed, diagnostic raised. Inside the engine the same thing is a
`sim::RunHooks` of `std::function`s that `Runner` and `Engine` call; `session.cpp` builds those from
the subscriber. The internal hop exists so that `sim/` does not include a public header and the
public event structs are not what the year loop is written against.

**Events carry copies of numbers the run has already computed, and return nothing.** Every callback
returns `void` and takes a `const` reference to a struct of plain values. There is no way for a
subscriber to reach the random source, the population, the scenario or the work order, because none
of those is in the signature. That is the whole of the guarantee: it is enforced by what the
functions can be handed, not by a rule about what they may do.

**Delivery is synchronous, on the calling thread, in production order.** No queue, no worker, no
buffering. A subscriber therefore needs no locking, and it blocks the run for as long as it takes —
which is a real cost, stated in [docs/api.md](../api.md), and much better than the alternative: a
queue between the engine and its observer is exactly the mechanism behind B-01.

**The year timing is wall-clock, measured around the year, and nothing in the simulation reads it.**
`YearCompleted::elapsed_ms` is a `steady_clock` difference. A clock read is the one thing here that
is genuinely non-deterministic, so the test that matters is not that the events are right but that
they cannot matter: `EventStream.ARunWithASubscriberWritesTheSameBytesAsOneWithout` runs the same
configuration twice, once with a recording subscriber and once with none, and compares the result
files byte for byte. `ASubscriberThatWastesTimeStillChangesNothing` does it again with a subscriber
that burns two hundred thousand iterations per callback, so the timings are certain to differ
between the two runs.

**`RunStarted::total_years` exists so a progress bar has a denominator before the first year.** Its
contract is "the number of `YearCompleted` events an uncancelled run will emit", and a test asserts
the two agree rather than leaving a host to derive it from the horizon and get the scenario count
wrong.

**Cancellation is part of the same design and lives in the same two places.** `CancellationToken` is
a copyable handle over one shared `atomic_bool`; the engine checks it **between** simulated years and
between scenarios, never inside a population sweep. So a cancelled run is a *prefix* of the run that
would have happened — whole years, written and closed — and
`Cancellation.ACancelledRunIsAPrefixOfTheRunThatWouldHaveHappened` asserts exactly that by checking
the partial CSV is a byte prefix of the full one.

## Alternatives

- **Print from the engine.** One line of code, and it makes every non-terminal host impossible.
- **A logging callback taking a formatted string.** Then the engine owns the wording, a GUI has to
  parse prose to get a percentage, and the events cannot be tested for content.
- **A single `Event` variant, or a callback per event as `std::function`s in an options struct.** A
  `std::variant` would make a subscriber write a visitor to ignore five of six events; a struct of
  optional callbacks is what the internal hooks are, and it is the wrong shape for the published API
  because there is nothing to inherit and document per event. One virtual function per event with an
  empty default is the cheapest thing for a caller that wants one of them.
- **An asynchronous queue.** Would stop a slow subscriber from slowing the run, at the cost of the
  exact mechanism this project removed from the baseline (B-01) and of events arriving after the run
  that produced them had finished.
- **Per-person or per-module events.** Millions of callbacks per run, and a subscriber inside the
  population sweep is a subscriber that can see a half-updated cohort. The year is the smallest unit
  at which the model is in a consistent state, so it is the unit of progress.
- **Cancellation by exception or by thread interruption.** An exception from inside a sweep leaves the
  cohort half-updated and the files half-written; there is nothing to salvage and no way to say what
  the partial output means. The flag-between-years version has a stated meaning, which is what makes
  it worth having.
- **Checking cancellation inside the population sweep, for a faster stop.** A year of `HLM_France` is
  70 ms, so the worst-case latency is already imperceptible; on `HLM_India` it is a minute, and that
  is the argument for revisiting this — but it would need a definition of what a half-simulated year
  means before it could be implemented, and there isn't one.

## Consequences

`sim::Engine::run` and `sim::Runner::run` take a `const RunHooks *` that defaults to null, so every
existing caller is unchanged. `Runner::run` now returns a `Runner::Outcome` — elapsed milliseconds,
whether it was cancelled, and how many scenario-years were produced — rather than a bare `double`,
because a cancelled run has to be able to say so.

The CLI gains `--progress`, off by default: a forty-year run at one trial produces forty lines, which
is useful on a terminal and noise in a log. Progress goes to `stderr` and the summary to `stdout`, so
piping one does not swallow the other.

`on_diagnostic` has no producer yet. Every diagnostic the engine can raise is raised while loading,
which is the design ([ADR 0018](0018-no-swallowing-catch-load-time-validation.md)), and the method
exists because the first thing that will need it is already known: the immigration shortfall into an
emptying band (B-21) is currently a per-year metric in the results file and nothing tells anybody
about it while it happens. Adding a producer is then a change to one call site rather than to the
API.
