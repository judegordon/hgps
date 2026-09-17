# 0021 — Scope for this run: the FINCH and HLM_France surfaces, with the `simple` intervention

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

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
