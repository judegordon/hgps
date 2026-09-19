# Seed 80 of `KevinHall_FINCH`: where the energy balance diverges, and whose it is

The ninth run found that this build **refuses** seed 80 of `KevinHall_FINCH` and the baseline
completes it ([docs/SUMMARY.md](../SUMMARY.md), *One seed in two hundred*;
[docs/backlog.md](../backlog.md) item 2). What it could not say was whether the instability was
ours. This document is the trace that answers it.

**The answer: it is the model's, and both implementations compute it identically.** The
divergence is one person, and its first non-physical value is not the −1.7×10²⁸³ kg weight the run
stops on — that is two years downstream. It is a **body fat mass of −2.224 kg**, one year earlier,
which the model's partition coefficient has a pole just past.

## What was done

Seed 80 was reproduced — `out/build/release/src/healthgps` on the harness's derived config, one
thread, `--baseline-compat all` — and it fails in 1.4 s, deterministically, with

```
person 1222 (male, age 24) weighs -1.702e+283 kg after the energy balance, below the
configured minimum of 1 kg for 'Weight'
```

A throwaway trace was then built into a **separate build tree** (`/tmp/hgps-trace/new`, so the
five-hundred-seed census running against `out/build/release` was not disturbed), printing every
term of `KevinHallModel::run_energy_balance` for a named person at seventeen significant digits.
The instrumentation was reverted before anything was committed; what it printed is below, and the
part of it that matters is pinned by a test
(`KevinHallPhysiology.TheSeed80TrajectoryIsWhatTheTraceRecorded`).

**A cross-implementation person-level trace is not defined, and that is worth saying plainly.**
The two implementations draw different random streams, so person 1222 on seed 80 here is not
person 1222 on seed 80 there, and no year-by-year comparison of *this person's* terms against
*that person's* is a comparison of anything. The ninth run said as much. So the question "is this
arithmetic ours?" is answered a different and stronger way: by running the **baseline's own
statements**, copied out of `hgps_main/src/HealthGPS/kevin_hall_model.cpp`, on the state this
build traced. That is §3.

## 1. The trajectory

Person 1222, male, born into the cohort at 19, `Baseline` scenario. Every year of their adult
energy balance, from the first to the one that fails:

| | 2026 | 2027 | 2028 | **2029** | **2030** | **2031** |
|---|---:|---:|---:|---:|---:|---:|
| age | 19 | 20 | 21 | 22 | 23 | 24 |
| weight at entry, kg | 103.86 | 104.48 | 104.90 | 105.95 | 77.78 | 55.85 |
| physical activity (PAL) | **1.400** | **1.400** | **1.400** | 1.763 | 1.890 | 1.777 |
| energy intake, kJ/day | 4347.8 | 4330.5 | 4338.5 | **3044.3** | 3030.6 | 4828.6 |
| δ, energy cost per kg | 21.18 | 21.07 | 20.97 | **46.99** | 65.68 | 68.17 |
| body fat at entry, kg | 24.51 | 24.96 | 25.17 | 25.51 | 5.86 | **−2.232** |
| **steady-state fat, kg** | 25.47 | 25.24 | 25.43 | **−6.13** | **−3.07** | −2.44 |
| τ, days | 533.8 | 539.8 | 542.9 | 375.0 | 154.7 | **−0.559** |
| exp(−365/τ) | 0.505 | 0.509 | 0.510 | 0.378 | 0.094 | **2.29×10²⁸³** |
| **body fat out, kg** | 24.99 | 25.10 | 25.29 | 5.82 | **−2.224** | 4.65×10²⁸² |
| lean tissue out, kg | 58.62 | 58.62 | 58.83 | 51.32 | 37.32 | −2.17×10²⁸³ |
| **weight out, kg** | 104.59 | 104.63 | 105.06 | 77.28 | 55.65 | **−1.702×10²⁸³** |

Three years flat, then three years that each break something further. Read the bold cells in order
and the whole mechanism is there.

## 2. The mechanism, named

### 2029 — the model's own steady state leaves the domain

Two draws move hard in the same direction in one year. Physical activity leaves the floor the
config puts it on (`PhysicalActivity` has `range: [1.4, 2.5]`, and this person had been pinned at
1.4 for three years) and rises to 1.763; energy intake falls from 4338 to 3044 kJ/day, a **29.8%
drop** to about 727 kcal/day. δ — what is left of total expenditure per kg once the resting rate
and the thermic effect are taken out — more than doubles, from 20.97 to 46.99.

The two-equation steady state then solves to **`steady_fat = −6.13 kg`**. That is the model
stating that this person's equilibrium body has negative fat mass. Nothing has gone wrong
numerically; the fitted coefficients simply admit it. The year's step relaxes 37.8% of the way
there, and fat falls 25.51 → 5.82 kg.

### 2030 — the first non-physical value

Intake stays at 3031 kJ/day, activity rises again to 1.890, δ reaches 65.68, τ shortens to 155
days so the step now covers 90.6% of the distance to a steady state of −3.07 kg. Body fat comes
out at

```
fat = steady_fat − (steady_fat − fat_0) · exp(−365/τ)
    = −3.0670836989773984 − (−3.0670836989773984 − 5.8617563101306471) × 0.0944203064178748
    = −2.2240198893612368 kg
```

**This is the first value in the trajectory that cannot exist.** The run does not stop: 55.65 kg
is a legal weight, above the configured minimum of 1 kg, and the results file for 2030 is written
with this person in it. Nothing in either implementation looks at body fat.

### 2031 — the pole

The partition coefficient is

```
C = 10.4 · ρ_lean / ρ_fat = 10.4 × 7600 / 39500 = 2.001012658227848
p = C / (C + F₀)
```

`p` has a **pole at `F₀ = −C = −2.001 kg`**, and 2030 left the person at −2.232 kg — past it, by
0.23 kg. Everything downstream inverts at once:

| | value | |
|---|---:|---|
| `p` | **−8.6475** | a partition of a surplus, negative |
| partition `x` | −0.9091 | |
| determinant `a1·b2 − a2·b1` | **−4.876×10⁷** | positive in every earlier year (5.8×10⁵ to 2.0×10⁶) |
| `τ = ρ_lean·ρ_fat·(1+x) / determinant` | **−0.559 days** | |
| `exp(−365/τ)` | **exp(+652.5) = 2.288×10²⁸³** | |

A negative τ turns the year's step from a *relaxation toward* the steady state into an exponential
*flight away from* it, and one year of it at a time constant of half a day is 652 e-foldings. Body
fat comes out at +4.65×10²⁸², lean tissue at −2.17×10²⁸³, and the weight at −1.702×10²⁸³ kg, which
`validate_weight` catches because it is below the configured minimum.

**So the chain is: an intake collapse the model admits → a negative steady-state fat mass → a
negative fat mass → the partition coefficient's pole → a negative time constant → overflow.** The
term that fails is the body-fat compartment, not the weight; the weight is where it is noticed.

### Why only one person

Instrumented to report every person-year whose body fat comes out non-positive, seed 80's whole
run — **43,960 adult person-years** of energy balance in the baseline arm before it stops —
reports **exactly one**:

```
FATCENSUS year=2030 id=1222 age=23 steady_fat=-3.0670836989773984 fat_0=5.8617563101306471
         fat=-2.2240198893612368 tau=154.66107471082029 weight=55.651179795891721
```

**Seeds 1 to 20 — the twenty every stored reference is built from — report none at all**, which is
why the guard below cannot move any number those references hold, and why that is a measurement
rather than a hope.

## 3. Whose arithmetic it is

The two implementations' energy balances are the same expressions with the same constants:

| | this build | the baseline |
|---|---|---|
| ρ_fat, ρ_lean | 39.5e3, 7.6e3 | 39.5e3, 7.6e3 |
| γ_fat, γ_lean | 13.0, 92.0 | 13.0, 92.0 |
| η_fat, η_lean | 750.0, 960.0 | 750.0, 960.0 |
| β_TEF, β_AT | 0.1, 0.14 | 0.1, 0.14 |
| ξ_Na, ξ_CI | 3000.0, 4000.0 | 3000.0, 4000.0 |
| the step | `energy_balance.cpp` `run_energy_balance` | `kevin_hall_model.cpp:955-1030` `kevin_hall_run` |
| a floor on body fat | at initialisation only (`fat < 0 → 0.2·weight`) | at initialisation only (`F < 0 → 0.2·BW`) |
| in the yearly update | **none** | **none** |

Both floor a negative Deurenberg fat estimate when the state is first built, and the comment
explaining why says exactly what this document is about — it "keeps the partition coefficient
below finite, which a zero or negative fat mass would not". **Neither applies that floor to the
yearly update, which is the only place body fat can go negative by integration.**

That is an argument from reading. The measurement is this: the baseline's statements were copied
verbatim into a standalone program that shares no code with this project, and fed the state this
build traced.

| | 2030 step | 2031 step |
|---|---|---|
| this build's `fat` | −2.2240198893612368 | 4.6517731687203264e+282 |
| **the baseline's arithmetic** | **−2.2240198893612395** | **4.6517731687203666e+282** |
| this build's `weight` | 55.651179795891721 | −1.7019180456941046e+283 |
| **the baseline's arithmetic** | **55.651179795891721** | **−1.7019180456941172e+283** |

Identical to the last digit on the year that matters, and to fifteen significant figures on the
overflow, where the residue is `-O2` floating-point contraction and not a difference in the model.

**The instability is inherent to the fitted model**, in the sense
[docs/decisions/0041-deliberate-deviations-are-switchable.md](../decisions/0041-deliberate-deviations-are-switchable.md)'s
classification means: not something this build does differently, and not something the baseline
masks. The energy-balance coefficients admit a draw whose one-year step takes body fat below zero,
and neither implementation checks.

## 3a. What the aggregate output does not show

The ninth run recorded a **three-year precursor** — "the largest band mean weight is flat at
87.28 kg through 2027 and then 87.32, **92.1**, **98.5** in 2028–2030" — and concluded that a run
stopping in 2030 would have written a contaminated number and exited zero
([docs/equivalence.md](../equivalence.md), [docs/backlog.md](../backlog.md) item 2).

**That is not a precursor, and this run withdraws it.** Three things are wrong with it:

- **The rising series is the *intervention* arm.** In the baseline arm the largest band mean
  weight is flat at **87.2845 kg in every year** of the run, because the Kevin Hall model
  calibrates each (sex, age) band's mean weight onto the `FactorsMean` table and the intervention
  replays the baseline's adjustment rather than computing its own. The baseline arm's band means
  are pinned by construction and can say nothing about one person.
- **The band is the wrong band.** 92.1 is three 96-year-old men and 98.5 is three 97-year-old
  men. Person 1222 is 22 and 23 in those years.
- **It happens on seeds that never diverge.** Seed 251 completes cleanly and its intervention arm
  reaches **98.6 kg** in 2030, in a five-person band of 92-year-olds. It is the ordinary
  behaviour of a maximum taken over hundreds of bands, some of which hold three people.

**So the honest statement is the opposite of the one it replaces, and it is worse: the aggregate
output cannot show this at all.** Person 1222's own weight really does go 105.9 → 77.3 → 55.7 kg
over 2029 and 2030. Their band's reported mean does not move by so much as a bit:

| male, in the baseline arm | seed 80 | seed 1 | seed 5 |
|---|---:|---:|---:|
| age 21, 2028 | 78.931943 | 78.931943 | 78.931943 |
| age 22, 2029 | **79.468944** | 79.468944 | 79.468944 |
| age 23, 2030 | **80.042100** | 80.042100 | 80.042100 |

Identical to the last printed digit on a seed whose cohort contains a man 22 kg lighter than he
should be, because the Kevin Hall model **calibrates each (sex, age) band's mean weight onto the
`FactorsMean` table**: `compute_weight_adjustments` takes `expected − simulated` per band and adds
it to everybody in it. The diverging person's deficit is therefore not merely diluted among his
forty-six peers — it is *spread onto them*, and the band mean is restored exactly. A run stopped
at 2030 would have written a mean weight no reader could tell from a correct one, because in that
column it **is** the correct one.

That is the argument for [ADR 0050](../decisions/0050-no-output-carries-a-number-that-cannot-exist.md)
rather than for watching the aggregates: an aggregate is exactly the wrong instrument for a defect
that affects one person in seven thousand, and the place to catch it is where the person's own
value is still a person's own value.

## 4. What the baseline does when it happens

It does what this build did: nothing, until the weight leaves the configured range, and then it
depends on the sign.

- **Downward** — `validate_weight_in_config_range` throws below the minimum
  (`kevin_hall_model.cpp:1196`). The baseline would stop on this seed too, if its own cohort put
  somebody past the pole. It is not more robust; it was not asked.
- **Upward** — above the maximum it prints `[WEIGHT RANGE WARNING]` to stdout and **returns**
  (`kevin_hall_model.cpp:1191-1193`). A runaway of the other sign therefore leaves the baseline
  exiting **zero**, with an impossible number in the results file and a line in a log nobody
  parses. This build counts a `WeightAboveConfiguredMaximum` metric in the same case and also
  continues.

That is the shape of the upstream report: **the guard the baseline lacks is the one that stops an
out-of-domain body composition from being integrated into the next year**, and the reason it
matters upstream more than here is that upstream nothing at all stops the number being written
when the runaway goes up rather than down. See [docs/upstream-reports.md](../upstream-reports.md).

## 5. What was changed here

A bounded, documented guard in `run_energy_balance`, described in
[ADR 0049](../decisions/0049-the-energy-balance-is-integrated-only-where-it-is-defined.md), and
an invariant check on the analysis path so that no non-finite or physically impossible value can
reach an output file whatever produces it. The guard is a deviation from the baseline's behaviour
and so carries a compatibility flag under
[ADR 0041](../decisions/0041-deliberate-deviations-are-switchable.md): see
[docs/deviations.md](../deviations.md).

## How to reproduce this

```bash
# the refusal, in about a second and a half
python3 tests/equivalence/seed_scan.py --example KevinHall_FINCH --seeds 1 --first-seed 80 \
    --side new --out /tmp/seed-80.jsonl
```

The trace itself needs the throwaway instrumentation, which is not in the tree. What is in the
tree is the state it recovered and the step it explains: `tests/model/kevin_hall_test.cpp`,
`KevinHallPhysiology.TheSeed80TrajectoryIsWhatTheTraceRecorded` and the two tests beside it run
the same arithmetic on the same numbers.
