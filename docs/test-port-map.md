# Test port map

How the baseline's 471 tests map onto this implementation's 433, suite by suite. It exists so that
"the tests were ported" is a checkable claim rather than an assertion, and so that a reader can
find the descendant of any baseline test — or read, in one line, why there isn't one.

Columns: the baseline suite and its test count, where those tests live here, and what changed.

- **value** — the expected numbers are carried over unchanged. This is the strongest kind of port:
  it pins arithmetic, not just shape.
- **intent** — the test asserts the same property against a different API.
- **fixed** — the expectation was changed because the baseline test encoded one of the audit's
  findings. Each of those names the finding ID in a comment in the test.
- **not ported** — with the reason. Either the thing tested does not exist here by design, or it
  belongs to a model family this run did not implement.

Totals are at the bottom. Verified against `hgps_tests --gtest_list_tests` and the baseline's
`HealthGPS.Tests --gtest_list_tests`, not counted by hand.

## Core and infrastructure

| Baseline suite | Tests | Here | Notes |
|---|---:|---|---|
| `TestCore` | 18 | `tests/core/core_test.cpp` (19) | value. Plus one added case for case-insensitive column lookup and insertion order. |
| `TestCore_Identity` | 12 | `tests/core/identifier_test.cpp` (14) | value, plus two added: equality and ordering must agree (**fixed**, B-04), and the hash is for bucketing only. |
| `TestCore_Interval` | 10 | `tests/core/interval_test.cpp` (12) | value, plus inverted bounds and a non-numeric delimiter (the baseline parsed a `string_view`'s `.data()`, which reads past the field). |
| `TestCore_MathHelper` | 7 | `tests/core/math_helper_test.cpp` (8) | value, plus an unequal-values case. |
| `TestCore_Array2D` | 15 | `tests/core/array2d_test.cpp` (15) | value. |
| `TestCore_UnivariateSummary` | 10 | `tests/core/univariate_summary_test.cpp` (12) | value — the moment recurrence's expected values are carried over unchanged, which is what pins the port. Plus `clear` and undefined-moment cases. |
| `IncomeCategoryLayout` | 4 | `tests/core/income_category_layout_test.cpp` (5) | value. `HgpsException` becomes `std::invalid_argument`: `core` has no dependency on the diagnostics module. Plus an added test that the strata are ordered low to high, which is what income sampling relies on (B-05). |
| `SHA256` | 3 | `tests/io/sha256_test.cpp` (7) | value — the baseline's expected digests are kept, against our own implementation rather than OpenSSL's. Plus the FIPS 180-4 vectors, block-boundary lengths and single-use enforcement. |

## Config and model files

| Baseline suite | Tests | Here | Notes |
|---|---:|---|---|
| `ConfigParsing` (10) + `ConfigParsingFixture` (22) + `ConfigSchemaExpanded` (47) + `ConfigLegacyFields` (3) | 82 | `tests/config/config_loader_test.cpp` (26), `tests/io/json_test.cpp` (10), `tests/config/schema_agreement_test.cpp` (5) | intent. The baseline's `get`/`get_to`/`rebase_valid_path_to` helper tests become `io::JsonCursor` tests, because accumulated diagnostics replace throw-per-problem; its section loaders (`load_input_info`, `load_modelling_info`, `load_running_info`, `load_output_info`, `load_interventions`, `check_version`) each have a counterpart. One loader test carries many baseline cases, which is why 82 maps onto 41. **fixed**: `seed` is required and scalar (B-06); `output.file_name` is used exactly as configured (B-08); an undefined `${VAR}` is an error (N-17); `project_requirements` is required (D-03); `sync_timeout_ms` is rejected (ADR 0009). |
| `ConfigurationPIF` | 2 | `tests/config/config_loader_test.cpp` | intent — PIF config is reserved and rejected at load in this build (ADR 0021), so the two struct tests become one reservation test. |
| `JsonParser` | 29 | `tests/config/config_loader_test.cpp`, `tests/config/model_loader_test.cpp` (13), `tests/core/interval_test.cpp` | intent. Twenty-six of the baseline's 29 are `to_json`/`from_json` round-trips of its poco structs; nothing here writes a config, so a round-trip has no counterpart and the *reading* half is what was ported. `CoefficientInfo`, `LinearModelInfo`, `VariableInfo`, `FactorDynamicEquationInfo` and `Array2Info` become the model-loader tests; `Interval` and `DoubleInterval` the interval tests; `SettingsInfo`, `SESInfo`, `PolicyPeriodInfo`, `PolicyImpactInfo`, `PolicyAdjustmentInfo`, `PolicyScenarioInfo`, `OutputInfo` and `IndividualIdTrackingConfig` the config-loader tests. `FileInfoToJson` has no counterpart: nothing writes that structure. |
| — | — | `tests/config/convert_config_test.cpp` (11), `ConvertedExamples` (3) | Added: the v1→v2 converter and the six converted examples as acceptance tests (ADR 0010). |
| `ModelParserFinch` | 3 | — | **not ported**: the FINCH model parser needs `StaticLinear` and `KevinHall`, which this run did not implement (ADR 0021, docs/backlog.md). |
| `LoadNutrientTable` | 2 | — | **not ported**: nutrient tables are a `KevinHall` input. |

## Data store

| Baseline suite | Tests | Here | Notes |
|---|---:|---|---|
| `DatastoreTest` | 20 | `tests/data/store_test.cpp` (23) | value where the numbers are the store's own, intent where a `HgpsException` becomes a located issue. Plus the registry-vs-tree validation the baseline has no equivalent for. |
| — | — | `DataRegistryValidation` (8), `DataSourceTest` (9), `DataIndexTokens` (2) | Added: D-01 registry validation, the checksum requirement and the content-addressed cache (ADR 0011), and index token resolution. |
| `RepositoryTest` | 3 | — | **not ported by design**: `CachedRepository`'s lazy, lock-after-read caching is the mechanism behind audit B-02. Everything a run needs is loaded before any worker thread exists, so there is no cache to test. |
| `DataManagerPIF` (3) + `PIFData` (3) + `PIFDataItem` (2) + `PIFTable` (3) + `RepositoryPIF` (2) + `DiseaseModelPIF` (3) + `PIFIntegration` (2) | 18 | — | **not ported**: population impact fraction is out of scope for this run and rejected at load with a named error (ADR 0021). |

## Model components

| Baseline suite | Tests | Here | Notes |
|---|---:|---|---|
| `TestHealthGPS_AgeGenderTable` | 12 | `tests/model/containers_test.cpp` (12) | value. |
| `TestHealthGPS_Map2d` | 6 | `tests/model/containers_test.cpp` (6) | value, including the user-defined value type. |
| `TestHealthGPS_Mapping` | 8 | `tests/model/containers_test.cpp` (6) | intent. The baseline's two iterator tests become one that checks the iterator and `entries()` agree; **fixed**: the entries keep the config's declared order rather than being sorted by (level, name) (docs/deviations.md). |
| `TestHealthGPS_LifeTable` | 5 | `tests/model/containers_test.cpp` (5) | value, plus the three construction-validation cases — which found that this implementation accepted a half or mismatched table and would have thrown `out_of_range` from inside the year loop instead. |
| `TestHealthGPS_Disease` | 12 | `tests/model/disease_types_test.cpp` (4 + `TestHealthGPS_MonotonicVector` 2) | value. The baseline's 12 are three measure cases, four table cases and five `MonotonicVector` cases; they are consolidated into six tests with the same assertions. |
| `TestRelativeRiskLookup` | 4 | `tests/model/disease_types_test.cpp` (5) | **value, and the strongest port in the suite**: `ReferenceDataLookup`'s 44 expected relative risks against a real 7×5 table are carried over unchanged and pass, which pins the interpolation arithmetic exactly. |
| `TestHealthGPS_LinearModelEvaluator` | 6 | `tests/model/predictor_resolver_test.cpp` (8) | value, plus two added: the coefficient sum is in name order (**fixed**, B-05 extension) and an unresolvable name is an `InternalError` rather than a swallowed fallback (ADR 0018). |
| `TestHealthGPS_PredictorResolver` | 12 | `tests/model/predictor_resolver_test.cpp` (14) | value. |
| `TestHealthGPS_Population` | 34 | `tests/model/population_test.cpp` (42) | value, plus **fixed**: a slot is free only when both its timestamps are in the past; `add` is not `noexcept` (B-12); adding many people is not quadratic (B-13). |
| `TestHealthGPS_Metrics` | 4 | `tests/model/containers_test.cpp` (5) | value. |
| `DataSeries` | 3 | `tests/model/containers_test.cpp` (5) | intent. The baseline's strata are created by an explicit call that had to be made idempotent; here they are created on first use, so the property checked is that touching a stratum again finds its values rather than a fresh vector. |
| `WeightModelTest` | 5 | `tests/model/disease_types_test.cpp` (7) | value, plus the zero-lambda LMS form and a missing LMS row. |
| `DemographicSummary` | 2 | — | **not ported**: one asserts the content of a printed summary box, which this build does not print; the other needs region and ethnicity prevalence data, which is loaded nowhere yet — the FINCH surface needs it and the run refuses with a named error without it (docs/backlog.md). |
| `IncomeStratumAdjustment` | 13 | — | **not ported**: income-quintile FactorsMean strata are a `StaticLinear` feature. The config for them loads and is validated (`KevinHall_FINCH` converts with all fifteen paths rebased); nothing consumes it yet. |
| `KevinHallHeight` (22) + `KevinHallWeightQuantiles` (7) + `KevinHallWeightValidation` (1) | 30 | — | **not ported**: the `KevinHall` energy-balance model is out of scope for this run. These are the 30 tests that make up most of the baseline's own 35 skips (B-11), so they have never run in the baseline's CI either. |

## Simulation, scenarios and output

| Baseline suite | Tests | Here | Notes |
|---|---:|---|---|
| `TestSimulation` | 25 | `tests/sim/simulation_test.cpp` (11), `tests/random/*` (20), `tests/model/containers_test.cpp` | intent. The six `Random*` tests become the four RNG suites, which go considerably further (B-06, B-07, B-14, B-15, N-5 all came from there). `CreateRuntimeContext`, `CreateSESNoiseModule`, `CreateDemographicModule`, `CreateDiseaseModule`, `CreateAnalysisModule` and `CreateRiskFactorModuleFailWithEmpty` become the end-to-end run in `simulation_test.cpp` plus the load-time rejections in the config tests: there is no module factory registry here to test, because the modules are built once, explicitly, in `app/build_modules.cpp`. `ModelInputProjectRequirements*` become `TestHealthGPS_ModelInput`. `AnalysisModuleDoesNotDoubleCountIncomeFieldsWhenMapped` becomes `AChannelThatIsBothADeclaredFactorAndAMemberIsCountedOnce`, generalised from income to any such channel — which is the regression test for the `mean_gender` bug the equivalence harness found. `DiseaseModuleUpdateWithInterventionAndPIFConfig` is **not ported** (PIF). |
| `ScenarioTest` | 23 | `tests/sim/simulation_test.cpp` (`ScenarioTest` 5, `ScenarioJournalTest` 3), `tests/config/config_loader_test.cpp` | intent. The baseline's `PolicyPeriod*` construction-validation tests move to the config loader, which is the only place a period can come from here; `PolicyIntervalEquality` is a defaulted `operator<=>` on `config::PolicyPeriod`. The seven `FiscalPolicy*` and `MarketingPolicy*` tests are **not ported**: only `simple` is implemented, and selecting any other intervention is rejected at load with a named error. |
| `ChannelTest` | 9 | `ScenarioJournalTest` (3) | **not ported by design**: `SyncChannel` is the mechanism behind audit B-01 and the `sync_timeout_ms` config field. Scenarios run sequentially and the figures travel in a journal (ADR 0009), so what is tested is the journal's record-and-replay contract instead. |
| `TestHealthGPS_EventBus` (14) + `EventMonitor` (1) | 15 | — | **not ported by design**: there is no event bus. The runner hands each result row to a sink, which is the one place output is written (ADR 0020). |
| `ResultFileWriter` | 5 | `tests/output/result_writer_test.cpp` (8) | value. Writing them found the income-stratum files being named from the layout's short labels, so a four-category run wrote `result_LowerMidIncome.csv` where the baseline writes `result_LowerMiddleIncome.csv`. Plus three added: no timestamp in the CSV (N-15, N-16), the recorded seed is the seed used (B-06), and the row order (B-01). |

## Added here, with no baseline counterpart

| Here | Tests | Why |
|---|---:|---|
| `tests/core/chars_test.cpp` | 4 | B-03: eleven `<cctype>` calls on a signed `char`. Exercises the whole byte range. |
| `tests/core/matrix_test.cpp` | 6 | The Cholesky decomposition and matrix-vector product are ours rather than Eigen's (ADR 0023). |
| `tests/core/parallel_test.cpp` | 6 | D5: a fixed-order reduction, bit-identical at any thread count (N-7). |
| `tests/random/engine_test.cpp` | 3 | The baseline has no RNG test at all, which is how B-06, B-07, B-14 and B-15 all survived. |
| `tests/random/parallel_guard_test.cpp` | 4 | D3: an RNG draw inside a parallel region throws with a source location. |
| `tests/random/categorical_test.cpp` | 7 | D4: ordered-only sampling (B-05). |
| `tests/random/modulo_bias_test.cpp` | 3 | The regression test for the earlier rewrite's `% range`. |
| `tests/random/source_test.cpp` | 10 | The draw contracts: half-open `next_int`, 53-bit `next_double`, rejected degenerate inputs. |
| `tests/diagnostics/diagnostics_test.cpp` | 7 | The two-tier diagnostics design has no baseline counterpart. |
| `tests/io/csv_reader_test.cpp` | 20 | The baseline's CSV path has no unit test; every case here is a located diagnostic replacing a coloured `std::cout` line. Includes the fractional-integer truncation the France dataset needs. |
| `tests/io/data_source_test.cpp` | 9 + 1 | Checksum requirement, content-addressed cache, no partial cache entry on failure. |
| `tests/io/paths_test.cpp` | 6 | `${VAR}` expansion reporting, the cache directory, the one `__APPLE__` branch. |
| `tests/config/schema_agreement_test.cpp` | 5 | Keeps `schemas/v2/` and the loader from drifting apart (ADR 0022). It has already earned its place: it caught the loader skipping `interventions.types` validation for a baseline-only config. |
| `tests/config/model_loader_test.cpp` | 13 | The model loaders had no test, so they read the internal member names instead of the ones the fitted files use. One of these asserts the internal names are *rejected*. |
| `tests/sim/reproducibility_test.cpp` | 5 | The determinism contract end to end: byte-identical twice, byte-identical at 1 and N threads, with and without an intervention. |

## Totals

| | Tests |
|---|---:|
| Baseline | 471 |
| Ported, or with a counterpart here | 348 |
| **not ported** — out of scope for this run (`KevinHall` 30, `StaticLinear`/income strata 16, PIF 18, FINCH parser 3, nutrient tables 2, other interventions 7) | 76 |
| **not ported by design** — the thing tested does not exist here (`SyncChannel` 9, event bus 15, `CachedRepository` 3, printed summary box 1) | 28 |
| Added here | 110 |
| **This implementation** | **433** |

348 + 76 + 28 = 452, not 471: the difference is the 19 baseline tests that are round-trip halves
of a `to_json`/`from_json` pair where nothing here writes that structure, counted in `JsonParser`
above.

Every **not ported** line is a scope decision recorded in an ADR or in
[docs/backlog.md](backlog.md), not an oversight. The 76 out-of-scope tests are the ones to port
first when the FINCH surface lands — they are the acceptance criteria for it, and 30 of them are
tests the baseline itself has never run (B-11).
