# 0035 — An intervention the configured model would ignore is refused at load time

## Status

Accepted, 2026-09-18.

## Context

`Scenario::apply` is the call that offers a person and one of their risk factors to the active
intervention policy. In the whole upstream baseline it has **one call site**,
`dynamic_hierarchical_linear_model.cpp:110`. Neither `static_linear_model.cpp` nor
`kevin_hall_model.cpp` calls it. This build has the same single call site,
`model/riskfactor/hlm_model.cpp:281`.

The consequence is that on the `StaticLinear`/`KevinHall` model surface **every intervention scenario
is inert**. Running `KevinHall_FINCH` with `marketing` active produces output byte-identical to
running it with `simple` active, in both implementations — which is how the previous run found this,
by checking that a comparison was testing what it claimed to
([docs/equivalence.md](../equivalence.md), [ADR 0031](0031-comparing-one-intervention-at-a-time.md)).

So today a config can name `food_labelling`, be accepted, run to completion, report success, and
produce a baseline and an "intervention" future that are the same numbers. That is the shape of thing
somebody eventually puts in a paper.

Two separate questions follow, and they have different owners.

**Should a policy reach that surface?** Not this project's call. It may well be deliberate: that
surface has its own policy mechanism — `modelling.policy_start_year` and the S1 policy-effect
coefficients — which does work and is better founded than an age-banded constant shift, and all four
Kevin Hall examples upstream ship their intervention with an **empty** impact list, which reads like
people who knew. [docs/backlog.md](../backlog.md) item 7 carries the question, tagged `needs-ruling`,
with the evidence on both sides.

**Should a config that asks for it be accepted?** That one is this project's call, and the answer is
no.

## Decision

**A config whose active intervention declares impacts that the configured dynamic model would never
apply is rejected at load time, with a located issue naming both.** The issue says which intervention,
how many impacts it declares, which dynamic model file, and what to do instead.

**An intervention with an empty impact list is accepted, with a warning.** This is the distinction the
whole decision turns on. `KevinHall_FINCH`, `KevinHall_India`, `KevinHall_PIF` and
`Dummy_disease_test` all select an intervention whose `impacts` list is `[]`. Filling it would change
nothing, and shipping it empty is upstream saying "no policy here" in the only way the format allows.
An empty impact list is a well-defined no-op; it is not a claim the run fails to honour. Rejecting it
would make four of the six examples unloadable, including one of the two equivalence references, to
prevent a mistake nobody is making.

The warning still says the thing worth saying: this intervention declares no impacts, the configured
dynamic model would not apply them if it did, and so this run's intervention arm differs from its
baseline arm only through `policy_start_year`, if that is set.

**Whether a model applies the policy is a property the model answers, not a name the loader
matches.** `model::RiskFactorModel::applies_the_active_scenario()` returns false by default and is
overridden to true by exactly one class: `DynamicHierarchicalLinearModel`, whose `update_exposure` is
the one place in this build that calls `Scenario::apply`. Matching on `ModelName == "kevinhall"` would
have been shorter and would drift the moment a fifth model family appeared; a virtual function next to
the call site is checked by the compiler and read by whoever adds the next family.

The default is **false**, deliberately. A new model family does not consult the policy until somebody
writes the call and flips the flag, so the failure mode of forgetting is a refused config rather than
a silent no-effect run.

**The check runs where both facts are known.** The intervention comes from the config document; which
dynamic model is configured is only known once its file has been read, which happens in
`config::models::load_risk_factor_models`. So the check lives there, and it is still load-time
validation: it happens before the first random draw, it accumulates into the same report as everything
else, and `--dry-run` catches it ([ADR 0018](0018-no-swallowing-catch-load-time-validation.md)).

## Alternatives

- **Implement the intervention on the Kevin Hall surface.** Explicitly out of scope for this run, and
  for a reason that outlives the run: the wiring is two lines, and deciding **where** in the food →
  nutrient → energy → body chain an age-banded impact applies is a modelling decision nothing in the
  data answers. An age-banded shift to `EnergyIntake` is a different intervention from the same shift
  to `FoodCarbohydrate`. Guessing would put an invented number inside somebody else's fitted model.
- **A warning rather than an error, in both cases.** The run would go on producing a
  no-effect "intervention" future, and the warning would scroll past. The whole point is that the
  output of such a run is indistinguishable from a correct one, so the time to stop is before it
  exists.
- **Refuse any `active_type_id` on that surface, empty impacts or not.** Cleanest rule to state, and
  it makes four of the six examples unloadable — including `KevinHall_FINCH`, whose stored equivalence
  reference is one of the two things this project's headline result rests on. The cost is real and the
  benefit is zero, because an empty impact list has no effect to lose.
- **Check at run time, on the first person.** By then the files are open and a random stream has been
  drawn from. Everything checkable before the run is checked before the run.
- **Match on the model's `ModelName` string in the loader.** Shorter today. It puts the knowledge of
  which models consult the policy somewhere other than the models, so the next model family is wrong
  by default and silently.

## Consequences

**The five age-banded policies can no longer be compared on the FINCH surface by this build.** The
previous run activated each of them on `KevinHall_FINCH` and established that both implementations
agreed they did nothing ([ADR 0031](0031-comparing-one-intervention-at-a-time.md)). Those runs used a
harness overlay that supplies non-empty impacts, so this build now refuses them — correctly, and at
the cost of a comparison. What that comparison showed is unchanged and is still recorded; it cannot be
re-run here without the overlay being changed to declare no impacts, at which point it would be
comparing nothing. [docs/equivalence.md](../equivalence.md) says so where the FINCH intervention
results are reported.

`HLM_France` and `HLM_India` are unaffected: their dynamic model is `EBHLM`, which is the one that
applies the policy.

The error message names `modelling.policy_start_year` as the alternative, because on the surface where
this error fires that is the mechanism that works, and a diagnostic that says only "no" is worth less
than one that says what instead.
