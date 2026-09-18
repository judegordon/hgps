# 0031 — Each intervention is compared on its own, and FINCH borrows its definitions

## Status

Accepted, 2026-09-18.

## Context

There are six intervention scenarios. Until this run the equivalence harness activated one of them,
`simple`, and compared that. `simple` is a flat shift with no memory and no draws of its own. The
other five are not: each has its own exposure rule, `dynamic_marketing`, `physical_activity` and
`food_labelling` draw a random number per person per year, and two of them keep a book of who they
have already affected. A comparison that never activates them says nothing about any of it, and
[ADR 0029](0029-one-banded-intervention-shape.md) put all five behind one shared shape — which is
exactly the kind of refactor that can be uniformly wrong.

Two obstacles.

**The reference is keyed by the config.** Changing which intervention is active changes the config,
so a naive `--intervention` flag would compare this build against a stored baseline reference that
was produced with a different policy — silently, and catastrophically.

**`KevinHall_FINCH` ships one intervention and it does nothing.** Its `running.interventions.types`
has a single entry, `simple`, whose `impacts` list is empty. The FINCH policy is elsewhere:
`policy_start_year` is 2024 and the `StaticLinear` model applies the S1 policy-effect coefficients
and residual policy covariance to the intervention scenario from that year. So on FINCH there is
nothing in the `interventions` block to compare, and the five age-banded policies have never been
activated against the FINCH surface at all — which, at the time this was written, looked like a gap
in the evidence. It turned out to be a property of the baseline instead; see below.

## Decision

**`--intervention NAME` runs the whole comparison with that policy active in both
implementations.** The stored reference is keyed by the SHA-256 of the derived config with the seed
removed, and the active intervention is part of that config, so each policy hashes to its own
reference file automatically. Nothing had to be added to make that safe; it follows from the key
already chosen.

**FINCH's missing definitions come from a file in the harness**,
`tests/equivalence/interventions/KevinHall_FINCH.json`, merged into the derived config for both
implementations when the example does not declare the policy being asked for. Its contents are
HLM_France's five upstream definitions **verbatim**, with two substitutions and nothing else:

- the active period becomes 2025 onwards, which is what FINCH's own `simple` declares, because
  France's definitions run to 2050 and the FINCH horizon ends in 2032;
- the risk factor `Energy` becomes `EnergyIntake`, which is FINCH's name for the same quantity.

France's coefficients are meaningless for Finland, and that is not what they are for. Both
implementations are given the identical definition and the question asked is whether they apply it
identically.

## What running it then showed, which is not what this ADR first assumed

This ADR was written expecting that an energy impact on the FINCH surface would propagate through
the Kevin Hall energy balance into weight and BMI. **It does not, because nothing on that surface
consults the policy at all.** In the whole baseline, `Scenario::apply` has one call site,
`dynamic_hierarchical_linear_model.cpp:110`; neither `static_linear_model.cpp` nor
`kevin_hall_model.cpp` calls it. The six intervention scenarios reach the HLM surface and nothing
else. This build has the same single call site, and running FINCH with `marketing` active gives
output byte-identical to running it with `simple` active, in both implementations.

So the FINCH intervention runs establish something narrower than intended: that both
implementations agree these policies are inert there. That is still worth having — it is what would
catch an implementation that wired `apply` into a model the baseline leaves alone — but the five
policies' own rules are exercised against the baseline on `HLM_France` only.

The decision stands unchanged; only the expectation of what it would show was wrong.
[docs/equivalence.md](../equivalence.md) has the measurement, and
[docs/backlog.md](../backlog.md) carries the upstream question of whether a policy *should* reach
the Kevin Hall surface.

## Alternatives

- **Compare the five on HLM_France only.** Cheap, and it is where the definitions are real — but it
  leaves the interaction between a policy and the Kevin Hall energy balance untested, which is the
  half that is new. Both are done; this would have been half.
- **Invent FINCH-appropriate coefficients.** Then the comparison would rest on numbers with no
  provenance, and somebody would eventually mistake them for a Finnish policy. Borrowing France's
  and saying so is more honest than inventing Finland's.
- **Edit the upstream example to add the five.** Forbidden by
  [ADR 0003](0003-read-only-sources-and-out-of-tree-baseline-build.md), and it would make the
  checked-in example differ from what upstream ships.
- **Write them into `examples/KevinHall_FINCH/config.json`.** That file is the converted upstream
  example and is checked by `ConvertedExamples`; putting harness-only policies in it would make the
  example a fiction. The definitions belong to the comparison, so they live with the comparison.
- **One combined run with all six active.** Not possible: one scenario is active per run, which is
  what makes the baseline-versus-intervention difference attributable to a single policy.

## Consequences

Twelve equivalence runs rather than two: two examples × (`simple` plus five policies), each at 20
seeds, plus the 60-seed confirmation for the two primary runs. That is a couple of hours of
machine time and it is run deliberately rather than in `scripts/check.sh`, which stays at the two
primary comparisons against the checked-in references.

`intervention_overlay` in `tests/equivalence/run.py` carries the reasoning above in its docstring,
so the substitution cannot be found later without its justification.
