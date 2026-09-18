# Summary of the fifth build run

What was built, what is proven, and where it stops. Written at the end of the run it describes.
Earlier runs' summaries are in the history of this file: the first covered the HLM surface, the
second the FINCH one, the third the library split and the index-keyed store, the fourth `HLM_India`
and the first CI workflow.

## The short version

A deterministic C++20 reimplementation of the Health-GPS microsimulation, with the whole upstream
model surface implemented, three examples compared against the baseline, and — since this run —
**three ways to use it**: as a library, from a command line, and from a browser.

Three things happened, in the order the run found them.

- **CI ran for the first time and every job failed.** The previous summary said "something in it is
  probably wrong", and it was: **five causes**, four of them defects in the tree rather than in the
  workflow, and **three of those were portability defects invisible to the only compiler that had
  ever built this project**. All fixed, one commit per cause. **GCC on Linux is now green**, which
  it had never been, and neither "CI has never run" nor "GCC has never built this tree" is true any
  more.
- **Every deliberate deviation is now switchable**, so its effect is measured rather than argued.
  The previous run attributed 28 out-of-tolerance comparisons to deviation B-24 by reading the shape
  of a curve; that attribution was right, and it was an argument. It is now a measurement taken by
  running the same binary twice on the same seeds — and it agrees.
- **A graphical host exists**: a local JSON server over the library, and a single-page app over that.
  It is what the library split, the event stream and the run manifest were built for, and it is the
  first thing to test whether that API is the right shape.

| | |
|---|---:|
| Tests, C++ | **738** in 93 suites — 741 CTest entries — passing under release, debug, ASan+UBSan and TSan |
| Tests, the equivalence harness's own | **39** (was 30) |
| Tests, the frontend | **48** |
| Comparisons against the baseline this run | **131,061**, **0** out of tolerance |
| Source | `src/` 153 files; `tests/` 67 files; 43,576 lines of C++ between them; `web/src/` 13 files, 2,357 lines |
| Documents | 12, plus **43 ADRs** |
| CI | **13 jobs**, green on every matrix entry |

## The seven tasks, and how each ended

| | Task | Outcome |
|---:|---|---|
| 1 | Orientation, cleanup, CI triage | **Done.** No stale processes of this project's were running. CI's five failure causes are in [docs/build-notes.md](build-notes.md), each fixed in its own commit. |
| 2 | A compatibility flag for B-24, the harness's deviation-impact section, reruns, an ADR | **Done**, and the reruns say more than they were asked to. [ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md). |
| 3 | Review the existing exclusions against that rule | **Done.** None converted, with the reasoning written down — and the review found a hole in one of them that was worth more than a conversion. |
| 4 | The local server: design, implementation, tests | **Done.** [docs/server-api.md](server-api.md), [ADR 0042](decisions/0042-a-local-server-in-the-same-binary.md), **45 tests** — 26 over a real socket, 3 for byte identity against the CLI, 10 for the reduction, 6 for the command line. |
| 5 | The frontend: four screens | **Done**, all four. [ADR 0043](decisions/0043-a-plain-typescript-frontend.md). 27 kB of JavaScript, 48 tests. |
| 6 | A CI job for the frontend | **Done**, and green. |
| 7 | Docs, ADRs, backlog, this file | **Done.** |

## What CI found, and what it says about the four clean runs before it

The workflow was written in the previous run and validated by reading, because neither `act` nor
Docker is installed here. It then ran, and **every job failed**. Five causes:

| # | What failed | Why it was invisible locally |
|---|---|---|
| 1 | `std::mt19937::result_type` narrowed implicitly | it is `std::uint_fast32_t`: **32 bits on libc++, 64 on libstdc++**. An exact, value-preserving narrowing — and an implicit one, which `-Wconversion` rejects on Linux and has no reason to mention on macOS |
| 2 | a constructor parameter shadowing a member | **GCC's `-Wshadow` covers constructor parameters and clang's does not** |
| 3 | nineteen missing standard headers | libc++ supplies `<cstdint>` through `<source_location>` and libstdc++ does not |
| 4 | `-Wmissing-field-initializers` on 213 designated initialisers | clang 19 has a narrow warning for this and the tree turns it off; an older clang and every GCC fold it into `-Wextra`'s broad one |
| 5 | a stored equivalence reference could not be found | its key was the hash of the derived config taken **after** absolutising the paths, so it carried `/Users/jude/work/hpgs/…` and could match only on the machine that wrote it |

Three of these — 1, 2 and 3 — are one shape: **a portability defect the development compiler cannot
see.** So each was fixed by finding every instance rather than the one CI stopped on. Cause 2 was
scanned for with clang's `-Wshadow-all` (two sites, both the ones GCC named). Cause 3 was scanned for
by following each file's project-local includes transitively — 245 candidates before following them,
**19** after, every one real.

Cause 4 took two commits, and the second is the lesson: the first added a fallback probed with
`check_cxx_compiler_flag(-Wno-missing-designated-field-initializers …)`, and CI failed again in the
same place, because **GCC accepts any `-Wno-<anything>` it has never heard of** and answers "yes".
Probing the positive spelling is answered honestly by everybody.

Cause 5 is the one worth dwelling on. **Nothing but this machine had ever run the equivalence
harness**, so nothing had ever noticed that a checked-in reference could only be found here. Four
runs of a document claiming the references made the comparison reproducible, and they did not.

**What this says about the four green runs before it**: a suite that passes on one compiler, one
standard library and one operating system is evidence about that combination and no more. The
project had 668 tests and four presets and was green on all of them while carrying three portability
defects and a reproducibility claim that was false everywhere but here.

### CI, per matrix entry

Thirteen jobs, and the previous run's two sceptical bullets — "CI has never run", "GCC has never
built this tree" — are both retired.

| Job | |
|---|---|
| `linux · clang · release` / `debug` / `asan-ubsan` / `tsan` | green |
| `linux · gcc · release` / `debug` | **green**, and never had been |
| `macos · appleclang · release` / `debug` / `asan-ubsan` / `tsan` | green |
| `equivalence · HLM_France · 20 seeds` | green — and had never reached the comparison before, because of cause 5 |
| `equivalence · KevinHall_FINCH · 20 seeds` | green, same |
| `web · typecheck, test, build` | green (new this run) |

The GCC entries still carry `experimental: true`, which makes them `continue-on-error`. That flag
was there because nothing knew what GCC would say. Now something does, and removing it is a
one-line change rather than an unknown — [docs/backlog.md](backlog.md).

## What a deviation is worth, measured

The rule ([ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md)): every deliberate
deviation that changes outputs gets a named compatibility flag, named after its ID in
[docs/deviations.md](deviations.md). With the flag on the engine reproduces the baseline's behaviour
exactly. Flags are off by default, reachable from the config, the API and the CLI, and **recorded in
every run manifest**.

**The equivalence harness compares with the flags on**, so a comparison tests everything except the
deliberate differences and an out-of-tolerance cell means something is wrong. It then runs the same
build once more with the flags off and reports the difference as a **deviation impact** section —
reported, never graded, because a deviation has no right size.

| Example | Intervention | Comparisons | Out of tolerance | Was |
|---|---|---:|---:|---:|
| `HLM_France` | `simple` | 31,468 | **0** | 0 |
| `HLM_France` | `food_labelling` | 31,552 | **0** | 1 |
| `HLM_India` *(reduced cohort)* | `food_labelling` | 68,041 | **0** | 3 |

**131,061 comparisons this run, zero out of tolerance.** The previous run's isolated `HLM_France`
residual and India's three are gone — they were B-24, and with the flag on B-24 is not there.

And the measurement, mean BMI of males in the intervention scenario, this build minus the
baseline-compatible one, over 20 seeds:

| | Largest | When | Relative |
|---|---:|---:|---:|
| `HLM_France` | **+0.0531** | 2037 | **+0.209%** |
| `HLM_India` *(reduced)* | **+0.0313** | 2050 | **+0.160%** |

This project has been quoting "about +0.2% of mean BMI" for B-24 for a run and a half, inferred from
which out-of-tolerance cells looked like it. The direct measurement agrees. That is the good case,
and the reason to build the mechanism is the case where it would not have.

**The deviation reaches much further than mean BMI.** On `HLM_India`, **194 series differ** and
12,532 agree to the printed precision — including years of life lost, disability-adjusted life
years, head counts and the prevalence and incidence of eleven diseases. A BMI that is wrong changes
incidence, which changes mortality, which changes the cohort. Nothing in this repository said that
before, because nothing could.

## The exclusions, reviewed

Three things are left out of the harness's reduction, and the ruling asked whether any would be
better expressed as a compatibility flag. **None is converted**, on one criterion stated once:

> A flag is right when **both** implementations compute a meaningful number and they differ on
> purpose — then the difference is the finding, and excluding it throws the measurement away. An
> exclusion is right when **one side has nothing to compare**.

The emptying age bands (B-21) are a rule this build *keeps*, so there is no behaviour to switch and
an empty band has no mean. `std_income` (B-22) is the close call: the baseline's column is a
placeholder, so a flag would make the comparison zero against zero, which is not a stronger test.
The first simulated year is not a deviation at all.

**The review found something better than a conversion.** The `std_income` rule has two halves —
"the baseline never fills it" and "we do" — and it checked only the first. Two identically zero
series would have printed *the baseline does not compute it* and skipped, word for word what it
prints when everything is fine. **A regression in the one variable the rule covers was invisible, in
the rule written to cover it.** It is reported now, with three tests.

## The graphical host

`hgps serve` hosts the engine over HTTP on localhost, and serves the built frontend as static files,
so the whole thing is **one binary and one folder**. [docs/server-api.md](server-api.md) is the
contract.

Two constraints are **enforced rather than hidden**, both of them the engine's rather than this
layer's. A second run is refused with `409` instead of queued, because two `execute` calls must not
overlap in one process and a queue would turn a stated constraint into an unstated wait. And cancel
returns `202`, not `200`, because the engine stops at the end of the year it is in and the response
cannot honestly say the run has stopped.

There is **no authentication**, and `--host` refuses anything but loopback before opening the socket.
That refusal is what makes the absence of authentication a decision rather than an omission: a
configuration names files to read and a folder to write.

There is **no database**. `GET /api/runs` reads the runs directory and parses each manifest, which is
why the history survives a restart and why a runs directory copied from another machine lists
correctly. That is the run manifest justifying itself — the feature that needed it did not exist when
it was designed.

The frontend is **plain TypeScript, 27 kB**, no framework and no charting library
([ADR 0043](decisions/0043-a-plain-typescript-frontend.md)). Preact was allowed if it earned its
place and did not: four screens, and the only one that updates continuously has a progress bar and a
line of text for a live region.

**Its correctness test is byte identity**: a run started over HTTP produces the same result CSV, byte
for byte, as the same configuration run in process. If routing a run through HTTP broke that, the
server would not be a host of this engine but a fork of it.

## What the process found that reading would not have

1. **Three portability defects, and a reproducibility claim that was false.** Above. The tree had
   been green on four presets for four runs while carrying all four.

2. **GCC lies about `-Wno-`.** A `check_cxx_compiler_flag` probe of a `-Wno-<anything>` flag is
   answered "yes" by a compiler that has never heard of it. The first fix for CI cause 4 was correct
   in every respect except that its feature test could not fail.

3. **Driving the page found three more defects the tests had not.** The server **exited the moment stdin
   closed**, so anything not started from a terminal died before serving a request. A run that was
   accepted and then failed to *build* stayed in the list as `starting` for ever **and held the
   one-run-at-a-time slot**, so nothing else could start. And a failed start left the Start button
   disabled, because `starting` was cleared in a `finally` that ran after the render. None of the 44
   server tests would have caught the first; the second now has a test that would.

4. **Pointing the server at the real examples, rather than the fixture, found another.**
   `GET /api/runs/{id}` reported `"manifest": null` for every one of them, and the history would
   have lost them all on a restart: the manifest is named after `output.file_name`, which the
   *configuration* decides, and the name the synthetic fixture happens to produce was hard-coded in
   three places. **All 44 server tests passed**, because all 44 used the fixture. It is the same
   shape as finding 7 below — the second time in this run that an unrepresentative fixture hid
   something — and the more uncomfortable of the two, because there the test failed and here every
   test passed.

5. **Three more server defects came out of reading it back, not from running it.** All three are
   lifetime or concurrency faults that no endpoint test would provoke: two clients validating the
   *same* document picked the same scratch filename, so the first to finish deleted the file the
   second was still loading; a server started and then simply dropped called `std::terminate`,
   because a joinable `std::thread` member is destroyed before the `stop()` that would have joined
   it; and stopping the server left a run thread writing while `main` returned, so Ctrl-C during a
   run could truncate a result file. Each has a test now, and the last one needed the thread
   ownership underneath it to be made coherent first — `finish` was detaching the run's thread from
   inside that same thread, so a join from anywhere else returned while the run was still
   unwinding. **The tests were written after the fixes and would not have found them**, which is
   worth saying rather than implying otherwise.

6. **A CSV row ending in a comma was silently dropped.** `std::getline(stream, field, ',')` stops at
   the last separator, so the row's field count disagreed with the header and the whole row went. A
   missing year in a chart, not a wrong number — which is the worse shape for a parsing bug to take.
   Found by a test written for something else.

7. **The compat flag's first end-to-end test failed, correctly.** The synthetic fixture's policy ran
   for two years and the defect needs three: one to fail a draw in, one to pass in, and one to be
   wrongly offered it again in. The test was wrong about the fixture, not the code — and a test that
   had passed for the wrong reason would have been worse than a failing one.

8. **Having a second implementor changed what the API's gaps mean.** Two of the seven
   [docs/api.md](api.md) lists are now *felt* rather than predicted: the summary endpoint parses a
   CSV the engine wrote seconds earlier in the same process, and cancel is a `202` plus an event.
   Neither is closed, deliberately — the cost is concrete now, which is a better basis for the design
   than guessing was.

## What a reader should still be sceptical about

- **The frontend has no end-to-end test.** Its unit tests cover the four places a mistake is silent,
  and its correctness rests on the server's byte-identity test. Three defects were found by a person
  opening it in a browser, three more — lifetime and concurrency faults in the server — by reading
  the code back afterwards, and one more by pointing the server at the real examples instead of the
  synthetic fixture. **Seven defects, none found by a test.** The tests that now cover them were
  written afterwards, and the last one is the most uncomfortable: 44 passing server tests all used a
  fixture whose output happens to be named the one way the code assumed.
- **India was compared at a hundredth of its cohort**, 12,406 people rather than 1,240,613. Nothing
  in the India result is evidence about the example as shipped.
- **Population impact fraction has never met the baseline.** Only the synthetic pack exercises it end
  to end; the one example that uses it cannot run in either implementation.
- **The FINCH surface is still one country, one data pack.**
- **Four of the six upstream examples can only be compared with `simple` active**, because an
  intervention on the Kevin Hall surface is a no-op upstream (B-25) and this build refuses the
  configuration rather than running it silently. Changing that needs a modelling decision this
  repository cannot make — [docs/backlog.md](backlog.md) item 1.
- **The comparison's floor.** The baseline writes six significant digits, so no comparison is tighter
  than about 10⁻⁵ relative.
- **The harness decides the headline result, and it has been wrong four times** — a normal-theory
  allowance on a point mass, the same on a lattice-valued median, a lattice detector that cannot see
  a lattice in a numerator, and now a rule that checked half of its own justification. It has 39
  tests, which is better than nothing and is not the same as being right.
- **macOS and Apple clang for every measurement in this repository.** Linux and GCC now *build* and
  *test* it; no number in [docs/performance.md](performance.md) was taken there.
- **The synthetic fixture pack is invented.** Its own `SYNTHETIC.md` says so.

## The recommended next run

**Answer the Kevin Hall intervention question, or decide not to** —
[docs/backlog.md](backlog.md) item 1. It is first because everything above it is done, and because
it is the largest thing this build refuses that a user could reasonably want: four of the six
upstream examples can only be run with a no-op policy. It is `needs-ruling` rather than work: on
that surface a policy shifting a nutrient has to propagate through the energy-balance model, and
*where* it is applied changes the answer. That belongs to whoever owns the fitted model.

If that answer is not available, the next run is **item 2**, resolving names to indices at the call
site — about 31% of the FINCH profile, and the largest remaining performance item, with the check
that matters already established: byte-for-byte comparison of the result files before and after.

Two smaller things are nearly free and would remove hedges from this document: **remove
`experimental: true` from the GCC matrix entries**, which is now a one-line change rather than an
unknown, and **fix the lattice detector** to classify on the numerator (item 10), which would either
explain or remove the six India residuals the previous run recorded.

[docs/backlog.md](backlog.md) has the rest, ranked, with what each costs.
