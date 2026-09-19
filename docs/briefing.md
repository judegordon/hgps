# A note for the Health-GPS authors

A second implementation of Health-GPS, in C++20, written to be compared against yours run for run.
Not a fork and not a proposed replacement: it exists to find out what the model does when two
independent implementations are held against each other. Every number below points at a document
here.

It reimplements the surface your examples use — the hierarchical linear model and its dynamic form,
the static linear and Kevin Hall families, the disease and analysis modules, population impact
fraction, six intervention types — reading your configurations through a converter ([ADR
0010](decisions/0010-config-v2-and-a-converter.md)) and your packs unchanged.

## What it proves, and what it does not

**Proved.** Three of your six examples — `HLM_France`, `HLM_India`, `KevinHall_FINCH` — run end to
end in both implementations and agree statistically: **124,766 tests over four sweeps of twenty
seeds, none failing** ([docs/equivalence.md](equivalence.md)). Each test is a hypothesis test per
(scenario, year, sex, variable) rather than a tolerance on a number, because two Monte Carlo runs
cannot agree exactly; [docs/equivalence-method.md](equivalence-method.md) has the rules, and the
harness is tested three ways.

**And the threshold is now a number we chose rather than one we discovered.** A comparison used to
pass when a difference was within 4.5 *estimated* standard errors — a z threshold on a quantity that
is not sigma, so the same build against the same baseline produced anywhere between 0 and 45
failures depending on which twenty seeds were drawn. It is now a **family-wise false-positive rate
of 1%**, controlled by Holm over every test a run performs, and that rate was **measured on a null
before the rule was adopted**: 30 pairs of twenty seeds of one build against itself, across all
three examples, 922,564 tests, **zero failures against 0.30 expected**
([ADR 0048](decisions/0048-a-comparison-with-a-stated-false-positive-rate.md)). There is no failure
budget on any example. If you want to check the rule rather than take it, §4 of
[docs/equivalence-method.md](equivalence-method.md) is written so that you can, without reading the
script.

**Not proved.** `HLM_India` is compared at a hundredth of the cohort it ships — 12,406 people
against 1,240,613 — because a full-scale sweep is days of machine time. The FINCH surface is one
country and one pack. Population impact fraction has never met the baseline, because the only
example using it cannot run. The floor is your CSV's six significant digits.

**The comparison used to read one file per run, and no longer does.** Every CSV a run writes is now
reduced and compared, family by family — which is how 49 columns of the income-stratified files came
to be zero here and non-zero in yours on the same example, unnoticed for as long as this build has
written them. All 49 are filled and matched, and a second check asks of the files themselves which
columns are identically zero on each side, because a statistical comparison cannot express "one side
has numbers and the other has nothing" ([docs/equivalence.md](equivalence.md)). Worth knowing on
your side: **the two HLM examples' stratum files are empty in *both* implementations**, because
nobody in them has an income category at all, so `KevinHall_FINCH` is the only example that checks
those columns against anything.

## Findings in the baseline that change results

Each has its evidence and a test in [docs/deviations.md](deviations.md).

| ID | What it does | How we know |
|---|---|---|
| **B-24** | the food-labelling policy re-applies its impact to somebody who failed an early coverage draw and passed a later one | measured below |
| **B-25** | an intervention on the Kevin Hall surface has no effect, and the run reports success | `KevinHall_FINCH` with `simple` and with `marketing`: identical result files |
| **B-21** | immigration into an empty age-sex band abandons that band's target silently, so the cohort falls short of its own projection | 197 of the baseline's band counts short on `HLM_France`, all of them empty bands |
| **B-26, B-27** | a fraction the PIF store cannot supply is a silent no-op and a gap inside a table reads as zero; and the published PIF schema has the sex column backwards from both the loader and the data | `KevinHall_PIF`: four of fifteen diseases have no `Smoking` table. `cervicalcancer` is non-zero only at `Gender=1`, 600 cells of 3,330 |
| **B-22, B-05, B-06** | `std_income` is emitted and never filled; a seed can give a different answer on a different standard library, twice over; an absent seed runs from `std::random_device` and is then recorded as `0` | 0 in all 4,884 rows of a FINCH run; `static_linear_model.cpp:1952`; `mtrandom.cpp:8` |

**New this run, and it is not in that table because this build reproduces it rather than fixing
it**: every demographic standard deviation that is also a declared risk factor has its square root
taken **twice**. `calculate_standard_deviation` finishes the accumulated squared deviations by
walking every mapping entry and then a fixed list of demographic names, and `KevinHall_FINCH`
declares `Region`, `Ethnicity`, `Income`, `income_category`, `Age`, `Age2`, `Age3` and `Gender` as
level-0 risk factors, so all eight are in both lists. `std_region` is **0.122097** in a band of 50
people whose region has mean 1.38 — the spread there is 0.745, and 0.122097 is its square root over
50. `std_bmi` is in the mapping only, is finished once, and is right.
[docs/upstream-reports.md](upstream-reports.md) has it as report 5, with what is and is not
affected. We reproduce it because a standard deviation is a number you may have published and
changing it is your decision, not ours.

Eleven more of the same kind are in that table. This build reproduces none of them, so a comparison
needs a way to put them back: each deviation that changes a number has a compatibility flag, and the
harness compares with the flags **on**, then re-runs with them off to report what each is worth
([ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md)).

## What B-24 is worth

Mean BMI of males in the intervention scenario, this build minus the same build with the flag on,
over twenty seeds ([docs/equivalence.md](equivalence.md)):

| | Largest difference | In | Relative |
|---|---:|---:|---:|
| `HLM_France` | +0.0531 | 2037 | +0.209% |
| `HLM_India` *(reduced cohort)* | +0.0313 | 2050 | +0.160% |

Zero in the policy's first year — the defect needs a *previous* failed draw — growing while the
coverage window is open, flat afterwards. **It reaches further than mean BMI**: on `HLM_India`, 194
series differ and 12,533 agree to the printed precision — years of life lost, DALYs, head counts,
and the prevalence and incidence of eleven diseases.

## Two packs neither implementation can run

`KevinHall_India` and `KevinHall_PIF` stop in their first simulated year in **both**
implementations, and it is one defect rather than two: `India.DataFile.csv` and both weight-quantile
files are byte-for-byte identical between the two directories. Both put `Weight`'s lower bound at
**3.319358 kg**, and the quantile curve gives newborns below it — 3.159 kg and 2.544 kg in two
cohorts. No seed, cohort size or horizon avoids it ([docs/examples.md](examples.md)).

**Either the bound or the curve is wrong, and only you can say which**; changing either here would
be inventing a number for a fitted model. It blocks two of your six examples and the only
population-impact-fraction comparison there could be.

## One seed in two hundred, and we do not know whose it is

Running `KevinHall_FINCH` two hundred times — which neither implementation had been, before this —
found **one seed on which our energy balance diverges and yours does not**. At seed 80, in simulated
year 2031, one 24-year-old man's weight reaches −1.7×10²⁸³ kg and this build stops rather than
writing it. It is deterministic, it is not one of the baseline behaviours we reproduce, and there is
a three-year precursor: the largest band mean weight is flat at 87.28 kg through 2027 and then 87.3,
**92.1**, **98.5** in 2028–2030, so a run that stopped in 2030 would have written a contaminated
number and exited zero.

**Your binary finished all two hundred of its own seeds** — but the two implementations draw
different random streams, so seed 80 is not the same cohort on both sides, and this is not evidence
that the instability is ours rather than the model's. **It is one in two hundred here and none in
two hundred there, and it is unexplained.** If the energy-balance coefficients admit a runaway for
some combination of intake and expenditure, it is worth knowing on your side too, because nothing in
your code stops it being written out ([docs/backlog.md](backlog.md) item 2,
[docs/equivalence.md](equivalence.md)).

## A question about interventions on the Kevin Hall surface

B-25 leaves a choice. This build refuses such a configuration at load time, naming the intervention,
its impacts and the dynamic model file — right if the alternative is silence, wrong if a policy is
*meant* to apply there.

**Where should a policy that shifts a nutrient be applied on that surface: before the energy balance
runs, after it, or to the intake targets?** Each gives a different answer, and it belongs to whoever
fitted the model. Until it is answered, four of your six examples can only be run with a no-op
policy ([docs/backlog.md](backlog.md) item 1).

## Running the comparison yourself

Your examples repository beside this one, and — only for `--refresh-reference` — a built baseline
([docs/build-notes.md](build-notes.md), including the four shims it needs on macOS).

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset release && cmake --build --preset release

tests/equivalence/run.py --example HLM_France --seeds 20 --refresh-reference  # runs your binary
tests/equivalence/run.py --example HLM_France --seeds 20 --use-reference      # needs no baseline

# The harness against itself: it must pass on two disjoint seed sets of one build, and fail on a
# deliberately corrupted copy of it.
tests/equivalence/self_check.py --mode seeds        --example Synthetic --seeds 20
tests/equivalence/self_check.py --mode perturbation --example Synthetic --seeds 20
```

`--verbose` adds the ten tests with the strongest evidence that still *passed*, which is how a shift
sitting just inside the threshold becomes visible; `--json FILE` writes the whole outcome, every
test with its raw and Holm-adjusted p-value. There is no failure budget, on any example.
[docs/SUMMARY.md](SUMMARY.md) is the long form.
