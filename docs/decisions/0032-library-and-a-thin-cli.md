# 0032 — The engine is a library with a published API; the CLI is a client of it

## Status

Accepted, 2026-09-18.

## Context

Until this run there was one way to run a simulation: the `healthgps` executable. The static library
`hgps` existed, but only as a compilation unit — everything under `src/` was on its public include
path, and `src/app/main.cpp` held the sequence that makes a run happen: load the config, resolve the
data, open the store, load the inputs, build one set of modules per scenario, open the writer,
construct the engines, run the runner, print.

Three things were wrong with that, and only the third was new.

**The sequence was already duplicated.** `tests/support/simulation_harness.cpp` was a
hundred-and-ten-line copy of `main()`'s body, kept in step by hand. It existed because the
reproducibility tests have to exercise the real pipeline rather than a test-only arrangement of it,
which is right — but the way to get that is for the pipeline to be callable, not copied. The two had
already drifted: the harness overrode `config->output.folder` after loading, which no caller outside
a test could do.

**There was no boundary to cross.** `target_include_directories(hgps PUBLIC src)` meant the CLI, the
tools and the tests all saw every internal header. Nothing stopped the CLI from reaching into
`model/` and nothing would have noticed if it had.

**A GUI needs the same sequence, and more.** The next major item is a graphical host
([docs/backlog.md](../backlog.md)). It needs to validate a config and show the problems without
running anything; to show what a run *would* do before starting it; to start a run, watch it, and
stop it. None of that is expressible against an executable that prints to stdout and exits.

## Decision

**The engine becomes a library target `hgps::engine` whose public API is `include/hgps/` and nothing
else.** Five headers: `engine.h`, `diagnostics.h`, `events.h`, `cancellation.h`, `version.h`. The
API is four calls and three opaque handles:

```
load_configuration(path, options, report) -> optional<Configuration>
resolve_data(configuration, report)       -> optional<DataHandle>
build_run(configuration, data, report)    -> optional<Run>
execute(run, options, subscriber, token, report) -> RunSummary
```

Each of the first three accumulates diagnostics into a `Report` the caller owns and returns nullopt
if any of them is an error, so a host can offer "validate" as a button that finishes in a second and
"run" as one that does not. `Run::description()` answers what the run will do — country, cohort
size, horizon, seed, per-run seeds, scenario names — which is what `--dry-run` prints and what a GUI
would show on a confirmation screen.

**Four steps rather than one, because they fail differently and cost differently.** Loading a config
is milliseconds and needs no network. Resolving data may download two hundred megabytes. Building a
run reads every model file and is where most input mistakes are found. Executing is minutes. A
single `run(path)` call would make all four indistinguishable to a host that wants to show progress
or let somebody fix a typo.

**The handles are opaque, and move-only.** `Configuration`, `DataHandle` and `Run` are pimpl types.
The configuration's internal shape is this project's business and changes; publishing
`config::Config` would publish the whole of `config/types.h`. They are move-only because each owns
something whose identity matters — and `Run` in particular *cannot* be copied or even moved after
construction, because the modules it holds keep references into the loaded inputs and the scenario
journal it also holds. That is not an accident of this refactor: a module holding a reference to a
disease-definition map that outlives it is how this implementation avoids the baseline's lazily
populated, concurrently mutated repository (audit B-02). The price is a stable owner, and a
`unique_ptr<Impl>` behind a move-only handle is how it is paid.

**Diagnostic codes cross the boundary as strings.** Internally an issue carries a closed
`IssueCode` enumeration, deliberately: a loader that can invent a code is a loader whose messages
nobody tests ([ADR 0007](0007-two-tier-diagnostics.md)). Publishing that enumeration would mean
every new internal code is a change to a published header, and a caller switching on it would fail
to compile against a code it has never heard of. So `api::Diagnostic::code` is the enumerator's name
as a string, and `Report::contains("config_bad_value")` is how a caller asks.

**The CLI keeps exactly two things: argument parsing and printing.** `src/app/` is `options.cpp`
(the hand-written parser), `reporter.cpp` (an `EventSubscriber` that writes lines) and `main.cpp`
(fifty lines of sequencing and exit codes). It links `hgps::engine` only, so `src/` is not on its
include path and an internal include is a compile error.

**The tests and the tools opt into the internals by name.** `hgps::internal` is an interface target
that adds `src/` to the include path. The tests link it because they test the parts as well as the
whole — 554 tests of which most are of one class or one loader — and the two tools link it because
the converter drives the config loader's own validation and the fixture generator writes the shapes
the loaders read. Keeping that as a *separate target* is what makes the CLI's boundary real: the
tests opt in explicitly, and nothing opts in by accident.

**The boundary is tested, not asserted.** `tests/app/cli_boundary_test.cpp` reads the CLI's sources
and the published headers and checks what they include. CMake enforces it today; the test is there
because a change to the target's include directories would silently remove the enforcement while
everything went on working. It is the same tactic as the `<cctype>` check (determinism clause D12),
for the same reason: the rule is about the shape of the tree, so the tree is what gets read.

**A second output-folder option, because the CLI's rule is not the library's.** `--output` is
allowed only when the config leaves `output.folder` empty, and giving it in both places is an error
— upstream's rule, and the right one for somebody who has just typed a command. A host that keeps
results in a directory of its own choosing has not made a mistake, so `LoadOptions` has a separate
`output_folder_override` that replaces the config's value with no diagnostic. Setting both is an
error. This is the drift in the old test harness, made legitimate: the harness now uses the
override, which is what it always wanted.

## Alternatives

- **One `run(config_path, options)` call.** Smallest API, and useless for a host: no way to validate
  without running, no way to show what a run will do, no way to tell a download from a simulation.
- **Publish the internal types.** `config::Config`, `diag::IssueReport` and `sim::ResultRow` are
  already there and already work. Publishing them makes every internal change a published change,
  and `sim::ResultRow` in particular drags in `model::ModelResult`, which is the whole result model.
- **A C ABI.** The right answer if the library were to be consumed from another language or shipped
  as a shared object with a stability promise. Neither is a requirement
  ([ADR 0013](0013-platforms-linux-and-macos.md) targets two platforms and one build), and a C ABI
  would cost an error-code convention, opaque-pointer lifetime rules and a hand-written marshalling
  layer for the event structs. If a Python binding is ever wanted, this decision should be revisited
  rather than worked around.
- **Header-only, or a shared library.** Static is what the tests, the tools and the CLI all want,
  and a shared library would need symbol visibility rules and an ABI promise nothing yet asks for.
- **Leave `src/` on the public include path and rely on review.** That is what was there. The
  duplicate pipeline in the test harness is what it produced.
- **Let the engine print.** `fmt::print` in the engine would have made the reporter unnecessary and
  a GUI impossible. [ADR 0033](0033-an-event-stream-the-simulation-cannot-see.md) covers the
  replacement.

## Consequences

`src/app/build_modules.{h,cpp}` moved to `src/engine/` and its namespace changed from `hgps::app` to
`hgps::engine`: it was always engine wiring and only lived under `app/` because that is where
`main()` was. The module layout in [docs/design.md](../design.md) gains `engine` at the top of the
dependency order and `app` becomes the CLI alone.

`main()` went from 190 lines to 80, and the 110 lines it lost are in `src/engine/session.cpp`, where
the test harness's copy of them also went. The harness is now 50 lines, four of which are API calls.

`hgps_cli` exists as a library so that `tests/app/options_test.cpp` can test the argument parser —
which was untested while it was one target with `main()`, and which is now the one thing the CLI
owns. That is 21 new tests for code that already worked, and writing them turned up nothing, which
is the ordinary outcome of covering something that has been exercised by hand for two runs.

The public API is documented in [docs/api.md](../api.md), with a usage example that is compiled as
part of the test suite rather than being prose that drifts.
