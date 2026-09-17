# Backlog

What is not done, ranked by value for effort. Every item says what it unblocks and what it costs,
because the point of a ranked backlog is to make the next decision easy rather than to be
complete.

Tags:

- `scope` — deliberately left out of this run ([ADR 0021](decisions/0021-scope-finch-and-hlm-france.md)).
- `correctness` — something could produce a wrong number or refuse a valid input.
- `validation` — makes an existing claim checkable, or checks it harder.
- `platform` — build, CI, packaging.
- `cleanup` — internal, no behaviour change.
- `docs`

## Do these first

### 1. `StaticLinear`, and with it region, ethnicity, income and physical activity — `scope`

**Value: high. Effort: high** (the baseline's `static_linear_model.cpp` is 2,615 lines.)

This is the single item everything else in the FINCH surface waits on. It brings:

- the continuous income model and income-quintile FactorsMean strata (16 baseline tests,
  `IncomeStratumAdjustment` and part of `ModelParserFinch`);
- region and ethnicity assignment, including **loading their prevalence data**, which nothing does
  today. `DemographicModule::set_region_prevalence` exists and is never called, so a config with
  `demographics.region: true` is accepted and then refused at run time with *"the project requires
  regions but no region prevalence data was loaded"*. That refusal is honest but it is a hole, and
  `KevinHall_FINCH` needs both flags true;
- the continuous physical-activity model;
- the two-stage logistic option (`project_requirements.two_stage`), which is parsed and validated
  and consumed nowhere;
- the trend types other than `null` — `upf_trend` and `income_trend` are implemented in the
  adjustment path but no static model sets up the trend tables they read.

Unblocks: `KevinHall_India` and, with the next item, `KevinHall_FINCH`.

### 2. `KevinHall` — `scope`

**Value: high. Effort: high** (1,462 lines, plus the nutrient tables and weight quantiles.)

The energy-balance model. Brings 30 baseline tests — `KevinHallHeight`,
`KevinHallWeightQuantiles`, `KevinHallWeightValidation` — which are **30 of the baseline's own 35
skips** (audit B-11): they have never run in the baseline's CI, so porting them is the first time
anyone finds out whether they pass. Point them at the synthetic pack or the converted FINCH
example and make them fail rather than skip, which is what the test fixture is set up to do.

Unblocks: the equivalence harness's second example, which is already defined in
`tests/equivalence/run.py` and reports that this build cannot run it.

### 3. The other five interventions — `scope`

**Value: medium. Effort: medium.** `marketing`, `dynamic_marketing`, `food_labelling`,
`physical_activity`, `fiscal`. Each is a small scenario class; `fiscal` is the largest because of
its impact-type variants. Brings seven baseline tests and makes `HLM_India` run (it is blocked
*only* by selecting `food_labelling`) and `KevinHall_PIF`'s scenario set usable.

`docs/examples.md` has the current state: four of six converted examples load, one runs.

### 4. Population impact fraction — `scope`

**Value: medium. Effort: medium.** 18 baseline tests across seven suites. The config block is
carried through the converter and rejected at load with a named error, so the shape is known. It
needs the PIF data tables and the disease-model hook.

## Worth doing soon

### 5. Equivalence for the FINCH surface — `validation`

**Value: high once (2) lands. Effort: low.** The harness already defines the example; nothing in
it needs to change. Until then the comparison covers one of two in-scope surfaces.

### 6. More seeds in the checked-in reference — `validation`

**Value: medium. Effort: low.** The checked-in baseline reference is 20 seeds (1.6 MB gzipped).
[docs/equivalence.md](equivalence.md) shows the standard-deviation comparison is the weak point at
n = 20 — its standard error is 16% of the standard deviation — and that 29 of the 54 failures are
standard deviations. Sixty seeds is about 5 MB and fifteen minutes; the decision is whether that
belongs in the repository or in CI artefacts.

### 7. A CI workflow — `platform`

**Value: high. Effort: low.** `scripts/check.sh` is the whole of it: configure, build and test
four presets, then the equivalence harness against the stored reference. What is missing is the
workflow file, a Linux runner (this run was developed and measured on macOS only, though the code
targets both — [ADR 0013](decisions/0013-platforms-linux-and-macos.md)) and a decision about
whether the disease-data fetch happens in CI or whether CI runs only against the synthetic pack.
The synthetic pack exists precisely so that it can.

### 8. Individual-level tracking output — `scope`

**Value: medium. Effort: low-medium.** `output.individual_tracking` is parsed, validated and
carried in `config::IndividualTracking`, and nothing writes the file. The baseline's
`individual_id_tracking_writer.cpp` is small. Worth noting: this is the feature that makes the
baseline's person IDs matter, and it is why this implementation kept the monotonic lifetime-unique
counter rather than the earlier rewrite's slot reuse
([ADR 0017](decisions/0017-person-ids-monotonic-and-free-slots.md)).

### 9. Give the age-band immigration a fallback donor — `correctness`

**Value: low-medium. Effort: low.** When an age-sex band is empty there is nobody to clone an
immigrant from, so both implementations skip it and the cohort falls short of the projected total
by a few people. [docs/equivalence.md](equivalence.md) shows this is the single mechanism behind
all 54 out-of-tolerance comparisons. The baseline has a nearest-age search in its demographic
module (`demographic.cpp:662`) that it does not use for this; using the nearest non-empty band
would make the target always achievable. It changes results, so it needs a deviation entry and a
re-run of the harness.

### 10. Windows — `platform`

**Value: unknown. Effort: medium.** Not targeted ([ADR 0013](decisions/0013-platforms-linux-and-macos.md)).
The code avoids PSTL and `<syncstream>` and has one `__APPLE__` branch, so the likely work is the
executable-path lookup, `posix_spawn` (used for `curl` and `unzip`), and the file-system
assumptions in the cache. Only worth doing if someone needs it.

## Smaller things

### 11. A schema for the model definition files — `docs`

**Value: medium. Effort: low.** `schemas/v2/` covers the config. The static and dynamic model
files have no published schema, which is exactly why their member names were wrong for a week:
`m`, `w`, `s`, `residualsStandardDeviation`, `rSquared` are documented only in
`src/config/models/*.cpp` and in `tests/config/model_loader_test.cpp`. Write them, and extend
`schema_agreement_test.cpp` to cover them the way it covers the config.

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

### 14. Profile the disease module — `cleanup`

**Value: low. Effort: low.** [docs/performance.md](performance.md) has where the time goes. The
one clear candidate is the per-person, per-disease relative-risk lookup, which dominates and is
pure arithmetic over tables that never change during a run.

## Explicitly not planned

- **Reintroducing the event bus.** Fifteen baseline tests, no purpose here: the runner hands each
  row to a sink, which is the one place output is written ([ADR 0020](decisions/0020-output-single-owner-defined-row-order.md)).
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
