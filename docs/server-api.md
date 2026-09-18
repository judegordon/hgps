# The local server's JSON API

`hgps serve` hosts the engine over HTTP on localhost, so that a browser — or a script, or another
program — can do what the CLI does: list the examples, validate a configuration and see the
diagnostics where they belong, start a run and watch it, read the results.

It is **one binary and one folder**: the same executable serves the API and the built web assets,
so the graphical host is a static site plus this, and not a second runtime to install.

    hgps serve                          # 127.0.0.1:8080, no static assets
    hgps serve --port 9000 --web web/dist
    hgps serve --runs ~/hgps-runs       # where runs are written and found

- [Ground rules](#ground-rules)
- [Errors](#errors)
- [Endpoints](#endpoints)
- [The event stream](#the-event-stream)
- [What it deliberately does not do](#what-it-deliberately-does-not-do)

## Ground rules

**`start` means it is serving.** The server binds, spawns its thread and then waits until that
thread is accepting before returning the port. It has to: cpp-httplib's `stop()` does nothing at all
unless the server is already running, so a `start()` that returned as soon as the thread was spawned
could be followed by a `stop()` that was lost, and the join then blocked for ever. A test that made
a request in between never saw it, because an answer proves the loop is running; the randomised
lifetime stress test in `tests/server/stress_test.cpp` found it on its first run.

**Localhost only, and it refuses to be otherwise.** `--host` accepts `127.0.0.1`, `::1` and
`localhost` and nothing else; anything that resolves elsewhere is refused at start-up with a message
saying so, before the socket is opened. There is no authentication, and that is only safe because
there is no way to reach it from another machine. A run reads and writes files as the user running
the server, which is the same trust boundary the CLI has.

**One run at a time.** The engine's own contract says two `execute` calls must not overlap in one
process: the worker pool and the parallel-region guard are process-wide
([docs/api.md](api.md#threading-and-determinism)). The server enforces that rather than hoping — a
`POST /api/runs` while another run is active is refused with `409` and the active run's id. Runs are
not queued, because a queue would make the refusal into a wait and hide the constraint.

**Every response is JSON**, except the result CSV download and the static assets. `Content-Type:
application/json; charset=utf-8`.

**Nothing is cached.** The server is a local tool over a directory that changes underneath it;
`Cache-Control: no-store` on every API response, so a stale run list is impossible.

**Paths from a client are resolved and checked.** A configuration path must be inside one of the
roots the server was started with — the repository's `examples/`, and `--configs` if given. A path
that escapes after resolving symlinks is a `400`, not a read. The same applies to result files: a
client names a run id and a file *name*, never a path.

## Errors

One shape, everywhere, so a client has one thing to handle:

```json
{
  "error": {
    "code": "config_invalid",
    "message": "the configuration has 2 errors",
    "diagnostics": [ … ]
  }
}
```

`code` is a stable string. `diagnostics` is present when the engine produced any, and is the same
array shape as `POST /api/configs/validate` returns — so a client renders diagnostics the same way
whether they arrived as a validation result or as the reason a run would not start.

| HTTP | `code` | When |
|---|---|---|
| 400 | `bad_request` | a malformed body, a missing field, a path outside the allowed roots |
| 404 | `not_found` | no such run, config, example or result file |
| 409 | `run_in_progress` | a second run was requested while one is active |
| 409 | `run_not_active` | cancelling a run that has already finished |
| 422 | `config_invalid` | the configuration loaded but did not validate; `diagnostics` says why |
| 500 | `internal_error` | a broken invariant in the engine. The message carries its source location, because that is a defect here |

A `422` is the interesting one: it is not a failure of the request, it is the answer. A client that
treats it as an error and shows a toast has misread the API.

## Endpoints

### `GET /api/version`

```json
{
  "engine_version": "0.2.0",
  "git_commit": "1570b19…",
  "git_describe": "…",
  "git_dirty": false,
  "platform": "Darwin arm64",
  "compiler": "AppleClang 17.0.0",
  "build_type": "Release",
  "api_version": 1,
  "baseline_compat_flags": [
    { "name": "B-24", "description": "the food-labelling policy offers its impact again, …" }
  ]
}
```

`api_version` is this document's version and changes when a client would have to. The engine fields
are `api::build_info()` verbatim — the same values the run manifest records, so a result and the
server that produced it can be tied together.

The compatibility flags are listed here rather than hard-coded in a client, because they are a
property of the build ([ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md)).

### `GET /api/examples`

Every configuration the server can see, from its configured roots.

```json
{
  "examples": [
    {
      "id": "HLM_France",
      "path": "examples/HLM_France/config.json",
      "root": "examples",
      "readable": true
    }
  ]
}
```

`id` is the directory name and is what other endpoints take. `path` is relative to `root` and is for
display; a client never sends a path back, only an `id`.

This endpoint **does not load** the configurations — it lists files. Listing a directory of a hundred
configs must not cost a hundred data-pack resolutions, and a config that fails to load is still worth
showing with the reason.

### `GET /api/examples/{id}`

One configuration, loaded and summarised, with whatever the loader said about it.

```json
{
  "id": "HLM_France",
  "path": "examples/HLM_France/config.json",
  "sha256": "…",
  "document": { … },
  "summary": {
    "seed": 123456789,
    "start_time": 2010,
    "stop_time": 2050,
    "trial_runs": 1,
    "diseases": ["alzheimer", "asthma", …],
    "active_intervention": null,
    "data_source": "…",
    "output_folder": "…",
    "baseline_compat": []
  },
  "diagnostics": [ … ]
}
```

`document` is the file's JSON as written, for the editor to open. `summary` is
`api::Configuration`'s accessors, so a client shows what the engine understood rather than what it
guessed from the document.

`diagnostics` may hold warnings on a configuration that loaded fine; a client shows them. If it did
not load, this is a `422` with the same diagnostics.

### `POST /api/configs/validate`

The heart of the editor. Takes a document and reports every problem in it, located.

```json
{ "document": { … }, "base": "HLM_France", "require_files_exist": true }
```

`base` names an example whose directory the document's relative paths resolve against — an editor is
usually editing a copy of something. Without it, relative paths are resolved against the server's
configs root and are likely to be reported missing, which is honest.

`require_files_exist: false` maps to `LoadOptions::require_files_exist` and is what an editor uses
while a document is half-written: it validates the *shape* without insisting the files are there yet.

```json
{
  "valid": false,
  "error_count": 2,
  "warning_count": 1,
  "diagnostics": [
    {
      "severity": "error",
      "code": "config_missing_required",
      "message": "required when population impact fraction is enabled",
      "location": {
        "file": "config.json",
        "pointer": "/population_impact_fraction/risk_factor",
        "line": 84,
        "column": 5
      }
    }
  ],
  "summary": { … }
}
```

**`location.pointer` is the point of this endpoint.** It is a JSON Pointer (RFC 6901) into the
submitted document, so an editor puts the message on the field it names rather than at the top of the
form. `line` and `column` are for a text editor and may be absent; `pointer` is present whenever the
engine knows which member it was talking about.

`summary` is present when the document validated, so an editor can show what it would run without a
second request.

Validation is **cheap and does not touch the data pack**: it is `load_configuration` only. Resolving
the pack may download and extract, which is a separate step by design
([docs/api.md](api.md#the-handles)) and happens when a run starts.

### `GET /api/schema`

The published configuration contract, `schemas/v2/config.json` with its `$ref`s resolved into one
document, so a client fetches one thing and can generate a form from it.

```json
{ "schema": { … }, "version": 2, "source": "schemas/v2/config.json" }
```

The `$ref`s are inlined rather than served as separate files because a schema-driven form generator
that has to fetch transitively is a form generator with a loading state per field.

### `POST /api/runs`

Starts a run. Returns at once, with an id; the run happens on the server's own thread.

```json
{
  "example": "HLM_France",
  "threads": 1,
  "baseline_compat": ["B-24"],
  "write_manifest": true
}
```

`example` is an id from `/api/examples`, and is required. `threads` defaults to 1 and changes no
result. `baseline_compat` is a list of flag names from `/api/version`, and an unrecognised one is a
`400` before anything runs.

**There is no way to run an inline document.** `POST /api/configs/validate` takes one, because
validating is quick and the scratch file it needs lives for a few milliseconds; a *run* takes
minutes to an hour, and the same trick would leave a scratch configuration in a configs directory
for the whole of it. A host that wants to run what is on screen saves it first — which needs config
*writing*, which the engine does not offer
([docs/api.md](api.md#what-is-not-here-yet), gap 5). That gap is the reason, and it is worth being
explicit that this endpoint is the poorer for it.

```json
{
  "id": "20260918T143011Z-a1b2c3",
  "state": "starting",
  "example": "HLM_France",
  "output_folder": "/Users/…/hgps-runs/20260918T143011Z-a1b2c3",
  "description": {
    "country": "FRA", "cohort_size": 10000, "start_time": 2010, "stop_time": 2050,
    "trial_runs": 1, "seed": 123456789, "run_seeds": [ … ],
    "scenarios": ["Baseline", "Intervention"], "total_years": 82
  }
}
```

The id is time-ordered and unique, so a run list sorts without parsing a date field, and it is the
output directory's name, so a run's files can be found without the server.

`description` is `api::Run::Description`, available because the run was *built* before the response
was sent. That is deliberate: building is where bad input fails, so a `201` means the run will
almost certainly finish, and a configuration problem comes back as a `422` with diagnostics rather
than as a run that dies a second later.

A run is refused with `409 run_in_progress` if another is active.

### `GET /api/runs`

Every run this server knows about: the active one, and every completed one whose manifest is in the
runs directory.

```json
{
  "active": "20260918T143011Z-a1b2c3",
  "runs": [
    {
      "id": "20260918T143011Z-a1b2c3",
      "state": "running",
      "example": "HLM_France",
      "started_utc": "2026-09-18T14:30:11Z",
      "finished_utc": null,
      "years_completed": 31,
      "total_years": 82,
      "cancelled": false,
      "manifest": null
    }
  ]
}
```

Completed runs are read **from their manifests**, which is what makes the history survive a restart:
the server holds no database, and a runs directory copied from another machine lists correctly.
`state` is one of `starting`, `running`, `completed`, `cancelled`, `failed`.

### `GET /api/runs/{id}`

One run, with its full manifest once it has one, and its diagnostics.

```json
{
  "id": "…", "state": "completed", "example": "HLM_France",
  "years_completed": 82, "total_years": 82, "elapsed_ms": 2412.5,
  "cancelled": false,
  "manifest": { … },
  "results": ["result.csv", "result.json", "result_manifest.json"],
  "diagnostics": [ … ]
}
```

`manifest` is the manifest JSON verbatim — including `baseline_compat`, so a result's provenance
reaches a client without the client knowing what to ask for.

### `GET /api/runs/{id}/events`

Server-sent events: the engine's event stream, as it happens. See below.

### `POST /api/runs/{id}/cancel`

Sets the run's cancellation token. Returns `202` at once with the run's state, because cancellation
is observed at the end of the current year and is not instant — `docs/api.md` is explicit that a
Cancel button cannot honestly change state until the run says so. The stream is where the state
change arrives.

`409 run_not_active` if the run has already finished.

### `GET /api/runs/{id}/results/{name}`

One result file, by name, from that run's directory. `name` must be one of the names the run's
`results` list gives; anything else is a `404`, and nothing is resolved as a path.

`text/csv` for a `.csv`, `application/json` for a `.json`, with
`Content-Disposition: attachment; filename="…"` so a browser's download does the obvious thing.

### `GET /api/runs/{id}/summary`

The result CSV reduced for charting: one value per (scenario, year, variable), summed or
count-weighted over age bands and sexes the way the equivalence harness reduces
([docs/equivalence-method.md](equivalence-method.md#2-the-reduction-from-rows-to-series)).

```json
{
  "id": "…",
  "reduction": "count-weighted mean over age bands; count, deaths and emigrations summed",
  "scenarios": ["Baseline", "Intervention"],
  "years": [2010, 2011, …],
  "variables": ["mean_bmi", "prevalence_asthma", …],
  "series": [
    { "scenario": "Baseline", "variable": "mean_bmi", "sex": "all",
      "values": [24.9, 24.92, …] }
  ]
}
```

`values` is parallel to `years`, with `null` for a year the variable has no value in — the first
year of a flow variable, for instance. A client plots it without a lookup.

`?sex=male|female|all` and `?variable=a,b,c` narrow it. The default is `all` and every variable,
because the first thing a client does is ask what there is.

This is the one endpoint that computes rather than reports, and it earns its place: the alternative
is every client re-implementing a reduction the harness already had to get right, and getting a
different answer.

**One thing in that reduction is wrong, in both places.** `normal_weight`, `over_weight`,
`obese_weight` and `above_weight` are head counts — the analysis module increments one per person —
so their population figure is a sum, and the rule above gives them a count-weighted mean. The series
still moves with the underlying quantity, which is why it does not look wrong; its level is
meaningless. It is not fixed here because the harness's reduction has to change with it — the two
must not disagree — and that invalidates every stored equivalence reference.
[docs/backlog.md](backlog.md) item 2 has the cost.

## The event stream

`GET /api/runs/{id}/events` is `text/event-stream`. Each message is one JSON object with a `type`,
mapping the engine's `EventSubscriber` one to one ([docs/api.md](api.md#events)):

```
event: year_completed
data: {"type":"year_completed","run":1,"scenario":"Baseline","year":2031,"elapsed_ms":28.4,"population":9981}
```

| `event:` | From | Carries |
|---|---|---|
| `run_started` | `RunStarted` | seed, horizon, cohort size, scenarios, `total_years` |
| `scenario_started` | `ScenarioStarted` | name, kind, run number |
| `year_completed` | `YearCompleted` | year, elapsed ms, population |
| `scenario_completed` | `ScenarioCompleted` | elapsed, years simulated |
| `diagnostic` | `on_diagnostic` | one diagnostic, same shape as everywhere else |
| `run_completed` | `RunCompleted` | elapsed, cancelled, the files written |
| `state` | the server | the run's state, on connect and on every change |

A client that connects **after** the run started is not left guessing: the server replays the events
so far, then continues live. Events are buffered per run for exactly that reason, and the buffer is
bounded — a run of `HLM_India` at full scale emits about 80 `year_completed` events, so the bound is
generous and a run that somehow exceeded it would drop the oldest and say so with a `truncated`
field on `state`.

The stream ends after `run_completed`. A stream for a run that has already finished sends the
buffered events and `run_completed`, then ends — so a client's reconnect logic has one path.

**The subscriber must not block the simulation.** `docs/api.md` is explicit that delivery is
synchronous on the run's thread and a slow subscriber is a slow simulation. The server's subscriber
therefore does the minimum: it formats the event, appends it to the run's buffer under a short lock,
and notifies the writers. Every HTTP connection is served from the buffer, never from the engine's
thread.

## What it deliberately does not do

- **No authentication, and no option for it.** Adding one would invite binding to a non-loopback
  address, which is the thing being prevented. A server that should be reachable from elsewhere
  wants a reverse proxy in front of it and a decision about who may run simulations on this machine,
  and neither belongs in this binary.
- **No result data in the run-start response, and no way to run two at once.** Both follow from the
  engine's contract rather than from this layer.
- **No configuration writing, and no way to run a document that is not on disk.**
  `POST /api/configs/validate` takes a document and gives back diagnostics; it does not save one,
  and `POST /api/runs` takes an example **id** and not a document. So an editor can validate what is
  on screen and cannot run it: to run it, somebody has to save it as a configuration first, which
  needs config *writing*, which the engine does not offer
  ([docs/api.md](api.md#what-is-not-here-yet), gap 5). That is the gap, stated rather than papered
  over; this section used to claim the opposite of the endpoint above it.
- **No equivalence harness.** It is a research instrument that runs two implementations and takes
  half an hour; it is not a thing to poke from a browser.
