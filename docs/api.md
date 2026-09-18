# The public API

`hgps::engine` is the simulation as a library. This document is its contract: what the four calls
do, what the handles own, what the events guarantee, and what a host still has to do for itself.

The headers are under `include/hgps/`, and that directory is the whole API surface — nothing under
`src/` is reachable from a caller, and `tests/app/cli_boundary_test.cpp` checks that both ways round.
[ADR 0032](decisions/0032-library-and-a-thin-cli.md) says why the surface is shaped this way and
what was rejected.

## The shape of it

```
load_configuration(path, options, report)         -> optional<Configuration>
resolve_data(configuration, report)               -> optional<DataHandle>
build_run(configuration, data, report)            -> optional<Run>
execute(run, options, subscriber, token, report)  -> RunSummary
```

Four calls, because they fail differently and cost differently. Loading a configuration is
milliseconds and needs no network. Resolving data may download two hundred megabytes. Building a run
reads every model file and is where most input mistakes are found. Executing is minutes. A single
`run(path)` would make all four indistinguishable to a host that wants to show progress, or to let
somebody fix a typo without waiting for a download.

Each of the first three returns `nullopt` if anything it found was an error, and appends everything
it found — errors and warnings both — to the `Report` the caller owns. **A successful call can still
have filled the report**, so a caller reads it either way.

## A minimal host

This is `tests/engine/api_example.cpp`, quoted exactly: it is compiled and run by the test suite,
and `ApiExample.TheDocumentQuotesThisFile` fails if this block and that file drift apart.

```cpp
#include "hgps/engine.h"

#include <iostream>

namespace hgps::example {

/// Prints a line as each simulated year finishes.
class YearPrinter final : public api::EventSubscriber {
  public:
    void on_year_completed(const api::YearCompleted &event) override {
        std::cout << event.scenario << ' ' << event.year << ": " << event.population_size
                  << " people, " << event.elapsed_ms << " ms\n";
    }
};

/// Runs one configuration. Returns 0 on success, 3 for a problem with the inputs, 70 otherwise.
int run_one(const std::filesystem::path &config_path) {
    api::Report report;

    // Step 1: the configuration. Everything wrong with it is reported together.
    const auto configuration =
        api::load_configuration(config_path, api::LoadOptions{}, report);
    if (!configuration.has_value()) {
        std::cerr << report.to_string();
        return 3;
    }

    // Step 2: the data. Downloads and verifies if the source is an archive or a URL.
    const auto data = api::resolve_data(*configuration, report);
    if (!data.has_value()) {
        std::cerr << report.to_string();
        return 3;
    }

    // Step 3: the run. Reads every model file; this is where most input mistakes are found.
    auto run = api::build_run(*configuration, *data, report);
    if (!run.has_value()) {
        std::cerr << report.to_string();
        return 3;
    }

    // Warnings are worth seeing even when everything loaded.
    std::cerr << report.to_string();
    std::cout << run->description().cohort_size << " people, " << run->description().start_time
              << "–" << run->description().stop_time << ", seed " << run->description().seed
              << '\n';

    // Step 4: run it. The token is never cancelled here; call cancel() on a copy from any thread
    // and the run stops at the end of its current year.
    YearPrinter printer;
    const api::CancellationToken cancellation;
    const auto summary = api::execute(*run, api::RunOptions{}, &printer, cancellation, report);

    for (const auto &path : summary.outputs) {
        std::cout << path.string() << '\n';
    }
    return summary.succeeded ? 0 : 70;
}

} // namespace hgps::example
```

Build against it with CMake:

```cmake
add_subdirectory(hgps_new_rewrite)
target_link_libraries(my_host PRIVATE hgps::engine)
```

`hgps::engine` carries its own include directory, so `#include "hgps/engine.h"` is all a caller
needs. `hgps::internal` also exists and exposes `src/`; it is for this project's own tests and tools,
and a host that links it has left the supported surface.

## The handles

| | Owns | Lifetime |
|---|---|---|
| `Configuration` | the validated configuration document | may outlive the `Run` built from it, and does not have to |
| `DataHandle` | the opened data store and the directory it came from | must outlive the `build_run` call; not needed afterwards |
| `Run` | every loaded input, every model, the scenario journal | must outlive `execute`; **must not be moved after it is built** |

All three are move-only and opaque. `Run` is the one with a real constraint: the modules it holds
keep references into the loaded inputs and the journal it also holds, which is how this
implementation avoids the baseline's lazily populated, concurrently mutated repository (audit B-02).
The handle is a `unique_ptr` to a stable allocation, so moving the handle is fine and the thing it
points at never moves.

`Run::description()` answers what the run will do before it starts: country, disease and risk-factor
counts, cohort size, horizon, the master seed, each trial run's derived seed, and the scenario names
in the order they will run. That is what `--dry-run` prints, and what a confirmation screen would
show.

## Diagnostics

Two severities, `warning` and `error`, and nothing else
([ADR 0007](decisions/0007-two-tier-diagnostics.md)). An error means the run cannot proceed; a
warning says what was assumed instead of stopping.

Every `Diagnostic` carries a `code`, a `Location` and a `message`. The code is a stable string —
`"config_bad_value"`, `"data_checksum_mismatch"`, `"model_unknown_predictor"` — rather than an
enumerator, so that adding a code inside the engine is not a change to a published header and a
caller switching on one it has never heard of still compiles. `Report::contains("…")` is how a caller
asks whether a particular thing went wrong.

`Location` holds a file, a JSON pointer or column name, and a line and column where they are known.
`Diagnostic::to_string()` and `Report::to_string()` format them for a terminal; a host with a
different idea of presentation reads the fields.

## Events

`execute` takes an `EventSubscriber *`, or null. The subscriber receives, in this order:

| Event | When | Carries |
|---|---|---|
| `RunStarted` | once, before anything runs | seed, horizon, cohort size, scenario names, and `total_years` — a denominator for a progress bar |
| `ScenarioStarted` | per scenario per trial run | scenario name, kind, run number |
| `YearCompleted` | per simulated year | year, wall-clock milliseconds, population size |
| `ScenarioCompleted` | per scenario per trial run | elapsed time, years actually simulated |
| `RunCompleted` | once, after the files are closed | elapsed time, whether it was cancelled, every file written |
| `on_diagnostic` | whenever the run itself finds something | one `Diagnostic` |

Every method has an empty default, so a subscriber implements what it uses.

**Delivery is synchronous, on the thread that called `execute`, in the order the run produced them.**
A subscriber therefore needs no locking — and it also blocks the run for as long as it takes, so a
slow subscriber is a slow simulation. A GUI's subscriber should post to its own queue and return.

**Events cannot change results.** Nothing emitted is derived from the random stream, and nothing a
subscriber does is visible to the simulation: there is no way to reach the RNG, the population or the
work order from an event. `EventStream.ARunWithASubscriberWritesTheSameBytesAsOneWithout` asserts
it rather than trusting it. [ADR 0033](decisions/0033-an-event-stream-the-simulation-cannot-see.md)
has the mechanism.

The library writes to neither `stdout` nor `stderr`. There is no stream in its API; the events are
how a host learns what to print, and `src/app/reporter.cpp` is the whole of the CLI's printing.

## Cancellation

`CancellationToken` is a copyable handle over one shared flag. `cancel()` may be called from any
thread — a UI thread, a signal handler's worker — while `execute` runs on another.

The engine checks it at the two points where stopping is safe and cheap: **between simulated years
and between scenarios**. Never inside a population sweep, so cancellation cannot land between two
people and leave a half-updated cohort. A cancelled run is therefore a *prefix* of the run that would
have happened: whole years, written and closed, with `RunSummary::cancelled` set and the manifest
recording it. An uncancelled run is bit-for-bit what it would have been with no token at all.

A default-constructed token is never cancelled, and is what a caller that does not want cancellation
passes.

## Results

The engine writes files; it does not hand back a result table. `RunSummary` lists what it wrote:

- `result_csv` — the aggregated result table, one row per (scenario, run, year, sex, age band);
- `result_json` — the same run's metadata and per-year results;
- the income-stratified CSVs, when the configuration enables income analysis;
- `manifest` — the run manifest ([ADR 0034](decisions/0034-a-run-manifest-beside-the-results.md)):
  config hash, data checksum, the seed actually used, engine version and commit, host platform,
  start and end times, and the scenarios that ran.

A host that wants the numbers in memory rather than on disk does not have what it needs yet. That is
a deliberate gap rather than an oversight: the output contract is "one owner per file, rows in a
defined order" ([ADR 0020](decisions/0020-output-single-owner-defined-row-order.md)), and an
in-memory result sink has to be designed so that it cannot become a second, differently ordered
output path.

## What is not here yet

This API has one implementor — `src/app`, the CLI — and a contract with one implementor is a
description of that implementor. The gaps below are the ones a *graphical* host would hit, listed
here because they are properties of this document rather than of any host, and worked through with
costs in [docs/backlog.md](backlog.md) item 1.

| | Missing | Consequence for a host |
|---|---|---|
| 1 | **Results in memory.** `execute` writes files; `RunSummary` lists their paths | a host draws its charts by parsing files the engine has just written |
| 2 | **Per-year results in the event stream.** `YearCompleted` carries the year, the elapsed milliseconds and the population size, and no results | a run can be shown progressing but not shown *happening* |
| 3 | **Progress inside a year.** Events and cancellation are both per-year. A year of `HLM_India` at full scale is about a minute | a progress bar that moves once a minute. The *cancellation* granularity is deliberate and should stay — see above — but the event need not be |
| 4 | **Structured diagnostic arguments.** `code` and `Location` are structured; the message is prose with the numbers formatted into it | a config error can be located in a file but not turned into a field-level annotation with the offending value |
| 5 | **Writing a configuration.** The loader reads config v2 and `tools/convert-config` writes it; there is no supported call for a host to modify a `Configuration` and save it | a host can run configurations but not edit them |
| 6 | **Enumerating a data store.** `Run::description()` says what *this* run will do; nothing says what a pack *offers* — its countries, diseases and risk factors — before a run exists | a host cannot populate a chooser without a run to ask |
| 7 | **Observable cancellation.** `cancel()` returns at once and the run stops at the end of its current year; there is no way to ask whether it has noticed | a Cancel button cannot honestly change state until `RunCompleted` |

None of these is hard. The reason they are listed rather than built is that items 1 and 2 are one
design decision about result ownership, and making it for a host that does not exist yet is how the
wrong answer gets locked in.

## Threading and determinism

`RunOptions::threads` sets the worker count for the parallel sections that draw no randomness. The
results are byte-identical at any count; if they are not, that is a bug
([docs/design.md](design.md) section 4).

The setting is scoped to the `execute` call, so a host may call `execute` twice with different counts
and get what it asked for each time. Two `execute` calls must not overlap in time: the worker pool
and the parallel-region guard are process-wide, and the engine is not designed to run two
simulations at once in one process. Run them in sequence, or in separate processes.

## What it throws

Nothing a caller is expected to handle. Problems with the caller's input are diagnostics, always. A
broken invariant inside the engine, or a file it cannot write, comes out as a `std::runtime_error`
whose message carries a source location — because that is a defect here, not a mistake there.

## Versioning

There is none yet. `api::build_info()` reports the engine version, the commit that built it, the
platform and the compiler, and the API is at whatever shape this commit gives it. Nothing outside
this repository depends on it, so it changes when a reason appears; when something does depend on it,
this section should be replaced with a compatibility promise rather than left as it is.
