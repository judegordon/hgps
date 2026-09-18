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
- `docs`

The previous run's first three items — `StaticLinear`, `KevinHall`, the other five interventions —
are done, and with them the FINCH surface end to end. What is left is smaller and more varied.

## Do these first

### 1. A CI workflow — `platform`

**Value: high. Effort: low.** `scripts/check.sh` is the whole of it: configure, build and test the
four presets, then the equivalence harness against both stored references. What is missing is the
workflow file, a Linux runner — everything here was developed and measured on macOS, though the
code targets both ([ADR 0013](decisions/0013-platforms-linux-and-macos.md)) — and a decision about
whether the disease-data fetch happens in CI or whether CI runs only against the synthetic pack.
The synthetic pack exists precisely so that it can.

This is first because the two equivalence references are checked in and nothing runs them
automatically. A harness nobody runs is a document.

### 2. Population impact fraction — `scope`

**Value: medium-high. Effort: medium.** 18 baseline tests across seven suites, and the last model
feature of the upstream surface that is refused rather than implemented. The config block is
carried through the converter and rejected at load with a named error, so the shape is known
(`src/config/loader.cpp:1001`). It needs the PIF data tables and the disease-model hook.

Unblocks `KevinHall_PIF`, the one converted example that does not load.

### 3. Resolve factor and channel names to indices once per run — `cleanup`

**Value: medium-high. Effort: medium.** [docs/performance.md](performance.md) profiles both
examples and finds the same thing in each: the program is dominated by `std::map` lookups keyed by
strings, not by arithmetic. `core::Identifier` compares by string — deliberately, because
comparing by the cached hash is audit finding B-04 — and every risk-factor read on every person in
every year is such a comparison.

The change is contained to `Person::risk_factors` and `DataSeries`: resolve each name to an index
at start-up and use the index on the hot path. It is worth doing carefully rather than quickly,
because an index-keyed store is exactly the kind of change that can reorder a reduction without
anyone noticing. Doing it wants both equivalence references re-run, which is an hour, and the
determinism tests to stay green, which they should.

### 4. Individual-level tracking output — `scope`

**Value: medium. Effort: low-medium.** `output.individual_tracking` is parsed, validated and
carried in `config::IndividualTracking`, and nothing writes the file. The baseline's
`individual_id_tracking_writer.cpp` is small. Worth noting: this is the feature that makes the
baseline's person IDs matter, and it is why this implementation kept the monotonic lifetime-unique
counter rather than the earlier rewrite's slot reuse
([ADR 0017](decisions/0017-person-ids-monotonic-and-free-slots.md)).

## Worth doing soon

### 5. Equivalence for the two India examples — `validation`

**Value: medium. Effort: low-medium.** `HLM_India` loads and runs in both implementations; this run's ruling was to put them through the loader so that data
inconsistencies surface as located input issues, and not to compare them. The harness needs only a
new entry in its example table.

The cost is not the harness, it is the runs: `HLM_India`'s cohort is 1,240,613 people against
France's 6,244, so twenty seeds of both implementations is hours rather than minutes, and its
stored reference would be large. A sampled cohort would make it cheap and would no longer be the
example anyone ships.

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

### 7. Ask upstream whether a policy should reach the Kevin Hall surface — `docs`

**Value: medium. Effort: none here, and it is not this project's call.** In the whole baseline,
`Scenario::apply` — the call that offers a person and a risk factor to the active policy — has one
call site, in `dynamic_hierarchical_linear_model.cpp`. The `StaticLinear` and `KevinHall` models
never call it, so **all six intervention scenarios are inert on the FINCH surface**: `marketing`
and `simple` produce byte-identical output there, in both implementations
([docs/equivalence.md](equivalence.md)).

That may be deliberate — FINCH's policy mechanism is `policy_start_year` and the S1 policy-effect
coefficients, which is a different and arguably better-founded thing than an age-banded shift. But
a config can select `food_labelling` on a Kevin Hall model today and get a run that reports no
error and no effect, which is the shape of thing somebody eventually mistakes for a result. At the
very least this build should say so at load time, and that is a small change once upstream has said
which way it is meant to be.

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
