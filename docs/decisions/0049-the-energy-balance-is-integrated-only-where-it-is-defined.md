# 0049 — The energy balance is integrated only as far as the model is defined

## Status

Accepted, 2026-09-19. Closes the tenth run's [docs/backlog.md](../backlog.md) item 2. Applies
[ADR 0041](0041-deliberate-deviations-are-switchable.md)'s rule to the deviation it creates
(**B-29**), and is the reason [ADR 0050](0050-no-output-carries-a-number-that-cannot-exist.md)'s
invariant check exists as a second line rather than as the only one.

## Context

Running `KevinHall_FINCH` two hundred times found a seed this build refused and the baseline
completed: at seed 80, person 1222 weighs **−1.702×10²⁸³ kg** in simulated 2031. The ninth run
could not say whether that was ours. [docs/findings/seed-80.md](../findings/seed-80.md) is the
trace that answers it, and the answer has three parts.

**It is not the weight that fails.** The weight is where the bound check notices. The first value
in the trajectory that cannot exist is a **body fat mass of −2.224 kg**, one year earlier.

**It is a pole, not an accumulation of error.** The yearly step is the closed-form solution of a
two-compartment relaxation, and one of the relaxation's own coefficients is

```
p = C / (C + F),     C = 10.4 · rho_lean / rho_fat = 2.001012658227848 kg
```

which has a pole at `F = −C`. Person 1222 lands at −2.232 kg, 0.23 kg past it. Everything inverts
at once: `p` becomes −8.65, the determinant of the steady-state pair goes from +2.0×10⁶ to
−4.9×10⁷, and the time constant from +155 days to **−0.559 days**. A negative time constant turns
the year's step from a relaxation *toward* the steady state into an exponential flight *away* from
it, and one year of it is `exp(+652.5) = 2.29×10²⁸³`.

**It is the model's, not this implementation's.** The two energy balances are the same expressions
with the same ten constants, and both floor a negative body fat estimate at *initialisation* — with
a comment saying it is to keep the partition coefficient finite — and neither floors it in the
yearly update, which is the only place integration can take it there. The baseline's own
statements, copied into a program that shares no code with this project and fed the state this
build traced, return **−2.2240198893612395** and **−1.7019180456941172e+283** against this build's
−2.2240198893612368 and −1.7019180456941046e+283.

So it is [ADR 0041](0041-deliberate-deviations-are-switchable.md)'s third case: an instability the
fitted model admits, which the baseline does not mask and does not guard.

## Decision

**The year's step is integrated only as far as the model is defined: to the instant body fat
reaches zero, and no further.**

The step is `F(t) = F* − (F* − F₀)·exp(−t/tau)`. When `F* < 0 < F₀` the trajectory crosses zero at
some `t* < 365`, and

```
exp(−t*/tau) = F* / (F* − F₀)
```

— the same exponential the step is already written in, so no logarithm is taken and no new
constant is introduced. Lean tissue follows its own half of the same solution to the same instant.
The person ends the year with **no body fat** and the lean tissue they had when they ran out.

Three properties make this a guard rather than a modelling change:

- **It is the model's own trajectory**, evaluated at the edge of the model's own domain. Nothing
  about the physiology is invented; what is refused is evaluating a solution outside the region
  where the equation it solves has coefficients.
- **It is self-healing.** At `F = 0`, `p = C/C = 1` exactly, so the steady state is exactly zero
  and every later year leaves the person there. There is no pole to approach again, no warning
  every year afterwards, and no drift.
- **It is bounded in the other direction too.** A time constant that is not positive and finite
  is not a relaxation at all, and there is then no instant inside the year at which the model
  describes anything. That step is not taken: the composition is left where the year started.
  Nothing in a shipped example reaches this branch — the determinant is positive for every body
  with fat at every physical activity level a person can have — and it exists because a guard
  that only handles the case that has happened is not a guard.

**Every occurrence is counted and located.** An `EnergyBalanceBodyFatBounded` metric in the
results, and a **located warning in the run manifest** naming the scenario, the year, the person,
what the step would have produced, what it produced instead, and what the steady state was. The
manifest's `warnings` object is present and empty in an ordinary run, like `baseline_compat` and
`perturbation` beside it, because an absent key reads as "nobody looked".

**It is switchable: `--baseline-compat B-29`.** With the flag on, the energy balance integrates
straight through zero exactly as the baseline does, runaway and all. That is ADR 0041's rule and
it is not a formality here: it is how "the guard changes nothing on 499 of 500 seeds" is a
measurement.

## Why not the alternatives

**Clamp the weight instead.** The obvious move, and the one [docs/backlog.md](../backlog.md) item
2 suggested before the mechanism was known: `validate_weight` already treats *above* the
configured maximum as a counted metric rather than an error, so the shape exists. It is wrong for
two reasons. It acts two years and one pole too late, on a quantity that is a symptom — by 2031
the person's body fat, lean tissue and weight are all nonsense and clamping the weight would leave
a body whose compartments do not sum to it. And it would keep the **2029 and 2030** rows, in which
this person's own weight is already 77 kg and then 56 kg on a trajectory to nowhere, averaged
invisibly into a band of forty other men. Nothing in the output says so: see *what the aggregate
does not show* in [docs/findings/seed-80.md](../findings/seed-80.md).

**Floor body fat at `0.2 · weight`, as the initialiser does.** Consistent-looking, and it invents
11 kg of fat for a person who has just run out. The initialiser's floor is applied to a *regression
estimate* that came out negative for a very light body, which is a different thing from a
trajectory that has been integrated to zero.

**Freeze the whole year — leave fat and lean where they started.** Simpler to explain and simpler
to test, and it says the year did not happen, which is false: the person was starving through it.
The crossing costs one division and is the model's own answer for the part of the year that is
inside the domain.

**Refuse the run, as this build did before.** Honest, and it is what the backlog said to keep
until the mechanism was known. Now that it is known, refusing is refusing an input the model
admits: about one seed in five hundred of a shipped example, and — because the baseline's bound
check only *warns* above the configured maximum — an input on which the baseline would exit zero
had the runaway gone the other way. A guard with a stated bound and a named warning is strictly
more useful than a stopped run, and the run still stops if anything gets past it
([ADR 0050](0050-no-output-carries-a-number-that-cannot-exist.md)).

**Report it upstream and change nothing.** The report is being written either way
([docs/upstream-reports.md](../upstream-reports.md)), and it is the more important half. But "we
knew the model could write 10²⁸³ kg into a results file and left it" is not a position this project
can hold while it is also the implementation people would run.

## What it costs

One comparison per adult per year — `fat < 0.0` on a value the step has just computed — and a
branch not taken on 499 of the 500 seeds measured. No timing claim is made: the machine that
would have measured it was running two five-hundred-seed censuses at the time, and a number
taken then would say more about the contention than about the branch.

**And it costs a deviation.** The two implementations now differ on any draw that reaches the
boundary, which is what B-29 exists to measure. On `KevinHall_FINCH` the measurement is that the
difference is **exactly nothing on 499 of 500 seeds** — byte-identical CSVs with the guard on and
off — and on seed 80 it is the difference between a results file and no results file at all.

## Consequences

- `docs/deviations.md` gains **B-29**, with its flag and its evidence.
- The run manifest gains a `warnings` object, always present. The result CSVs do not change, which
  is the contract [ADR 0034](0034-a-run-manifest-beside-the-results.md) set and which the stored
  equivalence references depend on.
- `RuntimeContext` gains a warning channel beside its metrics, which is general: it is the place a
  future guard's located warning goes, and `Runner::Outcome` carries the union over both scenarios.
- The energy balance's step is a static, public `bounded_step`, so every branch of it is a unit
  test rather than a seed somebody has to find.
