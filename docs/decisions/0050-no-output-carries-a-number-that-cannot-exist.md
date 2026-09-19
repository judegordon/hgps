# 0050 — No output carries a number that cannot exist

## Status

Accepted, 2026-09-19. Complements
[ADR 0049](0049-the-energy-balance-is-integrated-only-where-it-is-defined.md): that one fixes the
defect this run found, this one is the reason the *next* one cannot ship silently. Creates
deviation **B-30** under [ADR 0041](0041-deliberate-deviations-are-switchable.md).

## Context

The seed-80 investigation ([docs/findings/seed-80.md](../findings/seed-80.md)) found a body fat
mass of −2.224 kg that nothing looked at, a weight of −1.7×10²⁸³ kg that something finally did,
and two simulated years written to a results file in between with a contaminated cohort in them.
ADR 0049 closes that path. It does not close the class.

Reading the code for what *would* have caught it found something worse than the gap. The analysis
module — the last thing a value passes through before it becomes a mean in a results file —
contains this, ported faithfully from the baseline (`analysis_module.cpp:385-391`):

```cpp
const double factor_value =
    value == person.risk_factors.end() || std::isnan(value->second) ? 0.0 : value->second;
by_gender.at(person.gender) += factor_value;
```

**A value that is not a number is counted as zero, and the sum is still divided by the whole head
count.** Three people weighing 80 kg, one of them `NaN`, report a mean weight of 53.3 kg. Nothing
in the output, the log or the exit code says a substitution happened. And `std::isnan` is false
for `±inf`, so an infinity is not looked at at all: it goes into the sum and takes the whole
band's mean with it.

That is the shape [ADR 0041](0041-deliberate-deviations-are-switchable.md)'s second case names
exactly — "the baseline has the same instability but masks it (a clamp, a silent reset, **a NaN
that gets overwritten**)" — and it converts every defect upstream of it, including the one this
run fixed, from a run that fails loudly into a results file that is quietly wrong.

## Decision

**The engine never writes a non-finite or physically impossible value to any output. On detection
it stops the run with a located internal error naming the person, the year and the term.**

The check lives in the analysis module's per-person, per-factor accumulation, which is chosen for
three reasons and not for convenience:

- it is the **last** place a value passes through before it becomes output;
- it is the last place that still knows **whose** value it is, which is what makes the error
  locatable rather than a statement that some mean is wrong;
- it is a loop that already loads every value, so the check is `std::isfinite` and two
  comparisons on a double already in a register.

**"Physically impossible" is stated, per factor, and deliberately not the configured range.**
The configured `range` in a config document is a *modelling* bound: `KevinHallModel` already
treats a body above its maximum as implausible-but-describable and counts it rather than refusing
the run, which is what the baseline does and what a heavy cohort needs. These bounds are the ones
at which a number stops being a measurement of anything:

| Factor | Describable | Why that and not something tighter |
|---|---|---|
| `Weight` | 0.001 – 1000 kg | the heaviest human reliably recorded weighed about 635 kg |
| `Height` | 1 – 300 cm | the tallest stood 272 cm |
| `BMI` | 0.001 – 1000 | a consequence of the two above |
| `EnergyIntake` | 0 – 10⁶ kJ/day | negative energy is impossible; the ceiling is about 240,000 kcal |
| everything else | finite | — |

**A factor with no stated bound is checked for finiteness only, and that is deliberate.** A
nutrient intake has no ceiling this project can defend, and inventing one would make the guard a
modelling opinion rather than an invariant. The bounds above are all far outside any configured
range in any shipped example, so crossing one is evidence of a defect and never of an unusual
draw.

**An absent factor is not an impossible value.** Not every project assigns every declared factor,
and a factor nobody has contributes nothing to the sum — which is what it has always done.

**It is switchable: `--baseline-compat B-30`.** With the flag on, a `NaN` becomes a zero and the
run carries on, exactly as the baseline does, so the difference the guard makes is measurable
rather than asserted.

## Why a check and not a repair

A guard that *corrects* an impossible value has to choose a correction, and every choice is a
modelling decision made in the wrong place by the wrong code. ADR 0049's bound is defensible
because it is the model's own trajectory at the model's own boundary; there is no equivalent for
"this person's BMI is `NaN`". The honest action is to stop, name the person and the term, and let
whoever owns the model decide what should have happened.

That is also why this is an *error* and not a warning, where ADR 0049's guard is a warning. The
difference is whether anything downstream knows what the right answer is. When something does,
bound it and record it; when nothing does, stop.

## What it costs

A run that would have produced a wrong number now produces none. That is the intended trade and
it is worth writing down as a cost rather than as a feature: a long run that dies in its last year
over one person has lost the other years, where before it would have written them with a bad mean
in one column. The mitigation is that the error is located — the person, the year and the term are
in the message — so the next run can be the diagnostic one.

The accumulators became a flat vector on the way — built once a year from the map that used to
be walked once per person per year — so the check's two comparisons are paid for by not
re-walking a `std::map` node by node for every person. No timing claim is made either way: the
machine that would have measured it was running two five-hundred-seed censuses at the time.

## Consequences

- `docs/deviations.md` gains **B-30**, with its flag.
- [docs/upstream-reports.md](../upstream-reports.md) gains report 7, because the substitution is
  still there in the baseline and a guard in a reimplementation does nothing for anybody running
  it.
- `tests/model/analysis_invariants_test.cpp` is the test the ruling asked for: a deliberately
  injected non-finite value, caught and located, plus the bounds, the finiteness-only case, the
  absent-factor case and the flag.
- The invariant is stated where a reader will meet it — [docs/design.md](../design.md) — as a
  property of the engine rather than as a feature of one module.
