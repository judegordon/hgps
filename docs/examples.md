# The converted upstream examples

`examples/` holds the six upstream Health-GPS examples, converted from config v1 to
[config v2](../schemas/v2/config.json) by `tools/convert-config`. They are the acceptance tests
for the config format: if the converter, the loader and the upstream examples drift apart,
`tests/config/convert_config_test.cpp` fails.

## What is here, and what is not

**Configs only.** The model CSVs and JSONs — the fitted regressions, the FactorsMean tables, the
policy-effect files — are *not* copied. They stay in `../hgps_main_examples/`, which is read-only,
and each converted config names them by a relative path back there:

```
examples/HLM_France/config.json
  inputs.dataset.name → ../../../hgps_main_examples/HLM_France/France.DataFile.csv
```

That is what `--rebase` does: it rewrites every input path so it resolves to the same file from
the converted config's new location, and leaves `output.folder` and `output.file_name` alone
because where results go is the user's choice, not an input. `ConvertedExamples.StillNameTheUpstreamModelFilesTheyWereRebasedOnto` checks that every one of those paths still resolves to a
real file inside the upstream examples, so a rename upstream is noticed here rather than at the
next run.

**The disease data is not here either**, and is not in this repository at all: it is declared
CC BY-NC-ND, so it is fetched on demand from the release URL each config already names and cached
content-addressed under `~/Library/Caches/healthgps/data/<sha256>/`
([ADR 0011](decisions/0011-data-fetched-not-vendored.md)). The first run of an example downloads
about 100 MB; later runs do not.

## How they were produced

```bash
cd /Users/jude/work/hpgs/hgps_new_rewrite
for e in Dummy_disease_test HLM_France HLM_India KevinHall_FINCH KevinHall_India KevinHall_PIF; do
  src=../hgps_main_examples/$e/new_config.json
  [ -f "$src" ] || src=../hgps_main_examples/$e/config.json
  out/build/release/tools/convert-config --input "$src" --output "examples/$e/config.json" --rebase --check
done
```

Where an example ships both variants, the modern `new_config.json` is the source, because it is
the one that carries `project_requirements`. Three examples have only a legacy `config.json`, so
both converter paths are exercised by this set.

Every conversion prints what it changed. The four recurring notes are:

| Note | Why |
| --- | --- |
| `running.seed converted from a one-element array to a scalar` | v2 requires a seed and there is only ever one ([ADR 0010](decisions/0010-config-v2-and-a-converter.md)) |
| `running.sync_timeout_ms removed` | scenarios run one after the other, so there is nothing to wait for ([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)) |
| `project_requirements … from the documented defaults … (audit finding D-03)` | the legacy configs do not carry the block that most current behaviour is gated on; the defaults written in are the baseline's own struct defaults, checked against `hgps_main/src/HealthGPS.Input/poco.h` |
| `N input path(s) rewritten` | `--rebase`, above |

## What runs today

Measured, not predicted: each row is the result of `healthgps --config examples/<name>/config.json
--dry-run`, which validates the config, the model files, the data index and the disease registry.

| Example | Source | Config loads | Models load | Runs | What stops it |
| --- | --- | :-: | :-: | :-: | --- |
| **HLM_France** | `config.json` | yes | yes | **yes** | — |
| **HLM_India** | `config.json` | yes | yes | **yes** | — |
| **KevinHall_FINCH** | `new_config.json` | yes | yes | **yes** | — |
| KevinHall_India | `new_config.json` | yes | yes | no | a newborn's weight from the quantile curve falls below the configured minimum — **and the baseline fails on it in the same place**, see below |
| Dummy_disease_test | `config.json` | yes | no | no | model family `dummy`, which is a test double rather than a model |
| KevinHall_PIF | `new_config.json` | no | no | no | `population_impact_fraction.enabled` is true |

**Three of the six run end to end**, against one at the end of the previous run:

```
HLM_France       6 diseases, 11 risk factors, cohort of     6,244, 2010–2050
HLM_India       35 diseases, 11 risk factors, cohort of 1,240,613, 2010–2050
KevinHall_FINCH 15 diseases, 34 risk factors, cohort of     6,817, 2022–2032
```

"Runs" means both scenarios, start to finish, with a result file at the end — checked by running
them, not by `--dry-run`. `HLM_India` is the one to be ready for: its cohort is 199 times
`HLM_France`'s, and on the machine [docs/performance.md](performance.md) describes it takes
**42 minutes and 2.4 GiB**, against France's 2.8 seconds and 57 MiB. It produces the same 16,565
rows, because the output is per (year, sex, age band) and not per person.

`KevinHall_India` gets further than any of the three that stop — its config, its model files, its
data and both scenarios' modules all load, and it reports `7 diseases, 17 risk factors, cohort of
14,171 people, 2022–2026` under `--dry-run`. It stops in the first simulated year, and the reason
is the pack's, not this build's.

Two of them are compared against the baseline over many seeds — `HLM_France` and
`KevinHall_FINCH`, at 20 seeds with `simple` active, again at 60, and once more for each of the
other five interventions ([docs/equivalence.md](equivalence.md)). The two India examples are run
through the loader and the engine but not compared, which is this run's scope ruling: what they are for here is to make any
missing disease directory or data inconsistency surface as a located input issue rather than as a
mid-run failure, and they do.

The two that stop, stop at a named missing feature with a pointer to
[docs/backlog.md](backlog.md), never at a crash or a plausible-looking wrong number.

### KevinHall_FINCH and the policy files that are not there

`KevinHall_FINCH/static_model.json` names two files the pack does not contain:

```json
"PolicyCovarianceFile":  { "name": "Finch_residual_policy_covariance.csv" }
"RiskFactorModels": { "policy_coefficients": { "name": "policyeffect_model.csv" } }
```

What it ships is seven variants of each, `S1_`…`S7_`, one pair per modelled policy scenario. The
example is broken as shipped — the baseline fails at load on it — and its sibling
`new_static_model.json` names the `S1_` pair. This is audit finding **D-02**.

The upstream examples are read-only, so the converter resolves it
([ADR 0030](decisions/0030-policy-scenario-selection-for-the-broken-finch-example.md)). Given
`--policy-scenario S1..S7`, defaulting to `S1`, it writes a patched copy of the static model beside
the converted config and points the config at it:

```bash
out/build/release/tools/convert-config \
    --input ../hgps_main_examples/KevinHall_FINCH/config.json \
    --output examples/KevinHall_FINCH_S3/config.json --rebase --policy-scenario S3

warning: the static model's PolicyCovarianceFile names 'Finch_residual_policy_covariance.csv',
         which the upstream pack does not contain; it ships one file per policy scenario and this
         conversion uses 'S3_Finch_residual_policy_covariance.csv' (audit D-02, --policy-scenario)
warning: the static model's RiskFactorModels.policy_coefficients names 'policyeffect_model.csv', …
note:    wrote static_model.S3.json and pointed the config at it
```

The checked-in `examples/KevinHall_FINCH/config.json` is converted from `new_config.json`, which
already names the S1 pair, so it needs no patch. Converting the *legacy* `config.json` does, and
then reveals a second inconsistency in it — it has no `income_stratum_factors_mean` block while its
dynamic model ships one weight-quantile file per income quintile, which this build reports as

```
error [config_bad_value] …/dynamic_model.json (/WeightQuantiles/Male): WeightQuantiles.Male gives
      one curve per quintile, but baseline_adjustments.income_stratum_factors_mean.enabled is
      false, so there are no quintiles to give them to
```

That is the legacy config being older than the model file beside it, and it is why
`new_config.json` is the source for the checked-in conversion.

### HLM_India and the disease registry

`HLM_India` now runs. It is also the example that motivated the disease-registry ruling. Its
`running.diseases` names `pulmonar`. In the 20240624 release the directory is `pulmonar` too, so it
agrees. In the newer `hgps_main_data` checkout the directory has been renamed `pulmonary` while
`Metadata.json` still says `pulmonar`, and pointing the same config at that tree gives:

```
warning [data_disease_not_in_registry] …/diseases/Metadata.json: names disease 'pulmonar', which
        is not in index.json's registry and has no directory; a config copied from this file
        would fail. The canonical spellings are the registry's.
error   [data_disease_not_in_registry] /running/diseases: disease 'pulmonar' is not in this data
        store's registry; did you mean 'pulmonary'?
```

which is the whole point of validating the registry against the tree at load time
([ADR 0012](decisions/0012-disease-naming-pulmonary.md), deviation D-01). The baseline finds out
part-way through configuration, with `Disease code: 'pulmonar' not found.`

### KevinHall_India, which neither implementation can run

Two separate problems, both in the pack.

**Its `new_config.json` contradicts itself, and its own baseline refuses it.** The root says
`"trend_type": "UPFTrend"` while `project_requirements.trend.type` says `"income_trend"`. Running
the baseline on that file gives, before anything loads:

```
Invalid configuration - Deprecated root-level trend_type and/or income_categories are not allowed
when project_requirements is present. Use project_requirements.trend and
project_requirements.income.categories instead.
```

The converter resolves it — the block wins, because it is what the loader reads — but it now says
so at `warning` level, naming both values and which one was used, rather than dropping the root
field with a bland note. `ConvertConfig.SaysSoWhenADeprecatedRootFieldContradictsTheModernBlock`
pins that. Turning a refusal into a silent guess is the thing to avoid here.

**And then it stops in the first simulated year.** With the config loadable, both implementations
run into the same wall:

```
this build:   healthgps: internal error: person 1 (male, age 0) weighs 3.159 kg after the weight
              quantile curve, below the configured minimum of 3.319 kg for 'Weight'. The energy
              balance has produced a body the rest of the model cannot describe; check the model's
              nutrient and energy coefficients
              [kevin_hall/weight_height.cpp:119 …validate_weight…]

the baseline: libc++abi: terminating due to uncaught exception of type hgps::core::HgpsException:
              kevin_hall_model.cpp:1196: Weight (3.108400 kg) is below minimum configured range
              [3.319358, 92.563910] kg during phase 'initialise_weight'.
```

Same place, same cause, weights differing only by the two implementations' random streams. The
pack's `modelling.risk_factors` puts `Weight`'s lower bound at **3.319358 kg**, and its own weight
quantile curve produces newborns lighter than that. The bound and the curve disagree, and no code
change here can reconcile them: raising the curve or lowering the bound would both be inventing a
number for somebody else's model.

Worth noting what each implementation does with it. The baseline throws from inside the dynamic
model, the exception escapes the worker thread, and the process is killed by `std::terminate` —
there is a `ERROR: Exception in dynamic model:` line, and then the run dies. This build reports it
as a diagnosed internal error naming the person, the weight, the bound and the source location,
writes its metadata file, and exits cleanly with a failure code
([ADR 0007](decisions/0007-two-tier-diagnostics.md)).

`KevinHall_India` therefore stays out of the equivalence comparison for a better reason than scope:
there is nothing to compare.

### What KevinHall_India does exercise

It is the only example that uses an income trend — `trend.type: income_trend`, with
`IncomeTrend` equations, an `ExpectedIncomeTrend`, an `IncomeTrendSteps` and an `IncomeDecayFactor`
per risk factor. It is also the only one whose static model is in the *JSON* shape rather than the
CSV-matrix one, so between them the two India examples and FINCH exercise both shapes of
`StaticLinear` and both trend types.

Two things it surfaced, both now handled: its top-level `PhysicalActivityStdDev` is `null` with the
real value in the model block below it, and its `RiskFactorModels` entries carry five income-trend
members the schema had no place for. Both of those are load-time work that runs to completion, and
they are the reason this example is worth converting even though it cannot be run.

## Two things a reader should know before trusting a converted config

1. **`project_requirements` written from defaults is a draft.** For the three legacy configs the
   block did not exist upstream, and what is in `examples/` now is the baseline's default for every
   switch. That is the right *mechanical* answer — it makes this build behave as the current
   baseline behaves when reading the same legacy file — but it is not a statement about what the
   study intended. It is why the converter says so at `warning` level rather than `info`.

2. **A converted config can be more verbose than the upstream run it came from.** `HLM_France`'s
   output now carries `mean_income`, `mean_income_category` and `mean_physical_activity`, because
   the default has `income.enabled` and `physical_activity.enabled` true. The stored 50-simulation
   reference output in `hgps_main_examples/HLM_France_results-50sims/` has 33 columns and none of
   those, because it was produced in January 2024 by a baseline that predates the block. That is a
   reason to compare against a baseline built today, which is what
   [docs/equivalence.md](equivalence.md) does, and not against the stored CSVs.

## Warnings you will see and should not worry about

Running `HLM_France` reports 73 warnings. Seventy-two are `data_missing_file`: relative-risk files
that the release does not contain, each one naming the disease, the risk factor and the sex, and
each meaning *that effect is off*. The baseline loads the same data and says nothing. The
seventy-third is:

```
warning [csv_bad_value] …/France.DataFile.csv:551 (Age): 80 value(s) in this integer column have
        a fractional part and were truncated toward zero, as the baseline's std::stoi does; the
        first is '32.171900537047'
```

The file declares `Age` as an integer and 80 of its 40,000 rows are not; its own `Age1` column
carries `floor(Age)`, so truncation is what the data means. The arithmetic matches the baseline
exactly — only the silence is gone. See [docs/deviations.md](deviations.md).

`KevinHall_FINCH` reports **855** and `HLM_India` **1,183**, for the same reason and in the same
proportion: they select fifteen and thirty-five diseases against a release that carries a fraction
of the relative-risk files those diseases could have. Each line names a disease, a risk factor and
a sex, and each means that one effect is off. A count in the hundreds is a property of the data
release, not of the config — but it is worth reading the list once for a new study, because a
missing file is indistinguishable from an effect somebody meant to switch off.

## One more thing worth knowing about the baseline

Running the baseline on `KevinHall_FINCH` is not reliable. Over the many runs
[docs/equivalence.md](equivalence.md) reports — the same binary, the same config, the same seed —
some exit on a signal and then succeed when re-run unchanged, and three different signals have been
seen: `SIGTRAP`, `SIGSEGV` and `SIGABRT`. That is the concurrency defect the audit recorded (B-01,
B-02: two scenario threads, and a repository populated lazily from inside a parallel loop, behind a
lock-free fast path that races a concurrent insert).

The equivalence harness retries a baseline run up to three times for this reason, and prints every
retry it makes, so the flake is visible rather than smoothed away. It is not something a comparison
against the baseline can fix, and it is the strongest single argument for the sequential-scenario
design this implementation uses ([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)).
