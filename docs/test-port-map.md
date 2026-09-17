# Test port map

How the baseline's 471 tests map onto this implementation's suite, suite by suite. It exists so
that "the tests were ported" is a checkable claim rather than an assertion, and so that a reader
can find the descendant of any baseline test.

Columns: the baseline suite and its test count, where those tests live here, and what changed.
`intent` means the test asserts the same property against a different API; `value` means the
expected numbers are carried over unchanged; `fixed` means the expectation was changed because the
baseline test encoded one of the audit's findings — each of those names the finding ID in a comment
in the test.

Status: **in progress**. The table is updated as each module lands; the totals at the bottom are
what `docs/SUMMARY.md` reports.

## Ported

| Baseline suite | Tests | Here | Notes |
|---|---:|---|---|
| `TestCore` | 18 | `tests/core/core_test.cpp` | value. Plus one added case for case-insensitive column lookup and insertion order. |
| `TestCore_Identity` | 12 | `tests/core/identifier_test.cpp` | value, plus two added: equality and ordering must agree (**fixed**, B-04), and the hash is for bucketing only. |
| `TestCore_Interval` | 10 | `tests/core/interval_test.cpp` | value, plus inverted bounds and a non-numeric delimiter (the baseline parsed a `string_view`'s `.data()`, which reads past the field). |
| `TestCore_MathHelper` | 7 | `tests/core/math_helper_test.cpp` | value, plus an unequal-values case. |
| `TestCore_Array2D` | 15 | `tests/core/array2d_test.cpp` | value. |
| `TestCore_UnivariateSummary` | 10 | `tests/core/univariate_summary_test.cpp` | value — the moment recurrence's expected values are carried over unchanged, which is what pins the port. Plus `clear` and undefined-moment cases. |
| `IncomeCategoryLayout` | 4 | `tests/core/income_category_layout_test.cpp` | value. `HgpsException` becomes `std::invalid_argument`: `core` has no dependency on the diagnostics module. Plus an added test that the strata are ordered low to high, which is the property income sampling relies on (B-05). |
| `SHA256` | 3 | `tests/io/sha256_test.cpp` | value — the baseline's expected digests are kept, against our own implementation rather than OpenSSL's. Plus the FIPS 180-4 vectors, block-boundary lengths and single-use enforcement. |
| `ConfigParsing` (10) + `ConfigParsingFixture` (22) + `ConfigSchemaExpanded` (47) + `ConfigLegacyFields` (3) | 82 | `tests/config/config_loader_test.cpp`, `tests/io/json_test.cpp` | intent. The baseline's `get`/`get_to`/`rebase_valid_path_to` helper tests become `io::JsonCursor` tests, because accumulated diagnostics replace throw-per-problem; its section loaders (`load_input_info`, `load_modelling_info`, `load_running_info`, `load_output_info`, `load_interventions`, `check_version`) each have a counterpart. **fixed**: `seed` is required and scalar (B-06); `output.file_name` is used exactly as configured (B-08); an undefined `${VAR}` is an error (N-17); `project_requirements` is required (D-03); `sync_timeout_ms` is rejected (ADR 0009). |
| `ConfigurationPIF` | 2 | `tests/config/config_loader_test.cpp` | intent — PIF config is reserved and rejected at load in this build (ADR 0021), so the two struct tests become one reservation test. |

## Added here, with no baseline counterpart

| Here | Why |
|---|---|
| `tests/core/chars_test.cpp` | B-03: eleven `<cctype>` calls on a signed `char`. Exercises the whole byte range. |
| `tests/core/matrix_test.cpp` | The Cholesky decomposition and matrix-vector product are ours rather than Eigen's (ADR 0023). |
| `tests/core/parallel_test.cpp` | D5: a fixed-order reduction, bit-identical at any thread count (N-7). |
| `tests/random/engine_test.cpp` | The baseline has no RNG test at all, which is how B-06, B-07, B-14 and B-15 all survived. |
| `tests/random/parallel_guard_test.cpp` | D3: an RNG draw inside a parallel region throws with a source location (N-4). |
| `tests/random/categorical_test.cpp` | D4: ordered-only sampling (B-05). |
| `tests/random/modulo_bias_test.cpp` | The regression test for the earlier rewrite's `% range`. |
| `tests/diagnostics/diagnostics_test.cpp` | The two-tier diagnostics design has no baseline counterpart. |
| `tests/io/csv_reader_test.cpp` | The baseline's CSV path has no unit test; every case here is a located diagnostic replacing a coloured `std::cout` line. |
| `tests/io/data_source_test.cpp` | Checksum requirement, content-addressed cache, no partial cache entry on failure. |
| `tests/io/paths_test.cpp` | `${VAR}` expansion reporting, cache directory, the one `__APPLE__` branch. |
| `tests/io/json_test.cpp` | Located JSON diagnostics. |
| `tests/config/schema_agreement_test.cpp` | Keeps `schemas/v2/` and the loader from drifting apart (ADR 0022). It has already earned its place: it caught the loader skipping `interventions.types` validation for a baseline-only config. |
