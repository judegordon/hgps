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
| HLM_India | `config.json` | no | (HLM, would load) | no | `interventions.active_type_id` is `food_labelling`; this build implements only `simple` |
| Dummy_disease_test | `config.json` | yes | no | no | model family `dummy` |
| KevinHall_FINCH | `new_config.json` | yes | no | no | model families `staticlinear` and `kevinhall` |
| KevinHall_India | `new_config.json` | yes | no | no | model families `staticlinear` and `kevinhall` |
| KevinHall_PIF | `new_config.json` | no | no | no | `population_impact_fraction.enabled` is true |

So one example runs end to end: **HLM_France**, the reference example, which is what
`tests/equivalence/` compares against the baseline.

```
healthgps: 1 run of 2010–2050, cohort 6244, seed 123456789, 6.7s
```

Every other example stops at a named missing feature with a pointer to
[docs/backlog.md](backlog.md), never at a crash or a plausible-looking wrong number. The scope for
this run was the HLM surface end to end plus the `simple` intervention; `StaticLinear`,
`KevinHall`, the other five interventions and PIF are the backlog's top items, and the examples are
here so that finishing each one has an acceptance test waiting for it.

### The two near misses

**HLM_India** is blocked only by its choice of intervention. Its models are HLM, and with
`active_type_id` set to `null` it validates against the release data: *35 diseases, 11 risk
factors, cohort of 1,240,613 people, 2010–2050*. It is not checked in that way, because editing a
config to make it pass is not a conversion.

It is also the example that motivated the disease-registry ruling. Its `running.diseases` names
`pulmonar`. In the 20240624 release the directory is `pulmonar` too, so it agrees. In the newer
`hgps_main_data` checkout the directory has been renamed `pulmonary` while `Metadata.json` still
says `pulmonar`, and pointing the same config at that tree gives:

```
warning [data_disease_not_in_registry] …/diseases/Metadata.json: names disease 'pulmonar', which
        is not in index.json's registry and has no directory; a config copied from this file
        would fail. The canonical spellings are the registry's.
error   [data_disease_not_in_registry] /running/diseases: disease 'pulmonar' is not in this data
        store's registry; did you mean 'pulmonary'?
```

which is the whole point of validating the registry against the tree at load time
([ADR 0012](decisions/0012-disease-naming-pulmonary.md), deviation D-01). The baseline finds
out part-way through configuration, with `Disease code: 'pulmonar' not found.`

**KevinHall_FINCH** converts with 15 rebased paths, including the five income-quintile FactorsMean
strata, and its config loads — including the `income_stratum_factors_mean` block, which no test
data in this repository exercises yet. Only the model families are missing.

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
