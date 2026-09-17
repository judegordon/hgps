# 07 — Old rewrite: data and examples coverage

What `hpgs_og_rewrite` ships as data and model packs, how that differs from `hgps_main_data` and
`hgps_main_examples`, and what is missing.

The headline is that the rewrite **vendors its data instead of fetching it**. Where the baseline's
examples name a pinned release URL, the rewrite carries a partial copy of the data repository in
`data/undb/` and a single model pack in `models/kevinhall_finch/`, and its config points at them by
relative path. That makes it self-contained and runnable from a fresh checkout — which the baseline
is not — at the cost of pinning the data by copy rather than by checksum.

---

## 1. Vendored data — `data/undb/`

| | Baseline `hgps_main_data/data` | Rewrite `data/undb` |
|---|---:|---:|
| Files | 1,901 | 1,717 |
| Size | 60 MB | 40 MB |
| Disease directories | 50 | 41 |
| Countries (demographic) | 3 (250, 356, 826) | 3 (250, 356, 826) |
| Cost-of-disease files | 7 | 7 (identical set) |
| Analysis files | `disability_weights.csv`, `lms_parameters.csv` | identical |
| Root-level PIF CSVs | 25 | **0** |
| Per-disease `PIF/` sub-trees | present | **0** |
| Per-disease `IF{CODE}.csv` | present | **0** |

### 1.1 The directory layout was flattened

The baseline's manifest sets `demographic.path` to `"undb"`, so population, mortality and indicator
files live at `data/undb/population/`, `data/undb/mortality/`, `data/undb/indicators/`, while
diseases and analysis live at `data/diseases/` and `data/analysis/`.

The rewrite sets `demographic.path` to `""` and moves everything under one root, so
`data/undb/` directly contains `population/`, `mortality/`, `indicators/`, `diseases/`,
`analysis/`, `countries.csv` and `index.json`. One level of nesting is removed and every section
sits at the same depth.

**Assessment:** a tidier layout, and the manifest was updated consistently with it. The folder is
still called `undb` (United Nations Database) even though it now holds IHME disease data too, which
is a misnomer worth not carrying forward.

### 1.2 Nine disease directories are absent

Present in the baseline, absent from the rewrite:

```
alcoholusedisorders   cervicalcancer          larynxcancer
lipandoralcavitycancer lowerrespiratoryinfections otherpharynxcancer
roadinjuries          selfharm                tuberculosis
```

**Impact.** `HLM_India/config.json` selects 35 diseases; `KevinHall_PIF` selects 15. Several of
these appear in upstream disease lists, so those configurations could not be run against the
rewrite's data even after the config format was translated.

### 1.3 `pulmonary` was renamed to `pulmonar` — resolving an upstream inconsistency the wrong way round

As established in `02-data-and-examples.md` (D-01), the **upstream data is internally inconsistent**:
the directory is `diseases/pulmonary`, but `diseases/Metadata.json` names the disease `"pulmonar"`
(2 occurrences). Upstream examples are split between the two spellings — `HLM_India/config.json:127`
says `"pulmonar"` and fails; `KevinHall_PIF/config_smoking.json` says `"pulmonary"` and works.

The rewrite made its copy **self-consistent** by settling on `pulmonar`: the directory is
`data/undb/diseases/pulmonar`, and both `Metadata.json:11,82` and `index.json:110` agree.

**Assessment.** The internal consistency is an improvement — the rewrite's data cannot produce
D-01's failure. But it resolves the ambiguity in the direction that disagrees with the baseline's
directory name, so the two data sets are now mutually incompatible on this disease: a config written
for one fails against the other. This needs an explicit ruling for the new rewrite; it is raised in
`09-ideas-and-questions.md`.

### 1.4 Population Impact Fraction data is entirely absent

The rewrite carries **zero** PIF data: no root-level PIF CSVs (the baseline has 25), no per-disease
`PIF/` sub-trees, no `IF{COUNTRY}.csv` files, and no `population_impact_fraction` entry in
`index.json`.

The **code and schema for PIF are still present**: `src/hgps_input/data/pif_data.{cpp,h}` and
`schemas/config/population_impact_fraction.json`.

**Assessment.** This is the clearest data gap in the rewrite. The PIF feature is wired up in source
but cannot be exercised, and the entire `KevinHall_PIF` example family — 15 alternative configs
covering alcohol, smoking and joint price-increase scenarios — has no data to run against. Recorded
as R-13 in `08-rewrite-issues.md`.

---

## 2. Model packs — `models/kevinhall_finch/`

The rewrite ships **one** model pack, against the baseline examples repository's **six**. It is a
normalised, single-scenario derivative of the upstream `KevinHall_FINCH` pack: 21 files against the
upstream 56.

### 2.1 Every file was renamed to snake_case

| Upstream `KevinHall_FINCH` | Rewrite `models/kevinhall_finch` |
|---|---|
| `Finch.DataFile.csv` | `input_dataset.csv` |
| `Finch.FactorsMean.Female.csv` | `factors_mean_female.csv` |
| `Finch.FactorsMean.Male.csv` | `factors_mean_male.csv` |
| `Finch_residual_risk_factor_correlation.csv` | `residual_risk_factor_correlation.csv` |
| `ranges_riskfactors.csv` | `risk_factor_ranges.csv` |
| `physicalactivity_model.csv` | `physical_activity_model.csv` |
| `energy_physicalactivity_quantiles.csv` | `energy_physical_activity_quantiles.csv` |
| `weight_quantiles_NCDRisk_{female,male}.csv` | `weight_quantiles_ncdrisk_{female,male}.csv` |
| `boxcox_coefficients.csv`, `ethnicity.csv`, `income_model.csv`, `logistic_regression.csv`, `region.csv` | unchanged names |

### 2.2 Seven policy scenarios were collapsed to one

Upstream ships `S1_`…`S7_` prefixed pairs of `Finch_residual_policy_covariance.csv` and
`policyeffect_model.csv` — fourteen files representing seven selectable policy scenarios. The
rewrite ships one unprefixed pair: `residual_policy_covariance.csv` and `policy_effect_model.csv`.

**This incidentally fixes upstream defect D-02.** The upstream `static_model.json` references the
unprefixed `Finch_residual_policy_covariance.csv`, which does not exist in the upstream pack — the
reason `KevinHall_FINCH` fails to load in the baseline. The rewrite's pack provides that file, so
its pack loads. Whether the content corresponds to S1, to a blend, or to something else is not
recorded anywhere, and **the ability to select among the seven scenarios is lost**.

### 2.3 Income-quintile stratification inputs were dropped

Absent from the rewrite's pack:

- 10 quintile FactorsMean files (`Finch.FactorsMean.{Female,Male}.Quintile1-5.csv`)
- 10 quintile weight files (`weight_quantiles_NCDRisk_{female,male}_Quintile1-5.csv`)
- `height_female.csv`, `height_male.csv`

**Impact.** The rewrite's `config.json` sets `project_requirements.income.adjust_to_factors_mean:
true` with `categories: 4`, but ships only the unstratified `factors_mean_{female,male}.csv`. The
quintile-stratified adjustment path — the whole point of the upstream FINCH pack, and the subject of
22 of the baseline's 35 skipped `KevinHallHeight` tests — is not exercised. The two height CSVs
being absent means the Kevin Hall height model runs from whatever its fallback is; the pack's
`dynamic_model.json` references only `energy_physical_activity_quantiles.csv` and the two
unstratified weight-quantile files.

### 2.4 Two model inputs were added

`blood_pressure_medication.csv` and `systolic_blood_pressure.csv` have no counterpart in any
upstream example pack. They are consumed by the rewrite's `static_model.json`. This is new
modelling capability in the rewrite, not a reorganisation of existing data — and, with no notes or
comments anywhere, its provenance and intent are unrecorded.

### 2.5 The pack runs

Confirmed (`00-inventory.md` §4.6): `hgps_console -c models/kevinhall_finch/config.json`, with
`data.index` repointed to an absolute path, completes successfully and **byte-identically across
three same-seed runs with an intervention active**. A 2022–2025 run at `size_fraction 0.001` took
151 s.

---

## 3. Config format changes and upstream example compatibility

The rewrite's config schema is **not backward compatible**. Four independent changes:

| # | Change | Effect |
|---|---|---|
| 1 | **`$schema` URL contract.** `schema.cpp:103-109` requires the `$schema` value to end with `/schemas/config/config.json`. The baseline requires `/schemas/v1/config.json`. | Every upstream config is rejected before any other validation runs |
| 2 | **`version` removed.** Not in the rewrite's `properties` and not in `required`; the baseline requires it. | Upstream configs carry `"version": 2`, and `additionalProperties` is false |
| 3 | **`project_requirements` is now required.** Optional in the baseline. | No upstream `config.json` has it (`02-data-and-examples.md`, D-03) — only the three `new_config.json` variants do |
| 4 | **`running.seed` is a scalar.** The baseline uses an array (`"seed": [123456789]`); the rewrite's pack uses `"seed": 123456789`. | Type mismatch |

Also changed: the `data` block accepts a new `{index}` form naming a local index file, and makes
`checksum` **required** when the `{source}` form is used (`schemas/config/data.json`, enforced by
`oneOf`). The `schemas/` tree itself was reorganised — the `v1/` version directory was dropped and
shared definitions extracted into `schemas/defs/` (`age_range.json`, `csv_file.json`,
`sex_coefficients.json`, `numeric_coefficient_map.json`, `dataset_metadata.json`,
`demographic_file_info.json`, `disease_population_impact_fraction_file.json`).

**Verified.** Running the rewrite against the upstream `HLM_France/config.json`:

```
Invalid configuration - Invalid schema URL provided:
https://raw.githubusercontent.com/imperialCHEPI/healthgps/main/schemas/v1/config.json
(expected URL ending with: /schemas/config/config.json).
```

**Consequence.** No example from `hgps_main_examples` can be run by the rewrite unmodified. This is
why the reproducibility comparison in `00-inventory.md` §4.6 had to run each program on its own
example rather than a shared one — there is no configuration both accept, so the two implementations
cannot be compared on identical inputs without a translation layer that does not exist.

Dropping `v1/` also removes the mechanism by which the baseline could evolve its schema while
continuing to accept older configs. The `$schema` URL in the rewrite's own pack points at
`raw.githubusercontent.com/judegordon/hgps/main/schemas/config/config.json`, a fork path.

---

## 4. Examples coverage summary

| Upstream example | Supported by the rewrite? | Blocker |
|---|---|---|
| `Dummy_disease_test` | No | Config format (§3); also uses HLM models |
| `HLM_France` | No | Config format (§3) |
| `HLM_India` | No | Config format; 35 diseases including several of the 9 absent (§1.2); `pulmonar`/`pulmonary` (§1.3) |
| `KevinHall_FINCH` | Partially — superseded by the rewrite's own pack | Config format; missing quintile and height inputs (§2.3) |
| `KevinHall_India` | No | Config format; no India Kevin Hall pack shipped |
| `KevinHall_PIF` | No | Config format; **no PIF data at all** (§1.4) |

**What is missing, stated plainly:**

1. **All PIF data** — 25 root CSVs, all per-disease `PIF/` trees, all `IF{COUNTRY}.csv`, and the
   manifest entry. Code and schema remain; the feature is unusable.
2. **Nine disease directories**, restricting which upstream configurations are expressible.
3. **Five of six example configurations.** Only a FINCH-derived pack exists. There is no HLM
   example at all, so the `StaticHierarchicalLinearModel` and `DynamicHierarchicalLinearModel` code
   paths — both retained in the rewrite's source — have no shipped configuration that exercises
   them.
4. **Income-quintile stratification inputs**, so the feature the rewrite's own config enables is
   not exercised by the data it ships.
5. **Scenario selection** in the FINCH pack: seven policy scenarios collapsed to one unlabelled set.
6. **Any record of provenance.** No note states which upstream data release `data/undb` was copied
   from, which FINCH scenario the unprefixed policy files correspond to, or where
   `blood_pressure_medication.csv` and `systolic_blood_pressure.csv` came from. With no checksum and
   no git history, the vendored data cannot be traced back to a source.
