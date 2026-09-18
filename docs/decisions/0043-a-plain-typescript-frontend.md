# 0043 — A plain TypeScript frontend, with Vite and no framework

## Status

Accepted, 2026-09-18. The graphical host [ADR 0042](0042-a-local-server-in-the-same-binary.md)
serves.

## Context

Four screens: a configuration editor generated from the published JSON schema, a run launcher with
live progress, a results browser with tables and line charts, and a run history. One user, on
localhost, against a server that holds one run at a time.

This is a research tool. The people who will read this code are the people who read the C++ next to
it, and the thing that would make it useless is not slow rendering — it is being unable to tell what
it does.

## Decision

**Plain TypeScript, Vite, no framework, no runtime dependencies.** The built output is static files
that `hgps serve --web` hosts, so the whole graphical host is one binary and one folder.

- **TypeScript, strict.** The API has a published shape ([docs/server-api.md](../server-api.md))
  and the types in `src/api/types.ts` are that shape written down. A response field renamed on the
  server becomes a compile error here, which is the cheapest test this project can have of a
  contract between two languages.
- **Vite.** Dev server with hot reload, one build command, no configuration to speak of. It is the
  default for a reason and there is nothing here to argue about.
- **No framework.** Four screens, one of which is a form and one of which is a chart. The DOM API
  is enough, and a framework would add a build-time dependency tree an order of magnitude larger
  than everything else this repository depends on put together ([ADR 0014](0014-minimal-dependency-set.md)).
- **No charting library.** The charts are line charts of a few dozen points, drawn as inline SVG in
  about a hundred lines. Chart.js is 200 kB to draw a polyline, and its defaults — animated
  transitions, tooltips that round — are wrong for a tool whose readers want the number.
- **No CSS framework.** One stylesheet, custom properties for the palette, and the browser's own
  form controls. A research tool that looks like a research tool is easier to trust than one that
  looks like a product.

**Preact was considered and rejected**, and the ruling explicitly allowed it "if it earns its
place". It does not, on this screen count. What a framework buys is reconciliation — not
re-rendering what did not change — and the only screen here that updates continuously is the run
launcher, whose live region is a progress bar and a line of text. Two `textContent` assignments do
not need a virtual DOM. It would also make every future contributor learn a framework to change a
label.

**The SSE client is the one place with real logic**, and it gets its own module and its own tests:
reconnection, replay (the server re-sends from the start of its buffer, so the client must be
idempotent about events it has already seen), and the state machine a Cancel button needs when
cancellation is observed at the end of a simulated year rather than at the call.

## What is tested, and what is not

The GUI is out of scope for equivalence. Its correctness test is the server's byte-identity test —
a run started over HTTP produces the same bytes as one started in process — plus its own unit tests.

Those unit tests cover the parts where a mistake is silent: the schema-to-form walk, the mapping
from a diagnostic's JSON pointer to the field it annotates, the SSE event reducer, and the chart's
scale computation. They do not cover "the button is in the right place", because a test that
asserts a layout is a test that fails when somebody improves the layout.

`npm run typecheck` and `npm test` run in CI. `node_modules` is not vendored; `package.json` and the
lockfile are committed.

## Consequences

- A contributor needs Node to build the frontend and needs nothing to build or run the engine. The
  `--web` directory is optional and the server works without it.
- `scripts/dev.sh` runs the server and the Vite dev server together, with the dev server proxying
  `/api` to the engine — so the frontend is developed against the real API rather than a mock,
  which is the only way the types stay honest.
