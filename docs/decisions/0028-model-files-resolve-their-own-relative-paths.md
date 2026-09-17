# 0028 — A model file's relative paths resolve against the model file, and its generated factors count as known names

## Status

Accepted, 2026-09-18.

## Context

The `StaticLinear` and `KevinHall` model files name a dozen CSVs between them — the correlation
matrix, the policy covariance, the Box-Cox coefficients, the logistic regression, the income and
physical-activity regressions, the region and ethnicity prevalence, the weight and energy quantiles,
and the height parameters. The baseline resolves every one of them against `config.root_path`, the
directory the *config* was loaded from.

That works upstream, where the config and its model files sit in the same folder. It does not work
here. The converted configs live in `examples/` while the model files they name stay in the
read-only upstream examples ([ADR 0003](0003-read-only-sources-and-out-of-tree-baseline-build.md)),
so a name written inside `hgps_main_examples/KevinHall_FINCH/static_model.json` resolved against
`examples/KevinHall_FINCH/` and came back "cannot open the file for reading" — seven times, for
seven files that are plainly there.

A second problem showed up at the same time. The FINCH static model generates twenty-one **food
group** intakes, `FoodCarbohydrate` and the rest, whose names come from the correlation matrix's
columns. The config's `modelling.risk_factors` list does not mention them: what it declares are the
*nutrients* the Kevin Hall model derives from them. So load-time validation of coefficient names
([ADR 0018](0018-no-swallowing-catch-load-time-validation.md)) rejected the dynamic model for
naming twenty-one factors nobody had heard of — every one of which the static model creates.

## Decision

**A path inside a model file is relative to that model file's directory.** Not to the config's.
The two are the same directory in every upstream example, so this changes nothing there, and it is
the reading that is right in both layouts: the names inside a file are that file's own references.

**A model's generated factors are known names for the models loaded after it.** `RiskFactorModel`
gains `generated_factors()`, which the static linear model overrides to return the correlation
matrix's column order; `load_risk_factor_models` collects it after the static model loads and adds
it to the factor set the dynamic model's names are validated against.

The validation is not weakened. A dynamic model naming a food group the static model does *not*
create is still refused with a located error and a suggestion — which is what
`KevinHallLoader.AFoodGroupNoModelGeneratesIsALoadTimeError` checks.

## Alternatives

- **Copy the model files into `examples/`.** They are 19 MB for France alone, they are somebody
  else's data, and a copy goes stale.
- **Rebase the model files' internal paths too**, as the converter rebases the config's. It would
  mean the converter writing a patched copy of every model file, which is what
  `--policy-scenario` does for the one case that genuinely needs it (audit D-02) and is too much
  machinery for the general one.
- **Resolve against the config and fall back to the model file.** Two places a file might be is
  worse than one, and the failure mode — picking up a stale file that happens to sit beside the
  config — is exactly the kind of thing that is discovered months later.
- **Declare the generated factors in the config.** It would make `modelling.risk_factors` list
  quantities the user does not choose and cannot change, and it would have to be kept in step with
  a CSV's column order by hand.

## Consequences

Both decisions are why the converted `KevinHall_FINCH` example loads and runs. `HLM_France` is
unaffected: its model files name no external CSVs and its factor list is complete.

The path rule is recorded in [docs/deviations.md](../deviations.md) as an internal difference. It
is visible in a diagnostic's file path, which now names the model file's directory rather than the
config's — which is more useful anyway, because that is where the missing file would have to go.
