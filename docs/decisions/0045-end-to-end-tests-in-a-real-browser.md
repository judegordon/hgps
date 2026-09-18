# 0045 — End-to-end tests in a real browser, with Playwright

## Status

Accepted, 2026-09-18.

## Context

The frontend had 48 unit tests and no end-to-end test. The previous run's summary said so under
"what a reader should still be sceptical about", and the reason it was worth saying is the count
underneath it: **nine defects in that run's new code, none of them found by a test.** Three were
found by a person opening the page in a browser — a server that died when stdin closed, a run that
stuck in `starting` for ever and held the one-run slot, a Start button left disabled after a failed
start. The tests that now cover them were written afterwards.

The unit tests are not the wrong tests. `schema-form.ts`, `events.ts`, `run-progress.ts` and
`chart.ts` are where a mistake is silent, and they are covered as pure functions. What they cannot
cover is the wiring: whether the pointer a diagnostic carries is the pointer the DOM used, whether
the server's events reach the progress bar at all, whether a download link built from a run's file
list yields the bytes the engine wrote.

## Decision

**Playwright, driving Chromium against the built frontend and a real `hgps serve`, over the two
synthetic fixture packs. One spec per screen, plus one for the journey across them. Its own CI job.**

Nothing is mocked. `scripts/e2e-server.sh` generates the fixture packs, lays them out as the server
expects (`<root>/<id>/config.json` with one shared `<root>/data`), and starts the binary with
`--web web/dist`. Playwright starts and stops it through its `webServer` option. A run of a
synthetic pack takes about a fifth of a second, so a test can start a run and wait for it to finish:
**19 tests in about 7 seconds**, which is what keeps it a thing that runs rather than a thing that
is run.

**Why Playwright rather than the alternatives.** Cypress would do the same job; it needs its own
runner and its own assertion style, and its download handling is the weakest part of it, which is
exactly what one of these tests is about. WebDriver through Selenium means a driver binary to pin
per browser. Puppeteer is the same engine without the test runner, retry semantics or trace
viewer. Playwright installs one browser (`npx playwright install chromium`), starts the server
itself, and has first-class handling of downloads and server-sent events. The deciding factor is
that it is one devDependency and one command in CI.

**Chromium only.** The frontend uses `fetch`, `<details>`, `<svg>` and CSS the built target already
constrains; a second engine would double a job that builds a C++ project to find the same defects.
Cross-browser is not a claim this project makes and adding it would be one.

**Serial, one worker.** One run at a time is the server's contract, not an accident
([docs/server-api.md](../server-api.md)) — parallel tests would produce `409`s that say nothing
about the frontend.

**Both packs.** Every screen test that runs a configuration runs it against each of the two
synthetic packs ([ADR 0044](0044-two-fixture-packs-and-a-parameterised-suite.md)). The download
test uses the second one deliberately: its output is not called `result.csv` and is not called the
same thing twice, so a link built from a remembered name fails there and nowhere else.

## What it found

**One defect, on the first run of the suite.** Pressing **Start** left the previous run on screen —
its id, its `completed` state, and its "See the results" button, which pointed at the run before —
for as long as the `POST /api/runs` took. The screen was saying something untrue about the button
that had just been pressed. `start()` now clears the previous run and its stream before rendering.

It is a small defect and it is the honest kind: no unit test could have seen it, because there is no
unit in it. It was found the way the previous run's three were found, except that this time
something other than a person was looking.

Two things the suite reported that were **tests being wrong, not code**, and both are worth writing
down because they are about this app's shape rather than about Playwright:

- **All four screens are in the DOM at once**, hidden rather than unmounted, because each keeps its
  state between visits (`web/src/main.ts`). A bare `p.note` locator therefore matches notes on
  screens nobody is looking at. Every locator in these tests goes through `panel(page)`, which is
  `div.panel:not([hidden])`.
- **A form section is a closed `<details>`** unless something in it is wrong, so a field has to be
  revealed before it can be typed into. `field(page, id)` opens the section that contains it.

## Consequences

- **A fourteenth CI job**, which builds the engine and the frontend and then runs a browser. It is
  the second most expensive job after the sanitizers, and it is the only one that tests the product
  as a user meets it.
- **A browser download in CI.** `npx playwright install --with-deps chromium` is about 170 MB and a
  minute. It is cached by `setup-node` only in part — the browser itself is not — and that is the
  price of the job.
- **`scripts/check.sh` now runs the frontend too**: type-check, unit tests, build, then the
  end-to-end suite. Until this run, `check.sh` verified nothing in `web/` at all, which meant "green
  at every commit" was a claim about the C++ only.
- **19 end-to-end tests is not a claim of coverage.** They cover each screen's principal job and the
  hand-offs between them. They do not cover the configuration editor's every control type, the
  chart's rendering at awkward scales, or any browser but one.
