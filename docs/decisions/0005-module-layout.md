# 0005 — Module layout: nine layers, dependencies one way

## Status

Accepted, 2026-09-17.

## Context

The baseline has four libraries — `Core`, `Input`, `HealthGPS`, `LibConsole` — which is a sound
skeleton, but inside them the layering is not enforced: `HealthGPS.Input/model_parser.cpp` (2,252
lines) both parses JSON and constructs model objects, and `analysis_module.cpp` (2,202 lines) mixes
statistics, cost tables and output shaping. The earlier rewrite split those files up, which the
audit found straightforwardly better, but it did not change the dependency structure.

## Decision

Nine modules under `src/`, each a directory, with dependencies permitted only left to right:

```
core ← diagnostics ← random ← io ← config ← data ← model ← sim ← output ← app
```

The responsibilities and the explicit "must not" for each are tabulated in `docs/design.md` §2. The
rules that matter most:

- `core` knows nothing about config, data, RNG or models. It is the only module the tests may use
  without constructing a world.
- `random` is a module of its own rather than a corner of `core`, because the determinism contract
  hangs off its type properties (ADR 0008) and it must be impossible to reach accidentally.
- `model` may not write files; `output` may not compute statistics.
- Only `app` calls `exit`.

The build has one static library (`hgps`) and four executables (`healthgps`, `convert-config`,
`gen-fixtures`, `hgps_tests`), with the layering enforced by review and by include discipline rather
than by one CMake target per module — nine targets would slow the build for no benefit at this size.

## Alternatives

- **Keep the baseline's four libraries.** They do not separate "parse" from "construct", which is
  where the two biggest files went wrong.
- **One target per module, with CMake enforcing the arrows.** Mechanically stronger. Rejected for
  now: nine static libraries at this code size costs build time and gives CMake a vote on every
  small refactor. If the layering erodes, this is the first thing to revisit.

## Consequences

The arrows are a convention backed by review, not by the linker — the one place in this design where
an invariant is *not* enforced by a mechanism, recorded here so it is a known gap rather than an
oversight.
