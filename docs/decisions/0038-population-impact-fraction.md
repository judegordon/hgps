# 0038 — Population impact fraction, loaded from the manifest and validated at load

## Status

Accepted, 2026-09-18.

## Context

Population impact fraction (PIF) was the last part of the upstream model surface this implementation
refused rather than implemented. A config enabling it was rejected at load with a named error and a
pointer to the backlog ([ADR 0021](0021-scope-finch-and-hlm-france.md)), which kept
`KevinHall_PIF` — one of the six upstream examples — unloadable.

**What a PIF is.** For one disease and one risk factor, the share of the disease's incidence
attributable to that risk factor which a policy removes, tabulated by sex, age and **years since the
intervention started**. Upstream applies it in one place, at the end of the incidence probability:

```cpp
// default_disease_model.cpp:283, default_cancer_model.cpp:295
probability *= (1.0 - pif_value);
```

in the **intervention** scenario only, with `year_post_intervention = time_now − start_time`. It is a
third policy mechanism, alongside the age-banded `interventions` block and the `StaticLinear` model's
`policy_start_year` coefficients, and like the latter it bypasses `Scenario::apply` entirely.

**The data.** `pif-data-v5.zip` from the `healthgps-data` releases, 5.4 MB, whose SHA-256 matches the
checksum `KevinHall_PIF/new_config.json` declares. It is a complete data store — countries,
demographics, diseases, analysis — plus, under 21 of its disease directories,
`PIF/<risk factor>/<scenario>/IF356.csv`. 69 tables in all: 23 (disease, risk factor) pairs across
`Alcohol` and `Smoking`, three scenarios each, one country (356, India). Every one of the 69 has the
same shape — 6,660 rows = 2 sexes × 111 ages × 30 years — and every value is in [0, 0.159].

Four things about upstream's loader shaped this decision.

1. **A missing table is a warning, and the warning is silent.** `get_pif_data` returns nullopt and
   calls `notify_warning`, which prints nothing when verbosity is `none` — the default. So a config
   enabling PIF with a risk factor the pack does not contain produces a run with **no policy applied
   and an exit code of zero**. Three of the example's own alternative configs do exactly that: they
   name `risk_factor: "Joint"`, and the pack has no `Joint` directory.
2. **`data_root_path` is dead config.** It is required by upstream's schema and has its `${VAR}`s
   expanded, and then `repository.cpp:118` overwrites it with the data store's own root before use.
   A config that points it anywhere else has been misled.
3. **A gap in a table reads as zero.** `build_hash_table` sizes a dense array from the observed
   minimum and maximum of each column and leaves every cell the file did not mention
   default-constructed, so a file missing rows produces a policy that works for part of the population
   and says nothing.
4. **The published schema's sex encoding is the opposite of the code's and the data's.**
   `schemas/v1/data_index/population_impact_fraction.json` says "Gender code: 0 = female, 1 = male";
   the loader says `csv_gender == 0 ? male : female`. The data settles it: `cervicalcancer`, a
   female-only disease, has a table that is non-zero only at `Gender=1` and identically zero at
   `Gender=0`. The code is right and the schema is wrong.

## Decision

**Implement the full upstream surface, with the load-time validation this project's diagnostics allow,
and follow the code and the data rather than the schema where they disagree.**

### The arithmetic is upstream's, unchanged

`probability *= (1 − PIF)`, intervention scenario only, `years_since_intervention = time_now −
start_time`. The table and the year are resolved **once per year** rather than once per person — they
do not change inside the population loop — which is the same optimisation upstream made and for the
same reason.

### The tables are found through the data index, not a hard-coded path

`diseases.disease.population_impact_fraction` in `index.json` gives the file name pattern, and the
published pack declares it. The **path** pattern is not in the pack, so it defaults to what upstream
hard-codes: `PIF/{RISK_FACTOR}/{SCENARIO}` under the disease directory. Reading it from the index when
it is there means a later pack can move the tables without a code change; defaulting to upstream's
layout means the pack that exists works today.

### A PIF a config asks for and the store cannot supply is an **error**

This is the substantive difference from upstream, and it is finding (1) above. A run that was asked for
a policy and applied none, and reported success, is indistinguishable from a run of the baseline — and
somebody will compare it against the baseline and find no difference and conclude something. The error
names the disease, the risk factor, the scenario and the directory it looked in, and lists what the
store does have under that disease.

The cost is real and is paid: `KevinHall_PIF`'s three `config_joint*.json` variants name a risk factor
the pack does not contain, so **this build refuses them and upstream runs them with no policy**. That
is recorded in [docs/examples.md](../examples.md) and in [docs/deviations.md](../deviations.md), and it
is the right way round.

### A table must be complete over its own range

Every (sex, age, year) combination inside the observed minima and maxima must be present exactly once.
A gap is an error naming the first missing combination; a duplicate is an error naming both rows. That
is finding (3): upstream reads a gap as a PIF of exactly zero, which is a meaningful value the file is
full of, so a gap is indistinguishable from a tabulated zero.

Outside the range, `at()` returns zero and does **not** error: a person older than the table's last age
or a year beyond its horizon is an ordinary thing a run meets, and "this policy does nothing here" is
the right answer for an untabulated cell. The distinction is between a hole in the middle and the
edge of the table.

### A value outside [0, 1] is an error, not a clamp

Upstream clamps and warns. A fraction above one makes an incidence probability negative; below zero it
makes a policy cause disease. Neither is a number this program can act on. The real data is in
[0, 0.159], so this refuses nothing that exists.

### `data_root_path` is accepted and warned about

Finding (2). It is in the config format because upstream's schema requires it, and setting it is
reported as a warning saying it is ignored, that the tables come from `data.source`, that upstream
ignores it too, and where. Rejecting it outright would make every upstream PIF config unloadable for a
field that has never done anything.

### Sex is 0 = male, 1 = female

Finding (4). The code and the data agree; the schema is wrong.
`PopulationImpactFractionData.TheSexEncodingIsTheDataAndNotTheSchema` reads the real `cervicalcancer`
table and asserts the female-only disease's non-zero rows are the female ones — so the encoding is
pinned by the evidence that settled it rather than by a comment.

## Alternatives

- **Keep refusing it.** Leaves an example unloadable and an upstream feature unimplemented, and the
  data release turned out to be fetchable and hash-verified, which removed the last reason.
- **Warn like upstream when a table is missing.** Byte-compatible behaviour, and it reproduces the
  defect deliberately. [ADR 0024](0024-deviations-recorded-baseline-bugs-fixed.md) is the standing
  decision not to.
- **Read the tables lazily, per disease, when first needed.** Upstream does this through its cached
  repository, which is audit finding B-02 — a lazily populated store mutated from inside a parallel
  loop. Everything is loaded before any worker thread exists, as everything else here is.
- **Follow the schema's sex encoding.** Would silently apply a female policy to men. The schema is a
  document; the data and the loader are the system.
- **Treat a `Joint` risk factor as a request for the flat `PIF-joint_*.csv` files in the pack's
  `diseases/` root.** Those files exist, they are what `ProcessImpactFractionEstimates.R` produces, and
  they are not in the per-disease `PIF/` tree the loader reads. Guessing that one is meant to satisfy a
  request for the other would be inventing a data layout on somebody else's behalf. The error names
  what is there instead.
- **Store the table sparsely, keyed by (sex, age, year).** A map lookup per person per disease per
  year, on the hottest loop in the program, for a table whose dense form is 6,660 doubles.

## Consequences

`KevinHall_PIF` loads and runs, which makes **five of the six upstream examples** run end to end.

The PIF data pack is a *different* data store from the one `HLM_France` and `KevinHall_FINCH` use, so
enabling PIF is not a switch on an existing run: it is a config pointing at that release. The PIF
example's own config already does.

`DiseaseDefinition` gains an optional `PifTable`, and `DiseaseModelBase::update_incidence_cases` gains
one branch outside the population loop and one multiply inside it. Both disease families get it from
the shared base, so — unlike upstream, where the code is duplicated between
`default_disease_model.cpp` and `default_cancer_model.cpp` — there is one copy.

18 baseline tests across seven suites covered PIF and are ported; `docs/test-port-map.md` says where
each went.
