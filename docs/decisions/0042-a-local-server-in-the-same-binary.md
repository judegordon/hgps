# 0042 — `hgps serve`: a local JSON server, in the same binary, on cpp-httplib

## Status

Accepted, 2026-09-18. The first consumer of the library boundary
[ADR 0032](0032-library-and-a-thin-cli.md) drew, and the reason several gaps in
[docs/api.md](../api.md) now have a second implementor arguing about them.

## Context

The engine has been a library since the third run, with `include/hgps/` as its whole surface and
`src/app` — the CLI — as its only client. The fourth run's summary said the obvious thing about
that: *"a contract with one implementor is a description of that implementor"*. A graphical host is
what this run adds, and a graphical host needs a process that speaks HTTP.

Three questions had to be answered before any of it: where the server lives, what serves the HTTP,
and how a browser is meant to reach the results.

## Decision

### One binary: `hgps serve`, a subcommand of the existing CLI

Not a sibling `hgps-serve` target. The CLI already links `hgps::engine` and nothing else, already
parses arguments with hand-written code that has tests, and already has a reporter that turns events
into output. A second executable would duplicate all three and would drift: the thing that goes
wrong is a flag the CLI supports and the server does not, six months later, with no compiler error.

The cost is that the CLI binary now links an HTTP library it does not need when simulating. That is
a header-only library, and the CLI is not a thing anybody ships by size.

`healthgps --config FILE` keeps working exactly as before — the subcommand is recognised only as the
first argument, so no existing command line changes meaning. A test asserts that.

### cpp-httplib, from the pinned vcpkg baseline

The requirements are small and unusual in one place: **a synchronous, blocking, single-dependency
server that can hold a response open for server-sent events.** Everything else is a GET returning
JSON.

`cpp-httplib` is one header, is in the vcpkg baseline this project already pins
(`bd2b548…`, version 0.15.3), needs no code generation and no event loop, and has
`set_chunked_content_provider`, which is exactly the SSE primitive. It is MIT.

Rejected:

- **Boost.Beast.** The capable answer, and it brings Boost.Asio, an executor model and a build-time
  cost into a project whose entire dependency set is `fmt`, `nlohmann_json` and GoogleTest
  ([ADR 0014](0014-minimal-dependency-set.md)). An event loop is not needed to serve one user on
  localhost.
- **Crow / Drogon.** Frameworks, with routing DSLs and their own JSON types. The second of those is
  the problem: this project already has `nlohmann_json` and a JSON cursor with located diagnostics,
  and a framework that wants its own would mean two JSON libraries in one binary.
- **Writing a small HTTP server.** Tempting for something this simple, and wrong: chunked transfer
  encoding, keep-alive and header parsing are where the bugs are, and they are not this project's
  subject. The hand-written *argument parser* was a defensible few dozen lines
  ([ADR 0014](0014-minimal-dependency-set.md)); an HTTP server is not the same size of thing.

### Loopback only, enforced before the socket opens

There is no authentication. That is a deliberate consequence of localhost-only, not an omission to
be fixed later: a simulation host with no auth on a public address is a remote code execution
surface, since a configuration names files to read and a folder to write.

So `--host` accepts `127.0.0.1`, `::1` and `localhost`, and **refuses anything else at start-up**,
with a message saying why and pointing at a reverse proxy. Refusing is what makes "no auth" a
defensible decision rather than a default nobody chose.

Paths from a client are resolved against a set of roots and rejected if they escape after following
symlinks — the same rule the equivalence harness's scratch directories already follow
([ADR 0039](0039-scratch-directories-copy-what-they-may-write.md)).

### One run at a time, refused rather than queued

`docs/api.md` says two `execute` calls must not overlap in one process: the worker pool and the
parallel-region guard are process-wide. The server could have hidden that behind a queue. It does
not — a second run is refused with `409` and the active run's id.

A queue would turn a stated constraint into an unstated wait, and the first time somebody started a
forty-minute `HLM_India` run and then a ten-second one, the ten-second one would appear to hang. The
refusal is honest and a client can show it.

### A run's history lives in its manifest, not in the server

There is no database and no in-memory registry that outlives the process. `GET /api/runs` reads the
runs directory and parses each run's manifest ([ADR 0034](0034-a-run-manifest-beside-the-results.md)).

That decision is why a restart loses nothing, why a runs directory copied from another machine lists
correctly, and why the run manifest had to carry everything in the first place. It is the manifest
justifying itself: the feature that needed it did not exist when it was designed.

### The reduction for charting lives here, not in the client

`GET /api/runs/{id}/summary` reduces the result CSV to one value per (scenario, year, variable). That
is computation in a layer that otherwise only reports, and it earns its place: the alternative is
every client re-implementing the count-weighted reduction that
[docs/equivalence-method.md](../equivalence-method.md) had to get right, and getting a different
answer. A chart that disagrees with the harness about what `mean_bmi` means would be worse than no
chart.

## Consequences

- The engine's gaps in [docs/api.md](../api.md#what-is-not-here-yet) now have a second implementor.
  Gap 1 (results in memory) is felt immediately: the summary endpoint parses a CSV the engine wrote
  seconds earlier, in the same process. That is the argument for closing it, and it is deliberately
  *not* closed in this run — the endpoint works, and it makes the cost of the gap concrete rather
  than hypothetical, which is a better basis for the design decision than guessing was.
- Gap 7 (observable cancellation) is felt as the `202` on cancel, with the state change arriving on
  the event stream. That is the honest shape, and it is more machinery in the client than an
  observable token would need.
- The `--web` directory is served as static files, so the whole graphical host is this binary plus
  one folder ([ADR 0043](0043-a-plain-typescript-frontend.md)).
