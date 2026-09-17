# 0021 — Scope for this run: the FINCH and HLM_France surfaces, with the `simple` intervention

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

**Amended 2026-09-18, also by the project owner.** The scope below was for the first build run,
which reached `HLM_France` and stopped short of FINCH. The second run's ruling extends it to the
full `KevinHall_FINCH` surface — `StaticLinear` including the two-stage logistic, categorical and
continuous income, region, ethnicity and income-quintile FactorsMean strata; the `KevinHall`
energy balance; the derived-predictor resolver; and **all six** intervention scenarios, not just
`simple`. Where a feature was rejected at load, the rejection was replaced by the implementation
and the corresponding "unsupported" test deleted.

Still out of scope, and still rejected at load with a named error: **population impact fraction**.
`HLM_India` and `KevinHall_India` are converted and run through the loader and the engine so that
any data inconsistency surfaces as a located input issue, but they are not compared against the
baseline. Everything below that is not listed in this amendment still holds, and the
"designed for rather than designed out" paragraph is what made the extension a matter of filling
in registry entries rather than reshaping the engine.

## Context

The baseline's model surface is six intervention scenarios, six risk-factor model families and 50
diseases, over six example packs. The audit found the surface is not exercised: `HLM_India` fails
outright on a disease-name inconsistency (D-01), `KevinHall_FINCH` references a model input file
that does not exist (D-02), the examples pin five mutually exclusive data releases (D-04), and the
earlier rewrite kept the full surface while shipping data for only one example — no HLM example at
all, no PIF data, no income-quintile inputs.

The audit's conclusion: "a smaller surface with real test data and real validation is worth more
than a complete surface that cannot be exercised".

## Decision

Ruled by the project owner. In scope for this run:

- **HLM_France** — static `HLM` (`StaticHierarchicalLinearModel`) plus dynamic `EBHLM`
  (`DynamicHierarchicalLinearModel`), 6 diseases, France, 2010–2050. The canonical reference
  example and the only upstream example with no active intervention.
- **KevinHall_FINCH** — static `StaticLinear` plus dynamic `KevinHall` energy balance, 15 diseases,
  United Kingdom, income quintile stratification, region and ethnicity, per-person tracking.
- The **`simple`** intervention scenario.
- End to end: config → data → models → simulation → output, with unit tests per component.

Out of scope for this run, and explicitly designed for rather than designed out: `HLM_India`,
`KevinHall_India`, `KevinHall_PIF`, `Dummy_disease_test`, and the `marketing`,
`dynamic_marketing`, `food_labelling`, `physical_activity` and `fiscal` scenarios.

"Designed for" means concretely:

- `Scenario` is an interface with `BaselineScenario` and `SimplePolicyScenario` implementing it; a
  new scenario is a new file plus a registry entry, with no change to the engine.
- The risk-factor model registry maps a model name to a builder, so a new family is a new unit plus
  a registry entry (`config/models/`).
- `population_impact_fraction` is a reserved, schema-valid config block that is rejected at load
  with `IssueCode::feature_not_implemented` (ADR 0010), so a PIF config fails with a sentence rather
  than being silently ignored.

## Alternatives

- **The full surface.** Six scenarios and six model families, five of them untestable here because
  their data or examples are broken upstream — i.e. the earlier rewrite's position, which the audit
  criticised.
- **HLM_France only.** Halves the work and would leave the Kevin Hall energy-balance model, the
  income-quintile machinery and the whole of `StaticLinearModel` unimplemented — and those are what
  the 35 skipped baseline tests exercise.

## Consequences

`docs/examples.md` records, per upstream example, whether it runs now or is out of scope.
`docs/backlog.md` ranks the remaining surface. A config selecting an out-of-scope scenario or model
fails at load with a clear message; nothing silently degrades.
