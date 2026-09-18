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

The previous run's first three items — `StaticLinear`, `KevinHall`, the other five interventions —
are done, and with them the FINCH surface end to end. This run closed the last three: population
impact fraction, a CI workflow, and the factor store's lookup. **The model surface is complete**:
nothing upstream implements is refused here any more.

So the ranking below changes shape. What is left is no longer "finish the model"; it is one large
piece of new work — a graphical host, which is what the library split and the event stream were built
for — and a tail of validation and cleanup. The GUI is first because it is the only item that needs
*design* rather than execution, and because the API gaps it needs (item 2) are cheaper to close before
something depends on the current shape than after.

## Do these first

### 1. A graphical host, and what the API still owes it — `scope`

**Value: high. Effort: large, and it is the first item here that is a project rather than a task.**
[ADR 0032](decisions/0032-library-and-a-thin-cli.md) split the engine from the CLI so that something
other than a terminal could drive it, [ADR 0033](decisions/0033-an-event-stream-the-simulation-cannot-see.md)
gave it an event stream a host can render, and [ADR 0034](decisions/0034-a-run-manifest-beside-the-results.md)
gave every run a manifest. Nothing uses any of it yet. A host would be the first real test of whether
that API is the right shape, and `docs/api.md` is currently a contract with one implementor.

**What a GUI needs from `hgps::engine` that is not there.** This list is the substance of the item;
the widgets are the easy half.

| | What is missing | Why a host needs it | Rough cost |
|---|---|---|---|
| 1 | **Results in memory.** `execute` writes CSVs and `RunSummary` lists the paths. A host that wants to draw a chart has to parse files the engine just wrote | every chart, every table, every live-updating view | medium — and it needs care: the output contract is "one owner per file, rows in a defined order" ([ADR 0020](decisions/0020-output-single-owner-defined-row-order.md)), and an in-memory sink must not become a second, differently ordered output path |
| 2 | **Per-year results in the event stream.** `YearCompleted` carries the year, the elapsed milliseconds and the population size — enough for a progress bar, nothing for a live chart | showing a run as it happens, which is most of why a GUI is better than a CLI | medium, and it is item 1's design decided once rather than twice |
| 3 | **Progress inside a year.** Cancellation and events are both per-year by design, and a year of `HLM_India` is 60 seconds at full scale. A progress bar that moves once a minute is a spinner | any run larger than the reference examples | small for the event; the *cancellation* granularity is deliberate ([docs/api.md](api.md)) and should stay per-year |
| 4 | **A machine-readable `Diagnostic`.** The code and location are structured, but the message is a prose string with numbers in it. A host that wants to underline the offending line in an editor has `Location`; one that wants to offer "fix this for me" has a sentence | showing config errors in a form editor rather than a log pane | small–medium: it needs the arguments carried beside the formatted message |
| 5 | **Config *writing*.** `tools/convert-config` writes config v2 and the loader reads it; there is no supported way for a host to modify a configuration and save it. A GUI is mostly a config editor | the whole editing half of a GUI | medium, and it wants the schema to drive it ([item 11](#11-a-schema-for-the-model-definition-files--docs)) |
| 6 | **Enumerating what a data pack offers.** `Run::description()` answers what *this* run will do. A host building a config needs the other direction: which countries, diseases and risk factors the store has, before a run exists | every dropdown in the editor | small: the index is already parsed and validated |
| 7 | **Cancellation that is observable.** `CancellationToken::cancel()` returns immediately and the run stops at the end of its current year. A host has no way to ask "has it noticed yet?" without waiting for `RunCompleted` | a Cancel button that can grey itself out honestly | small |
| 8 | **A version on the API.** [docs/api.md](api.md) says there is none, which is correct while nothing depends on it. A GUI is the thing that starts depending on it | not breaking the host on every engine change | small, and it is a decision rather than code |

Items 1 and 2 are one design; 3, 6 and 7 are small and independent; 4 and 5 are the ones that decide
whether the GUI can be a *config editor* or only a *run viewer*, which is the real scope question and
belongs to whoever wants the GUI rather than here.

**What it does not need.** Threading (the engine is process-wide single-run by design and says so),
determinism work (byte-identical at any thread count already), or a new output format.

### 2. Make the GCC build a required check — `platform`

**Value: medium-high. Effort: unknown until it is run once.** `.github/workflows/ci.yml` builds and
tests four presets on ubuntu-latest and macos-latest, runs the harness's own tests, and runs the
equivalence comparison against both checked-in references — so the thing this item used to ask for
exists. What is left is the compiler.

Every build this project has ever done is clang. The warning set is `-Werror` with `-Wconversion`,
`-Wsign-conversion`, `-Wold-style-cast` and `-Wdouble-promotion`, and a second compiler *family* has
never seen it. Building with a newer clang than the development one found two real defects and one
style disagreement ([docs/build-notes.md](build-notes.md)), which is a fair guide to what GCC will
find.

So the two GCC entries in the build matrix carry `experimental: true`, which makes them
`continue-on-error`: the information appears on every run without a red tick in a commit that cannot
act on it. Promoting them means deleting that flag, reading what GCC says and fixing it, which cannot
be estimated before seeing it — hence this item rather than a guess.

**Nothing in this repository has ever been built by GCC, or on Linux, or by CI.** The workflow was
written this run and validated by reading it against `scripts/check.sh` step by step, because neither
`act` nor Docker is installed on the development host ([docs/build-notes.md](build-notes.md)). The
first real run of it is also the first evidence that it works, and the honest expectation is that
something in it is wrong.

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
model. This is item 6's question asked again from a second direction, and answering it would unblock
two examples rather than one, plus the only PIF equivalence comparison there could be.

### 4. Resolve names to indices at the call site — `cleanup`

**Value: medium. Effort: medium, and it touches the model loaders.**
[ADR 0037](decisions/0037-index-keyed-risk-factor-store.md) made `Person::risk_factors` index-keyed
and [ADR 0040](decisions/0040-a-bounded-search-for-the-long-vectors.md) fixed the lookup that left
behind — `KevinHall_FINCH` is 1.54× and then a further 1.44× faster, and the output is byte-identical
across both. **The first half of this item is therefore done**; what is left is the half the profile
said was bigger.

**About 31% of the FINCH profile is names being resolved at the call site.** The store no longer
compares strings; its callers still hand it a `core::Identifier`, which costs a hash probe, and some
of them *construct* one per person per year — which is what `Identifier::validate_identifier` and
`chars::is_alnum` at 8.3% of a profile mean. The fix is ADR 0037's idea one level up: the linear
model's coefficient list and the Kevin Hall model's nutrient names hold **resolved indices**, so no
name reaches the hot loop.

`DataSeries` is still keyed by channel name and is the smaller half again: the analysis module and
the result writer are well below the per-person work in the profile.

Whatever is done here, the check that matters is the one ADR 0040 used and that caught a defect in
the first attempt at ADR 0037 within minutes: run both examples before and after and compare the
result files **byte for byte**. A statistical comparison over twenty seeds calls a last-bit
difference agreement.

### 5. Individual-level tracking output — `scope`

**Value: medium. Effort: low-medium.** `output.individual_tracking` is parsed, validated and
carried in `config::IndividualTracking`, and nothing writes the file. The baseline's
`individual_id_tracking_writer.cpp` is small. Worth noting: this is the feature that makes the
baseline's person IDs matter, and it is why this implementation kept the monotonic lifetime-unique
counter rather than the earlier rewrite's slot reuse
([ADR 0017](decisions/0017-person-ids-monotonic-and-free-slots.md)).

## Worth doing soon

### 6. `HLM_India` at the cohort it ships — `validation`

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
off the lattice that item 12 is about.

So this is worth doing once, on a machine that can be left alone for a few days, and the stored
reference would be large — which is item 10.

### 7. A second country for the FINCH surface — `validation`, and it needs upstream

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

### 8. Apply interventions on Kevin Hall models — `needs-ruling`

**Value: unknown, and it is not this project's call. Effort: small to wire, unbounded to justify.**
In the whole baseline, `Scenario::apply` — the call that offers a person and a risk factor to the
active policy — has one call site, in `dynamic_hierarchical_linear_model.cpp:110`. The `StaticLinear`
and `KevinHall` models never call it, so **every intervention scenario is inert on that surface**:
`marketing` and `simple` produce byte-identical output there, in both implementations
([docs/equivalence.md](equivalence.md)).

**What this run did about it.** Not implement it. A config whose active intervention declares impacts
the configured dynamic model would never apply is now **rejected at load time**, naming both
([ADR 0035](decisions/0035-refuse-an-intervention-no-model-applies.md), deviations B-25). An
intervention with an empty impact list — which is what all four Kevin Hall examples ship — is
accepted with a warning. So the silent-no-effect run is gone; the feature is not there.

**What the upstream authors' intent appears to be, from the evidence rather than from asking.** Three
things point the same way, and one points the other.

Pointing at "deliberate":

- `KevinHall_FINCH` ships `simple` with an **empty** `impacts` list, and so do `KevinHall_India`,
  `KevinHall_PIF` and `Dummy_disease_test`. Somebody who expected the mechanism to work and filled in
  coefficients would have noticed it doing nothing; somebody who knew it was inert would ship it
  empty, which is what they did. Four examples out of four.
- That surface has its own policy mechanism and it does work: `modelling.policy_start_year`, from
  which `StaticLinear` applies the S1 policy-effect coefficients and the residual policy covariance to
  the intervention scenario. The baseline's two FINCH scenarios are identical in 2022 and 2023 and
  differ in 2,476 of 4,600 reduced series in 2024. A per-factor fitted policy effect is a
  better-founded thing than an age-banded constant shift, and having built the former there is a
  reason not to wire up the latter.
- `KevinHall_PIF` is a third mechanism again — a population impact fraction multiplying disease
  incidence — and it also does not go through `Scenario::apply`. Two of the three policy mechanisms
  upstream has built since the HLM surface bypass the scenario object entirely.

Pointing at "an oversight":

- The `interventions` block is still parsed, validated and carried for those configs, and
  `active_type_id` still selects a scenario object that is constructed and then never consulted. If
  the mechanism were deliberately out of scope for that surface, the natural thing would have been to
  refuse the key — which is what this build now does.

**What it would take.** Wiring is one call in `StaticLinearModel::update_risk_factors` and one in
`KevinHallModel::update_risk_factors`, at the point where each writes a factor value. The hard part is
not the call: it is deciding **where** in the chain it goes on a surface where the factors are
food-group intakes that feed nutrients that feed an energy balance that produces weight. An age-banded
shift to `EnergyIntake` is not the same intervention as the same shift to `FoodCarbohydrate`, and
nothing in the data says which upstream means. It also changes results, so it needs a deviation entry,
an ADR, and both equivalence references re-run.

**So this is tagged `needs-ruling` rather than estimated.** The question for upstream is: on the
`StaticLinear`/`KevinHall` surface, should `running.interventions` do anything, and if so, at which
point in the food → nutrient → energy → body chain does an impact apply? Until there is an answer,
refusing the config is the honest behaviour, and it is what is implemented.

### 9. A fallback donor for immigration into an empty band — `correctness`

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

### 10. More seeds, and a smaller stored reference — `validation`

**Value: low-medium. Effort: low.** Both references are 20 seeds, confirmed at 60 and then
discarded. Keeping the 60-seed references would be about 12 MB gzipped. The alternative is to
store the reduction rather than the raw results — the harness reduces to (scenario, year, sex,
variable) before it compares anything, and the reduction is two orders of magnitude smaller — at
the cost of not being able to change the reduction without a re-run.

### 11. Windows — `platform`

**Value: unknown. Effort: medium.** Not targeted
([ADR 0013](decisions/0013-platforms-linux-and-macos.md)). The code avoids PSTL and
`<syncstream>` and has one `__APPLE__` branch, so the likely work is the executable-path lookup,
`posix_spawn` (used for `curl` and `unzip`), and the file-system assumptions in the cache. Only
worth doing if someone needs it.

## Smaller things

### 12. The lattice detector should classify on the numerator — `validation`

**Value: medium-high. Effort: low-medium, and the measurement is already taken.** The equivalence
harness compares a quantile of a **lattice-valued** series with an exact test of the counts rather
than numerically, because the normal-theory allowance shrinks as 1/√n while the lattice step does
not — a test that gets worse with more evidence. That rule was derived and added last run, and it is
right.

**Its detector looks at the wrong number.** It classifies a series as lattice-valued when the pooled
sample has at most 6 distinct values, or when one value covers more than half the seeds. Both rules
are applied to the **rate**. But a disease rate is a small integer count of cases over a band head
count that differs from seed to seed, so dividing smears the lattice: the `HLM_India` comparison at
60 seeds produced four such series with **43 to 79 distinct values** and a modal share of 0.30 to
0.40 — under both rules — while a third of their seeds were **exactly zero**. Six comparisons, at
1.02× to 1.09× of their allowance, in both the `simple` and the `food_labelling` runs, which is what
says they belong to the comparison and not to either implementation
([docs/equivalence.md](equivalence.md)).

The fix is specific: classify on the **numerator**. The harness reduces to a rate and throws the
count away; carrying the count alongside it would let the detector ask the question it means to ask —
"how many distinct case counts are there, and how often is it zero?" — instead of asking it of a
quotient.

**Do not do this by widening the thresholds.** Moving 0.5 to 0.3 after seeing which comparisons it
excludes is not evidence, which is the same rule this project applied to the one `HLM_France`
residual ([docs/equivalence.md](equivalence.md), "The threshold was not changed"). The detector
either measures the right quantity or it does not.

### 13. A schema for the model definition files — `docs`

**Value: medium. Effort: low.** `schemas/v2/` covers the config. The static and dynamic model
files have no published schema, which is why their member names were wrong for a week in the
previous run and why this run spent an afternoon on the FINCH pack's headerless two-column CSVs
and its R row-index columns. The shapes are documented only in `src/config/models/*.cpp` and in
the three loader test files. Write them, and extend `schema_agreement_test.cpp` to cover them the
way it covers the config.

### 14. Sector, and `demographic_models` — `scope`

**Value: low. Effort: low.** `person.sector` (urban/rural) is assigned nowhere; the channel
appears if the mapping declares the factor. `modelling.demographic_models` is carried through as
opaque JSON, deliberately — its shape belongs to the model family that reads it — and no model
family reads it yet.

### 15. The fixture pack's top-age artefact — `validation`

**Value: low. Effort: low.** The synthetic pack's population table stops at the same age as the
config's `age_range`, so anyone reaching the top age leaves the cohort and the pack's simulated
death rate runs above what its mortality table implies. Recorded in the pack's own `SYNTHETIC.md`.
Extending the pack's age range by a few years above the configured one would remove the artefact;
nothing depends on it, because no test reads the pack's death rates as a check on anything.

### 16. Report four things upstream — `docs`

**Value: low here, high upstream. Effort: low.** Four findings belong to the people who own the
data and the baseline, and telling them is not done:

1. **`KevinHall_India` cannot be run by its own baseline** — its `Weight` lower bound is above what
   its weight quantile curve produces (item 6), and its `new_config.json` contradicts itself
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
