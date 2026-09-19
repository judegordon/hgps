# A note for the Health-GPS authors

A second implementation of Health-GPS, in C++20, compared against yours run for run. Not a fork and
not a proposed replacement: it exists to find out what the model does when two independent
implementations are held against each other. It covers the surface your examples use — HLM and its
dynamic form, static linear, Kevin Hall, the disease and analysis modules, PIF, six intervention
types — reading your configs through a converter and your packs unchanged. Everything below is a
bug report in [docs/upstream-reports.md](upstream-reports.md), and the long form is
[docs/SUMMARY.md](SUMMARY.md). To reproduce any of it:
`tests/equivalence/run.py --example HLM_France --seeds 20 --use-reference` needs no baseline binary,
and `--refresh-reference` runs yours instead ([docs/build-notes.md](build-notes.md)).

## What it proves, and what it does not

**Proved.** Three of your six examples — `HLM_France`, `HLM_India`, `KevinHall_FINCH` — run end to
end in both implementations and agree statistically: **62,030 tests over two examples' stored
references this run, none failing, no failure budget on any example**
([docs/equivalence.md](equivalence.md)). Each is a hypothesis test per (family, scenario, year, sex,
variable), and the threshold is a **family-wise false-positive rate of 1%** under Holm, measured on
a null first — 922,564 tests, **zero failures against 0.30 expected**
([docs/equivalence-method.md](equivalence-method.md) §4).

**Not proved.** `HLM_India` is compared at a hundredth of the cohort it ships — 12,406 people
against 1,240,613. The FINCH surface is one country and one pack. PIF has never met the baseline,
because the only example using it cannot run. The floor is your CSV's six significant digits. And
both HLM examples' stratum files are empty in *both* builds, so only `KevinHall_FINCH` checks
those 49 columns.

## What we found in your code

Evidence and a pinning test for each in [docs/deviations.md](deviations.md). All but the last are
fixed here, switchably, so a comparison can measure what each costs.

| ID | What it does | How we know |
|---|---|---|
| **B-29** | the energy balance integrates a body fat mass through zero, past the pole of its own partition coefficient | seed 80 of `KevinHall_FINCH`: −2.224 kg of fat in 2030, −1.7×10²⁸³ kg in 2031. Report 6 |
| **B-30** | a risk factor that is `NaN` is counted as **zero** in the year's means and nothing says so; an infinity is not looked at at all | `analysis_module.cpp:385-391`: three people of 80 kg, one `NaN`, report 53.3 kg. Report 7 |
| **B-24** | the food-labelling policy re-applies its impact to somebody who failed an earlier coverage draw | +0.209% of mean male BMI on `HLM_France`; on `HLM_India` it moves 194 other series |
| **B-25** | an intervention on the Kevin Hall surface has no effect, and the run reports success | `simple` and `marketing`: identical result files. Report 2 |
| **B-21** | immigration into an empty age-sex band abandons that band's target silently, so the cohort falls short of its projection | 197 band counts short on `HLM_France`, every one an empty band |
| **B-26, B-27** | a PIF fraction the store cannot supply is a silent no-op, a gap inside a table reads as zero, and the published schema has the sex column backwards | `KevinHall_PIF`: 4 of 15 diseases have no `Smoking` table; `cervicalcancer` is non-zero only at `Gender=1` |
| **B-22, B-05, B-06** | `std_income` is emitted and never filled; a seed gives different answers on different standard libraries; an absent seed is recorded as `0` | all 4,884 rows of a FINCH run; `static_linear_model.cpp:1952`; `mtrandom.cpp:8` |
| *(reproduced, not fixed)* | every demographic standard deviation that is also a declared risk factor has its square root taken **twice** | `std_region` is **0.122097** where the spread is 0.745 — its square root over 50. Report 5: the correction is yours |

## The two we would send first

**Your energy balance admits a body fat mass below zero, and a year later the weight overflows.**
`p = C / (C + F)` in `kevin_hall_run` has a pole at `F = −C = −2.001012658227848 kg`. You floor a
negative fat estimate at *initialisation* (`kevin_hall_model.cpp:920`, your comment says it is to
keep `p` finite) and never again; neither did we. Past the pole `p`, the determinant and `tau` all
change sign, and `exp(-365.0 / tau)` at −0.559 days is 652 e-foldings. **It is your arithmetic as
much as ours**: those statements, in a program sharing no code with this project, fed the state we
traced, return `-1.7019180456941172e+283` against our `-1.7019180456941046e+283`. It is **four
seeds in five hundred**, and on three of the four **both implementations complete and write the
impossible person out** — `validate_weight_in_config_range` throws below the configured minimum and
only *warns* above it. Nothing warns in the aggregate output beforehand; the trace is
[docs/findings/seed-80.md](findings/seed-80.md).

**A risk factor that is not a number becomes a zero in the reported mean, silently.**
`AnalysisModule::calculate_historical_statistics` substitutes `0.0` for a `NaN` and divides by the
whole head count; `std::isnan` is false for `±inf`, so an infinity is not looked at at all. It is
the last place that knows whose a value is, and it turns any upstream defect into a wrong mean.

## Two decisions only you can make

**`KevinHall_India` and `KevinHall_PIF` stop in their first simulated year in both
implementations**, and it is one defect rather than two — their data files are byte-for-byte
identical. Both put `Weight`'s lower bound at 3.319358 kg and the quantile curve gives newborns
below it (3.159 kg and 2.544 kg), so no seed, cohort size or horizon avoids it. Either the bound or
the curve is wrong, and changing either here would be inventing a number for a fitted model. It
blocks two examples and the only PIF comparison there could be (report 1).

**Where should a policy that shifts a nutrient be applied on the Kevin Hall surface** — before the
energy balance runs, after it, or to the intake targets? Each answer gives a different number and
it belongs to whoever fitted the model. B-25 makes it a no-op upstream today; this build refuses
such a config at load time, right if the alternative is silence and wrong if a policy is meant to
apply there. Until it is answered, four of your six examples run only with a no-op policy.
