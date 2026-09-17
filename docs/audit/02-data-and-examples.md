# 02 — Baseline data and examples

Subjects: `hgps_main_data` (60 MB, 1,908 files) and `hgps_main_examples` (286 MB, 245 files),
as consumed by `hgps_main`.

---

## 1. How the baseline locates and loads data

### 1.1 Resolving the data source

The data store is named by `data.source` in the config, and may be a directory, a local `.zip`, or
an `https://` URL (`data_source.cpp:9-21`). Resolution order (`DataSource::get_data_directory`,
`data_source.cpp:50`):

1. If `source` is a directory → use it as-is.
2. Else if it ends in `.zip` and is a regular file → use that archive.
3. Else if it starts with `http://`/`https://` → download to a temporary file via curlpp
   (`download_file.cpp`).
4. Otherwise → `throw std::runtime_error("Data source must be a directory, a zip file or a URL …")`.

For archives, the file's SHA-256 is computed (`sha256.cpp`) and used as a content-addressed cache
key: `get_cache_directory()/zip/<hash[0:2]>/<hash[2:]>` (`zip_file.cpp:47`). If that directory
already exists the archive is not re-extracted. The optional `data.checksum` in the config is the
expected hash.

Relative `source` paths are rebased on the config file's own directory
(`try_rebase_path`, `data_source.cpp:24-36`).

There is also a `-s/--storage` command-line option. **It cannot be used with a schema-valid
config** — see `04-baseline-issues.md` (B-06).

### 1.2 Reading the store

`DataManager` (`datamanager.cpp`, 806 lines) implements `core::Datastore`. On construction it reads
`<root>/index.json` and uses it as a manifest describing where every other file lives and how to
interpret it. Paths are built by token substitution: `{COUNTRY_CODE}`, `{DISEASE_TYPE}`,
`{GENDER}`, `{RISK_FACTOR}`.

CSVs are read by `csvparser.cpp` (rapidcsv) into `core::DataTable`, whose columns are typed
according to the `inputs.dataset.columns` map in the config.

Loaded data is cached in `CachedRepository` (`repository.cpp`) so that repeated lookups — and the
two concurrently-running scenarios — share one copy.

---

## 2. Data inventory — `hgps_main_data`

The manifest `data/index.json` (352 lines) declares four sections.

| Section | `path` | Source declared in index | Licence declared | Consumed by |
|---|---|---|---|---|
| `country` | (root) | ISO 3166-1 | N/A | `DataManager::get_country`, validated in `program.cpp:152` |
| `demographic` | `undb/` | UN World Population Prospects 2019, projections 2022, ages 0–110, years 1950–2100 | Creative Commons | `DemographicModule` |
| `diseases` | `diseases/` | IHME, reference year 2019, ages 1–110 | **CC BY-NC-ND 4.0** | `DiseaseModule`, `DefaultDiseaseModel`, `DefaultCancerModel` |
| `analysis` | `analysis/` | IHME | CC BY-NC-ND 4.0 | `AnalysisModule`, `LmsModel` |

### 2.1 Files by section

| Path | Files | Format | Consumed by |
|---|---|---|---|
| `countries.csv` | 1 (249 rows) | CSV: `Code,Name,Alpha2,Alpha3` | Country validation |
| `undb/population/P{CODE}.csv` | 3 | Population by sex and single year of age, 1950–2100 | `DemographicModule::get_total_population_size`, `get_population_distribution` |
| `undb/mortality/M{CODE}.csv` | 3 | Deaths by sex and age | `create_death_rates_table`, residual mortality |
| `undb/indicators/Pi{CODE}.csv` | 3 | Births, deaths, life expectancy, migration by year | `get_birth_rate`, `get_net_migration`, `LifeTable` |
| `diseases/<disease>/D{CODE}.csv` | 50 dirs | Prevalence, incidence, remission, mortality by age and sex | `DiseaseDefinition` measure tables |
| `diseases/<disease>/relative_risk/disease/*.csv` | per disease | Disease→disease relative risks | `calculate_relative_risk_for_diseases` |
| `diseases/<disease>/relative_risk/risk_factor/{GENDER}_{DISEASE}_{FACTOR}.csv` | per disease | Risk-factor→disease relative risks | `calculate_relative_risk_for_risk_factors` |
| `diseases/<disease>/P{CODE}/` | selected cancers | `prevalence_distribution.csv`, `survival_rate_parameters.csv`, `death_weights.csv` | `DefaultCancerModel` |
| `diseases/<disease>/IF{CODE}.csv`, `diseases/<disease>/PIF/**` | selected | Population Impact Fraction tables by intervention scenario | `PIFData`, intervention scenarios |
| `diseases/*.csv` (25 at the directory root) | 25 | Standalone PIF tables, e.g. `Alcohol_PIF_10p-increase-in-price_Female.csv` | PIF loading |
| `diseases/Metadata.json` | 1 | Disease registry metadata | Disease registry |
| `diseases/PIF_Mappings.xlsx` | 1 | **Not machine-read** — see §4 | — |
| `analysis/disability_weights.csv` | 1 | YLD weights per disease | `AnalysisModule` |
| `analysis/lms_parameters.csv` | 1 | LMS parameters for BMI→weight category | `LmsModel`, `WeightModel` |
| `analysis/cost/BoD{CODE}.csv` | 7 | Cost of disease | `AnalysisModule` cost outputs |

### 2.2 Country coverage

Demographic and cost data exist for **three countries only**: `250` France, `356` India,
`826` United Kingdom. Disease files additionally use codes `8261`–`8264`, which are **not present in
`countries.csv`** and are not ISO 3166-1 codes; they appear to be UK sub-national breakdowns. Any
config naming one of them would fail the country validation in `program.cpp:152` while its disease
data loaded happily — an asymmetry worth removing in the new rewrite.

### 2.3 Supporting scripts

Four R scripts at the repository root — `AmalgamateDiseaseData.R`,
`CheckDiseaseDataComplete.R`, `CheckIncidenceOfDiseases.R`, `ProcessImpactFractionEstimates.R` —
prepare and sanity-check the CSVs. They are not invoked by the C++ program. `requirements.txt`
contains a single pinned Python package and is unrelated to the R scripts.

---

## 3. Examples inventory — `hgps_main_examples`

Six example model packs. Each contains a `config.json`, one or more risk-factor model JSON files, an
input dataset CSV, and FactorsMean adjustment CSVs.

| Example | Country | Model pair | Diseases | Years | `size_fraction` | Active intervention | Pinned data release |
|---|---|---|---|---:|---:|---|---|
| `Dummy_disease_test` | IND | static + dynamic HLM | 5 | 2022–2030 | 0.0001 | `simple` | `20240624` |
| `HLM_France` | FRA | static + dynamic HLM | 6 | 2010–2050 | 0.0001 | none | `20240624` |
| `HLM_India` | IND | static + dynamic HLM | 35 | 2010–2050 | 0.001 | `food_labelling` | `20240624` |
| `KevinHall_FINCH` | GBR | Kevin Hall energy balance | 15 | 2022–2026 | 0.0001 | `simple` | `finch-v4` |
| `KevinHall_India` | IND | Kevin Hall | 7 | 2022–2026 | 0.0001 | `simple` | `20240624` |
| `KevinHall_PIF` | IND | Kevin Hall + PIF | 15 | 2022–2024 | 0.0001 | `simple` | `PIF-v7` |

All six use seed `123456789` (except `Dummy_disease_test`, `12345`) and `trial_runs: 1`.

**What each demonstrates.** `Dummy_disease_test` is the smallest end-to-end smoke test.
`HLM_France` is the canonical hierarchical-linear-model reference and the only example with no
intervention — it is the natural reproducibility baseline. `HLM_India` scales the same models to 35
diseases and demonstrates the food-labelling scenario. `KevinHall_FINCH` demonstrates the energy
balance model with income quintile stratification (12 FactorsMean quintile CSVs), continuous income,
region/ethnicity, and per-person ID tracking. `KevinHall_India` demonstrates Kevin Hall with
subsidy/reformulation/cess scenario variants (18 scenario sub-directories). `KevinHall_PIF`
demonstrates Population Impact Fraction interventions, with 15 alternative config files for alcohol,
smoking and joint scenarios.

`R/` holds post-processing and plotting scripts. `example-jobscript.sh` is a PBS job script for
Imperial's HPC. `new_config_skeleton.json` is a documented template for the current config format.

---

## 4. Data and examples that are broken, stale, or inconsistent

Each item below was confirmed by running the built baseline binary or by direct file inspection.

### D-01 — `HLM_India/config.json` cannot run: disease `pulmonar` does not exist. **Confirmed.**

`HLM_India/config.json:127` requests the disease `"pulmonar"`. The data repository has no such
directory; it has `diseases/pulmonary`. Running it:

```
$ HealthGPS.Console -c HLM_India/config.json --dry-run
There are 50 diseases in storage, 35 selected.
Failed with message: Disease code: 'pulmonar' not found..
```

The root cause is an inconsistency *inside the data repository itself*:
`data/diseases/Metadata.json` names the disease `"pulmonar"` (2 occurrences) while the directory on
disk is `pulmonary`. Other examples — `KevinHall_PIF/config_smoking.json` and siblings — spell it
`"pulmonary"` and therefore work. So the two spellings are both live, and which one is correct
depends on which file you read. This matters for the new rewrite because the old rewrite resolved
the inconsistency in the *opposite* direction (see `07-rewrite-data-examples.md`).

### D-02 — `KevinHall_FINCH` references a model input file that does not exist. **Confirmed.**

`KevinHall_FINCH/static_model.json` references `Finch_residual_policy_covariance.csv`. That file is
not in the pack. What is present is seven scenario-prefixed variants:
`S1_Finch_residual_policy_covariance.csv` through `S7_…`. Running it:

```
Could not find file …/KevinHall_FINCH/Finch_residual_policy_covariance.csv
Failed with message: Could not load input file info.
```

This is independent of the pinned data release — the missing file is part of the *example*, not the
data store. This is the same example the 35 skipped unit tests depend on
(`00-inventory.md` §4.3), so the example and the test suite are broken by the same gap.

### D-03 — No `config.json` carries `project_requirements`, though the code and tests expect it. **Confirmed.**

`project_requirements` gates a large amount of current behaviour — `demographics.region`,
`demographics.ethnicity`, `income.type`/`categories`/`adjust_to_factors_mean`,
`risk_factors.adjust_to_factors_mean`, `physical_activity`, `trend`, `two_stage.use_logistic` —
read throughout `static_linear_model.cpp`, `demographic.cpp` and `kevin_hall_model.cpp`.

None of the six `config.json` files contains the block. Only three *secondary* files do:
`KevinHall_FINCH/new_config.json`, `KevinHall_India/new_config.json`,
`KevinHall_PIF/new_config.json`. The baseline's own planning document records this as outstanding
work with unticked checkboxes:

```
documentation/technical/plans/project-requirements-plan.md:228
- [ ] `input-data/data/KevinHall_FINCH/config.json` (or new_config.json) - add `project_requirements`
- [ ] `input-data/data/KevinHall_India/config.json` - add `project_requirements`
- [ ] `input-data/data/KevinHall_PIF/config.json` - add `project_requirements`
```

The effect is that the *primary* config of every example exercises the legacy code path, and the
modern path is only reachable through the `new_config.json` variants. Which file is canonical is
undocumented.

### D-04 — The examples pin five different, mutually exclusive data releases. **Confirmed.**

Distinct `data.source` values across the repository:

```
https://github.com/imperialCHEPI/healthgps-data/releases/download/20240624/data_20240624.zip
https://github.com/imperialCHEPI/healthgps-data/releases/download/finch-v4/data.zip
https://github.com/imperialCHEPI/healthgps-data/releases/download/PIF-v5/pif-data-v5.zip
https://github.com/imperialCHEPI/healthgps-data/releases/download/PIF-v7/pif-data-v7.zip
https://github.com/imperialCHEPI/healthgps-data/releases/download/PIF-v8/pif-data-v8.zip
https://example.com/your-data/releases/download/v1/data.zip        (skeleton placeholder)
```

No single checkout of `hgps_main_data` satisfies all six examples, and nothing in either repository
records which release corresponds to the current `hgps_main_data` working copy. Three examples
(`Dummy_disease_test`, `KevinHall_India`, `KevinHall_PIF`) do dry-run successfully against the local
data snapshot; `HLM_France` runs to completion; `HLM_India` fails on D-01 and `KevinHall_FINCH` on
D-02.

### D-05 — 177 MB of simulation *output* is committed to the examples repository. **Confirmed.**

`HLM_France_results-50sims/` contains 50 result CSVs dated 2024-01-29, totalling 177 MB — 62% of the
repository's 286 MB. These are run artefacts, not inputs. They are not referenced by any config or
script other than the R post-processing helpers.

### D-06 — `diseases/PIF_Mappings.xlsx` is an unreadable-by-the-program binary. **Confirmed.**

The data repository contains exactly one `.xlsx`. Nothing in `datamanager.cpp` or `pif_data.cpp`
reads Excel; the manifest `index.json` does not mention it. It is human documentation living in a
machine-read tree, and it will silently drift from the CSVs it describes.

### D-07 — A directory name containing a space and a personal name. **Confirmed.**

`hgps_main/disease_check_scripts- Mahima/` (note the space after the hyphen). Not referenced by the
build or by any config. Combined with the 157 `MAHIMA:` comments in the source, this is in-progress
personal working material committed to the main tree.

### D-08 — Model parsing emits a bare warning for a missing key rather than failing or defaulting explicitly. **Confirmed.**

Running any HLM example prints `Missing key "policy_start_year"` with no indication of what value was
used instead, no file name, and no severity marker. A silent default in a policy model is a
correctness risk for the reader of the output.
