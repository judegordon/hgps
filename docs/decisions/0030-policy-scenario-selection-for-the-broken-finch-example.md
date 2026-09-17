# 0030 — The converter selects a policy scenario for the FINCH example, rather than the example being edited

## Status

Accepted, 2026-09-18. Resolves audit finding D-02.

## Context

`hgps_main_examples/KevinHall_FINCH/static_model.json` names two files that do not exist:

```json
"PolicyCovarianceFile":  { "name": "Finch_residual_policy_covariance.csv", … }
"RiskFactorModels": { "policy_coefficients": { "name": "policyeffect_model.csv", … } }
```

What the pack ships is seven variants of each — `S1_Finch_residual_policy_covariance.csv` through
`S7_`, and `S1_policyeffect_model.csv` through `S7_` — one pair per modelled policy scenario. The
example is broken as shipped: running the baseline on that config fails at load. Its sibling
`new_static_model.json` names the `S1_` pair, which is evidently what the pack's authors meant.

The upstream examples are read-only
([ADR 0003](0003-read-only-sources-and-out-of-tree-baseline-build.md)), so the fix cannot be to
edit the file. And the choice of scenario is a real one — the seven pairs are seven different
policies, and which you want is not something a tool should guess silently.

## Decision

`tools/convert-config` takes **`--policy-scenario S1..S7`**, defaulting to **S1**.

When the static model the converted config names has a policy file that does not exist while the
scenario-prefixed one does, the converter writes a **patched copy of the static model** beside the
converted config — `static_model.S1.json` — with the two names prefixed, and points the config at
the copy. The copy's other file references are made absolute, so it still finds the upstream CSVs
from its new home.

Two notes are printed, one per patched member, each naming the file that was asked for, the file
that was used, and the finding. Nothing happens silently, and nothing happens at all for the five
examples whose policy files exist.

An unknown scenario name is an error listing the seven.

## Alternatives

- **Convert from `new_config.json` and ignore `static_model.json`.** This is what
  `examples/KevinHall_FINCH/config.json` in fact does, because `new_config.json` is the modern
  variant and carries `project_requirements`. But it leaves the legacy config unconvertible, and
  the legacy config is the one a reader coming from the upstream repository will try first.
- **Default to `S1` with no option.** Hides the choice. Seven scenarios exist because somebody
  wants to run all seven.
- **Patch the reference at load time**, in the model loader, rather than at conversion time. It
  would put a data-pack quirk into the program and make the loaded model differ from the file it
  names.
- **Edit the upstream example.** Forbidden by ADR 0003, and it would be the wrong fix anyway:
  upstream should be told, not patched locally.

## Consequences

Converting the legacy FINCH config now produces a config that loads, and says why it had to. The
`--policy-scenario` option is the only way to run the other six scenarios, which nothing could do
before.

Recorded in [docs/examples.md](../examples.md) and [docs/deviations.md](../deviations.md). The
converted example checked into `examples/KevinHall_FINCH/` still comes from `new_config.json`, so
the S1 patch is exercised by the converter's own test rather than by the checked-in example.
