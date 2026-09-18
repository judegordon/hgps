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
are done, and with them the FINCH surface end to end. What is left is smaller and more varied.

## Do these first

### 1. Make the GCC build a required check — `platform`

**Value: medium-high. Effort: unknown until it is run once.** `.github/workflows/ci.yml` builds and
tests four presets on ubuntu-latest and macos-latest, runs the harness's own tests, and runs the
equivalence comparison against both checked-in references — so the thing this item used to ask for
exists. What is left is the compiler.

Every build this project has ever done is clang. The warning set is `-Werror` with `-Wconversion`,
`-Wsign-conversion`, `-Wold-style-cast` and `-Wdouble-promotion`, and a second compiler *family* has
never seen it. Building with a newer clang than the development one found two real defects and one
style disagreement ([docs/build-notes.md](build-notes.md)), which is a fair guide to what GCC will
find.

So the workflow has a GCC job marked `continue-on-error`: the information appears without a red tick
in a commit that cannot act on it. Promoting it to required means reading what it says and fixing it,
which cannot be estimated before seeing it — hence this item rather than a guess.

### 2. A runnable Kevin Hall example, which needs upstream — `needs-ruling`

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

### 3. Finish what the index-keyed store started — `cleanup`

**Value: medium. Effort: low-medium, and it is two separate changes.**
[ADR 0037](decisions/0037-index-keyed-risk-factor-store.md) made `Person::risk_factors` index-keyed:
`KevinHall_FINCH` is 1.54× faster and uses 60% less memory, `HLM_France` 1.14× faster, and the output
is byte-identical. The profile was re-taken afterwards
([docs/performance.md](performance.md) §"Where the time goes now"), and it points at two things with a
measurement behind each rather than a guess.

**(a) `FactorValues::find_index` is a linear scan, and 55 entries is where that stops being free.**
It is now the single largest item in the FINCH profile at **18%** of samples. A scan is the right shape
for France's 11 factors — contiguous, one or two cache lines, no mispredicted branch — and at FINCH's
55 (34 declared plus 21 generated food groups) it averages 27 integer comparisons per lookup. A
per-person array indexed *directly* by factor index makes it O(1) for about the same memory, at the cost
of a second vector holding the present indices, which iteration and `size()` need.

**(b) The remaining ~31% is names being resolved at the call site.** The store no longer compares
strings; its callers still hand it a `core::Identifier`, which costs a hash probe, and some of them
*construct* one per person per year — which is what `Identifier::validate_identifier` and
`chars::is_alnum` at 8.3% of a profile mean. The fix is the same idea one level up: the linear model's
coefficient list and the Kevin Hall model's nutrient names hold resolved indices, so no name reaches
the hot loop. Bigger than (a), and it touches the model loaders.

`DataSeries` is still keyed by channel name and is the smaller half again: the analysis module and the
result writer are well below the per-person work in the profile.

Whatever is done here, the check that matters is the one that caught a defect in (the first attempt at)
ADR 0037 within minutes: run both examples before and after and compare the result files **byte for
byte**. A statistical comparison over twenty seeds calls a last-bit difference agreement.

### 4. Individual-level tracking output — `scope`

**Value: medium. Effort: low-medium.** `output.individual_tracking` is parsed, validated and
carried in `config::IndividualTracking`, and nothing writes the file. The baseline's
`individual_id_tracking_writer.cpp` is small. Worth noting: this is the feature that makes the
baseline's person IDs matter, and it is why this implementation kept the monotonic lifetime-unique
counter rather than the earlier rewrite's slot reuse
([ADR 0017](decisions/0017-person-ids-monotonic-and-free-slots.md)).

## Worth doing soon

### 5. Equivalence for `HLM_India` — `validation`

**Value: medium. Effort: medium, and most of it is machine time.** `HLM_India` loads and runs here
in 42 minutes; the baseline has not been run on it at all. This run's ruling was to put it through
the loader so that data inconsistencies surface as located input issues, and not to compare it. The
harness needs only a new entry in its example table.

The cost is the runs, not the harness: 1,240,613 people against France's 6,244, so twenty seeds of
both implementations is about a day, and the stored reference would be large. A sampled cohort
would make it cheap and would no longer be the example anyone ships.

(`KevinHall_India` was the other half of this item and is now item 6, because it cannot be run at
all.)

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

### 7. Apply interventions on Kevin Hall models — `needs-ruling`

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

### 8. A fallback donor for immigration into an empty band — `correctness`

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

### 9. More seeds, and a smaller stored reference — `validation`

**Value: low-medium. Effort: low.** Both references are 20 seeds, confirmed at 60 and then
discarded. Keeping the 60-seed references would be about 12 MB gzipped. The alternative is to
store the reduction rather than the raw results — the harness reduces to (scenario, year, sex,
variable) before it compares anything, and the reduction is two orders of magnitude smaller — at
the cost of not being able to change the reduction without a re-run.

### 10. Windows — `platform`

**Value: unknown. Effort: medium.** Not targeted
([ADR 0013](decisions/0013-platforms-linux-and-macos.md)). The code avoids PSTL and
`<syncstream>` and has one `__APPLE__` branch, so the likely work is the executable-path lookup,
`posix_spawn` (used for `curl` and `unzip`), and the file-system assumptions in the cache. Only
worth doing if someone needs it.

## Smaller things

### 11. A schema for the model definition files — `docs`

**Value: medium. Effort: low.** `schemas/v2/` covers the config. The static and dynamic model
files have no published schema, which is why their member names were wrong for a week in the
previous run and why this run spent an afternoon on the FINCH pack's headerless two-column CSVs
and its R row-index columns. The shapes are documented only in `src/config/models/*.cpp` and in
the three loader test files. Write them, and extend `schema_agreement_test.cpp` to cover them the
way it covers the config.

### 12. Sector, and `demographic_models` — `scope`

**Value: low. Effort: low.** `person.sector` (urban/rural) is assigned nowhere; the channel
appears if the mapping declares the factor. `modelling.demographic_models` is carried through as
opaque JSON, deliberately — its shape belongs to the model family that reads it — and no model
family reads it yet.

### 13. The fixture pack's top-age artefact — `validation`

**Value: low. Effort: low.** The synthetic pack's population table stops at the same age as the
config's `age_range`, so anyone reaching the top age leaves the cohort and the pack's simulated
death rate runs above what its mortality table implies. Recorded in the pack's own `SYNTHETIC.md`.
Extending the pack's age range by a few years above the configured one would remove the artefact;
nothing depends on it, because no test reads the pack's death rates as a check on anything.

### 14. Report four things upstream — `docs`

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
