# Backlog

What is not done, ranked by value for effort. Every item says what it unblocks and what it costs,
because the point of a ranked backlog is to make the next decision easy rather than to be
complete.

Tags:

- `scope` — deliberately left out ([ADR 0021](decisions/0021-scope-finch-and-hlm-france.md)).
- `correctness` — something could produce a wrong number or refuse a valid input.
- `validation` — makes an existing claim checkable, or checks it harder.
- `platform` — build, CI, packaging.
- `cleanup` — internal, no behaviour change.
- `needs-ruling` — cannot be done here: it needs a decision that belongs to whoever owns the model
  or the data, and guessing would put an invented number into somebody else's fitted model.
- `docs`

**This run closed items 2, 10 and 11, and opened 11a.** Names are resolved to indices at the call
site and `KevinHall_FINCH` is faster for it, byte-identically
([docs/performance.md](performance.md)); the GCC entries are required; the lattice detector asks its
question of the numerator. It also did three things nobody had asked for and a ruling did: a second
synthetic configuration with every test that runs one parameterised over both
([ADR 0044](decisions/0044-two-fixture-packs-and-a-parameterised-suite.md)), a browser driving the
frontend end to end ([ADR 0045](decisions/0045-end-to-end-tests-in-a-real-browser.md)), and a
randomised server-lifetime stress test. Between them those three found five defects.

What is left is what it was, minus the performance item: **one modelling question that belongs to
you** (interventions on Kevin Hall models), the validation this project's own documents hedge about,
one correctness item this run found and did not fix (11a), and Windows. The top of the list is a
question rather than work for the second run running.

## Do these first

### 1. Interventions on the Kevin Hall model surface — `needs-ruling`

**Value: high. Effort: none here, and that is the point.** It was item 8 in the previous run's
ranking; it is first now because everything above it is done, and because it is the largest thing
this build refuses that a user could reasonably want.

An intervention selected on the `StaticLinear`/`KevinHall` surface has **no effect at all** in the
baseline, and the run reports success: `Scenario::apply` has exactly one call site in the whole
upstream tree, in the dynamic hierarchical linear model, and neither `static_linear_model.cpp` nor
`kevin_hall_model.cpp` calls it (deviation **B-25**). So `KevinHall_FINCH` with `food_labelling`
active produces output byte-identical to the same run with `simple` active.

This build **refuses** such a configuration at load time rather than running it silently, with a
located issue naming the intervention, how many impacts it declares and the dynamic model file. That
is the right behaviour given the choice between "silently do nothing" and "say so"; it is not the
right behaviour if what upstream wants is for the policy to apply.

**The question that has to be answered elsewhere**: on the Kevin Hall surface, a policy that shifts
a nutrient has to propagate through the energy-balance model, and *where* it is applied changes the
answer — before the balance runs, after it, or to the intake targets. That is a modelling decision
with a fitted model behind it, and guessing would put an invented mechanism into somebody else's
work. Until it is answered, four of the six upstream examples can only be run with `simple`.

See [docs/deviations.md](deviations.md) B-25 and `tests/config/intervention_reach_test.cpp`.

### 2. The weight-category columns are counts and both reductions treat them as means — `correctness`

**Value: medium-high. Effort: low for the code, a reference regeneration for the consequences.**
Found this run while reading the reduction for the lattice detector (closed below).

`normal_weight`, `over_weight`, `obese_weight` and `above_weight` are **head counts** in the result
CSV — the analysis module increments one per person per band, and
`tests/sim/simulation_test.cpp` pins `normal + over + obese == count` for every row. Both reductions
that exist treat them as per-band means:

- the equivalence harness sums `count`, `deaths` and `emigrations` and takes the count-weighted mean
  of everything else, on the stated premise that "everything else is a mean or a proportion within a
  band" — which is false for these four;
- `GET /api/runs/{id}/summary` applies the same rule, and it is what the results screen charts.

So the reduced figure for `normal_weight` on `HLM_France` at (baseline, 2030, male) is **15.3** where
the population figure is about 1,550: the count-weighted average of a per-band count, which is a
number with no meaning. The *shape* of the series still follows the underlying quantity, which is why
nothing looked obviously wrong.

**It does not make any comparison wrong.** Both implementations are reduced identically, so the
equivalence result stands; what is wrong is the label on the number and the chart the server draws.

The fix is four names in two places. What makes it more than that is the consequence: the stored
equivalence references hold *reduced* values, so changing the reduction invalidates all four of them
and they have to be regenerated against the baseline binary — about an hour and a half of machine
time for `HLM_France`, `KevinHall_FINCH` and `HLM_India`'s two. Fixing only the server would make
the two reductions disagree, which is the one thing [docs/server-api.md](server-api.md) says the
shared rule exists to prevent, so the two halves go together.

### 3. A runnable Kevin Hall example, which needs upstream — `needs-ruling`

**Value: high. Effort: none here.** Population impact fraction is implemented
([ADR 0038](decisions/0038-population-impact-fraction.md)) and `KevinHall_PIF` loads completely: config,
both model files, the registry, 69 fraction tables, both scenarios' modules. It then stops in its first
simulated year, on `KevinHall_India`'s defect, because it is the same data — `India.DataFile.csv` and
both weight-quantile files are byte-for-byte identical between the two examples, and both put `Weight`'s
lower bound at 3.319358 kg, above what the curve produces for the lightest newborns. **The baseline dies
in the same place.**

So the whole Kevin Hall + India data family — `KevinHall_India`, `KevinHall_PIF` and all twelve of the
latter's alternatives — cannot be run by either implementation, and no code change here can fix it:
raising the curve or lowering the bound would both be inventing a number for somebody else's fitted
model. This is item 5's question asked again from a second direction, and answering it would unblock
two examples rather than one, plus the only PIF equivalence comparison there could be.

### 4. Individual-level tracking output — `scope`

**Value: medium. Effort: low-medium.** `output.individual_tracking` is parsed, validated and
carried in `config::IndividualTracking`, and nothing writes the file. The baseline's
`individual_id_tracking_writer.cpp` is small. Worth noting: this is the feature that makes the
baseline's person IDs matter, and it is why this implementation kept the monotonic lifetime-unique
counter rather than the earlier rewrite's slot reuse
([ADR 0017](decisions/0017-person-ids-monotonic-and-free-slots.md)).

## Worth doing soon

### 5. `HLM_India` at the cohort it ships — `validation`

**Value: medium. Effort: medium, and all of it is machine time.** `HLM_India` is now compared against
the baseline at 20 and 60 seeds — but at `size_fraction` 1e-5, which is **12,406 people against the
1,240,613 it ships** ([docs/equivalence.md](equivalence.md)). That was the only way to have the
comparison at all: at full scale one seed is about 40 minutes in this build and over an hour in the
baseline, so twenty seeds of both is about a day and sixty is three.

What the reduction cannot tell you is anything that only appears at scale. Two candidates are
specific rather than hypothetical: the emptying-band exclusion is **1,641 bands at 12,406 people**
against 785 on `HLM_France`, and at 1.24 million almost none of those bands would empty at all — so
the full-scale comparison would exclude far less and test more; and a rare disease that gives 0, 1 or
2 cases at this cohort size gives hundreds at the shipped one, which moves several of the comparisons
off the lattice that item 11 is about.

So this is worth doing once, on a machine that can be left alone for a few days, and the stored
reference would be large — which is item 8.

### 6. A second country for the FINCH surface — `validation`, and it needs upstream

**Value: high. Effort: unknown, and not all of it is here.** The FINCH equivalence evidence is one
pack, one country. The obvious second is `KevinHall_India`, which uses the same `StaticLinear` and
`KevinHall` code with a different factor set, different food groups, the JSON shape of the static
model and the income trend — the parts of the surface FINCH does not reach.

It cannot be run. Both implementations stop in its first simulated year because the pack's
configured lower bound on `Weight`, 3.319358 kg, is above what its own weight quantile curve
produces for the lightest newborns; the baseline dies on an uncaught exception and this build
reports it as a located internal error. [docs/examples.md](examples.md) has both messages.

Nothing here can fix that: raising the curve or lowering the bound would be inventing a number for
somebody else's fitted model. What this item needs is upstream to say which of the two is wrong.
Until then the FINCH surface has one country, and that is the largest single gap in the validation.

### 7. A fallback donor for immigration into an empty band — `correctness`

**Value: low-medium. Effort: low.** When an age-sex band is empty there is nobody to clone an
immigrant from, so both implementations skip it and the cohort falls short of the demographic
projection by a few people. It is recorded as **B-21**, reported per year as
`ImmigrationShortfallPeople` and `ImmigrationShortfallBands`, and it is the mechanism behind the
equivalence harness's one exclusion rule.

Keeping the baseline's rule was the right call for a comparison, but the shortfall is still a run
that misses its own target. The baseline has a nearest-age search in its demographic module
(`demographic.cpp:662`) that it does not use for this. Using it would make the target always
achievable, at the cost of nudging the age distribution. It changes results, so it needs a
deviation entry, an ADR and a re-run of both references.

### 8. More seeds, and a smaller stored reference — `validation`

**Value: low-medium. Effort: low.** Both references are 20 seeds, confirmed at 60 and then
discarded. Keeping the 60-seed references would be about 12 MB gzipped. The alternative is to
store the reduction rather than the raw results — the harness reduces to (scenario, year, sex,
variable) before it compares anything, and the reduction is two orders of magnitude smaller — at
the cost of not being able to change the reduction without a re-run.

### 9. Windows — `platform`

**Value: unknown. Effort: medium, and better understood than it was.** Not targeted
([ADR 0013](decisions/0013-platforms-linux-and-macos.md)). The code avoids PSTL and
`<syncstream>` and has one `__APPLE__` branch, so the likely work is the executable-path lookup,
`posix_spawn` (used for `curl` and `unzip`), `gmtime_r`, and the file-system assumptions in the
cache. `CMakeLists.txt` refuses a Windows build outright, so the first step is deciding to stop
refusing.

**CI changed the estimate, in both directions.** The tree now builds and passes on Linux with two
compiler families as well as on macOS, which removes the "it has only ever been one toolchain"
uncertainty that made this unquotable. But the five defects CI found are exactly what a *third*
platform would find more of, and three of them were invisible to the development compiler: a type
whose width differs, a warning one compiler has and another does not, and headers one standard
library supplies transitively. MSVC is a third standard library and a fourth warning set, and the
honest expectation is a comparable list. [docs/build-notes.md](build-notes.md) has what the second
platform cost, which is the only evidence available for what a third would.

Still only worth doing if someone needs it.

## Smaller things

### 10. Run the sanitizer presets' tests in parallel — `platform`

**Value: medium. Effort: low, and the risk is what makes it an item rather than a one-liner.** The
second fixture pack doubled every test that runs a configuration, and the cost lands almost entirely
on ThreadSanitizer: of 2,272 seconds of local TSan test time, 2,219 are the 184 `Packs/` tests, and
the `macos · appleclang · tsan` CI job went from **48m28s to about seventy minutes**. That job is the
workflow's long pole, it is the one every push supersedes, and it is now most of the reason a full CI
run takes over an hour.

`ctest -j` is the obvious answer: these tests are almost all single-threaded and run serially today,
on runners with several cores. What makes it an item rather than a one-liner is that some of them are
*about* threads — the byte-identity-at-N-threads tests spawn workers, the server tests bind sockets,
and the stress test runs several clients — so the right degree of parallelism has to be found rather
than assumed, and under a sanitizer the memory cost multiplies too. Doing it at the end of a run
leaves no time to find out what it breaks.

### 11. `DataSeries` is still keyed by channel name — `cleanup`

**Value: low-medium. Effort: low-medium.** The other half of the old item 2, and the half that item
always called smaller. A `KevinHall_FINCH` profile taken after this run's change still has
`_platform_memcmp` as its largest single entry, and what is left of it is the analysis module and the
result writer looking a channel up by its `std::string` name — `DataSeries::at(Gender, Income,
const std::string &)` and `Map2d<Gender, Identifier, …>::contains` — rather than anything per person
per year. The same treatment applies: resolve the channel name to a column index when the series is
built.

### 12. A schema for the model definition files — `docs`

**Value: medium. Effort: low.** `schemas/v2/` covers the config. The static and dynamic model
files have no published schema, which is why their member names were wrong for a week in the
previous run and why this run spent an afternoon on the FINCH pack's headerless two-column CSVs
and its R row-index columns. The shapes are documented only in `src/config/models/*.cpp` and in
the three loader test files. Write them, and extend `schema_agreement_test.cpp` to cover them the
way it covers the config.

### 13. Sector, and `demographic_models` — `scope`

**Value: low. Effort: low.** `person.sector` (urban/rural) is assigned nowhere; the channel
appears if the mapping declares the factor. `modelling.demographic_models` is carried through as
opaque JSON, deliberately — its shape belongs to the model family that reads it — and no model
family reads it yet.

### 14. The fixture pack's top-age artefact — `validation`

**Value: low. Effort: low.** The synthetic pack's population table stops at the same age as the
config's `age_range`, so anyone reaching the top age leaves the cohort and the pack's simulated
death rate runs above what its mortality table implies. Recorded in the pack's own `SYNTHETIC.md`.
Extending the pack's age range by a few years above the configured one would remove the artefact;
nothing depends on it, because no test reads the pack's death rates as a check on anything.

### 15. Report four things upstream — `docs`

**Value: low here, high upstream. Effort: low.** Four findings belong to the people who own the
data and the baseline, and telling them is not done:

1. **`KevinHall_India` cannot be run by its own baseline** — its `Weight` lower bound is above what
   its weight quantile curve produces (item 3), and its `new_config.json` contradicts itself
   between the deprecated root `trend_type` and `project_requirements.trend.type`, which the
   baseline's own validator refuses.
2. **An intervention scenario is inert on the FINCH model surface** (item 7): `Scenario::apply` has
   one call site and neither Kevin Hall model calls it, so a config selecting `food_labelling` on
   that surface runs with no error and no effect.
3. **`KevinHall_FINCH`'s legacy `static_model.json` names two files the pack does not contain**
   (audit D-02, [ADR 0030](decisions/0030-policy-scenario-selection-for-the-broken-finch-example.md)).
4. **The baseline crashes on `KevinHall_FINCH` about one run in forty-five** — 4 of 180 measured
   runs, three on `SIGTRAP` and one on `SIGABRT`, each succeeding on the retry with the same
   binary, config and seed. That is audit findings B-01 and B-02 (concurrent scenarios, a
   lazily-populated repository) showing up as a crash rather than as a reordering. The equivalence
   harness retries up to three times and prints every retry, so it is visible here rather than
   smoothed away.

Each is reproducible from this repository with one command, which is most of the work of a good
bug report.

## Closed this run

Kept as a record of what the numbers above used to be, because three documents and a test reference
them by number.

| Was | | |
|---|---|---|
| **2** | Resolve names to indices at the call site | done; [docs/performance.md](performance.md) has the A/B, byte-identical on all three runnable examples. What is left of it is item 11 above |
| **10** | Make the GCC entries required | done; `experimental: true` is gone from both entries |
| **11** | The lattice detector should classify on the numerator | done, with no threshold moved; all four stored references re-scored and within tolerance ([docs/equivalence.md](equivalence.md)) |

## Explicitly not planned

- **Reintroducing the event bus.** Fifteen baseline tests, no purpose here: the runner hands each
  row to a sink, which is the one place output is written
  ([ADR 0020](decisions/0020-output-single-owner-defined-row-order.md)).
- **Reintroducing `SyncChannel`.** Nine baseline tests, and the mechanism behind audit B-01
  ([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)).
- **A lazy, cached data repository.** Three baseline tests, and the mechanism behind audit B-02.
- **Bit-exact reproduction of the baseline.** Ruled out up front
  ([ADR 0006](decisions/0006-validation-strategy.md)); it would have foreclosed most of
  [docs/deviations.md](deviations.md).
- **Cross-platform bit-reproducibility.** Not a requirement
  ([ADR 0008](decisions/0008-determinism-contract-enforced-by-types.md)). The determinism
  contract is per-binary, and the cheap wins that happen to help across platforms — no unordered
  CDF, pinned floating-point contraction, no `std::generate_canonical` — are in it anyway.
- **The `dummy` model family.** `Dummy_disease_test` selects it. It is a test double that upstream
  ships as an example; this implementation has its own fixtures for that job.
