# Five reports for upstream

Five findings belong to whoever owns the baseline and the example data rather than to this
repository: this build cannot fix any of them without inventing a number or a mechanism for someone
else's fitted model. They are written here as five separate reports, each with the one command that
reproduces it and the evidence behind it, so that filing them is a copy rather than a rewrite.
[docs/backlog.md](backlog.md) item 14 is the item this file closes the *writing* half of; the other
half is somebody sending them.

Everything here was measured against Health-GPS `3.0.0.0` built as
[docs/build-notes.md](build-notes.md) records, on macOS 26.6.2 / Apple clang 21, with the
`imperialCHEPI/healthgps-examples` pack beside it. Nothing here depends on this implementation
being right about anything: each report reproduces with your binary and your data.

---

## 1. `KevinHall_India` and `KevinHall_PIF` cannot be run by their own baseline

**What happens.** Both examples stop in the first simulated year, in every run, with a weight below
the configured lower bound. Your binary dies on an uncaught exception; ours reports it as a located
error and stops. It is the same defect in the same place in both.

**Why.** Both configs put `modelling.risk_factors.Weight.range[0]` at **3.319358 kg**. Both give a
newborn a weight by multiplying an expected birth weight by a quantile drawn from
`weight_quantiles_NCDRisk_{male,female}.csv` — 9,995 multipliers each, 0.668034 to 1.916892 for
males and 0.655385 to 1.902204 for females — and the Kevin Hall model then validates that weight
against the configured range. The product is below the bound for ordinary draws, not tail ones:

| Example | Implementation | Who failed | Weight | Floor |
|---|---|---|---:|---:|
| `KevinHall_India` | baseline | (not named) | 3.1084 kg | 3.319358 kg |
| `KevinHall_India` | this build | person 1, male, age 0 | 3.159 kg | 3.319358 kg |
| `KevinHall_PIF` | baseline | (not named) | 3.1084 kg | 3.319358 kg |
| `KevinHall_PIF` | this build | person 5, male, age 0 | 2.544 kg | 3.319358 kg |

The person named is the first or fifth of the cohort, so no seed, cohort size or horizon avoids it.

**The two examples are one defect, not two.** `India.DataFile.csv` and both weight-quantile files are
byte-for-byte identical between the two directories.

**Reproduce:**

```bash
HealthGPS.Console -f KevinHall_India/new_config.json
```

**What we would like to know.** Either the bound or the curve is wrong, and only you can say which.
A 2.5 kg newborn is an ordinary low-birthweight baby, so a floor of 3.32 kg looks like the bound
being a mean rather than a minimum; but the same configured range has 92.56 kg at the top, which is
not a maximum either. Changing either here would be inventing a number for a fitted model.

**What it blocks.** Two of your six examples, the whole Kevin Hall + India data family, and the only
population-impact-fraction comparison there could be — `KevinHall_PIF` loads completely in this
build, all 69 fraction tables and both scenarios, and then stops on this.

**A second, smaller thing in the same config.** `KevinHall_India/new_config.json` carries both a
deprecated root `trend_type` of `"UPFTrend"` and a `project_requirements.trend.type` of
`"income_trend"`. Your own loader refuses a config that has both: *"Deprecated root-level trend_type
and/or income_categories are not allowed when project_requirements is present"*. So the file
contradicts itself and is unloadable by the validator that ships with it.

---

## 2. An intervention scenario is inert on the Kevin Hall / static-linear surface

**What happens.** A configuration that selects an intervention on that model surface runs to
completion, reports success, and produces output **byte-identical** to the same run with a no-op
policy. Nothing warns.

**Why.** `Scenario::apply` has exactly one call site in the whole tree — in the dynamic hierarchical
linear model. Neither `static_linear_model.cpp` nor `kevin_hall_model.cpp` calls it, so the policy
is never applied to anything on that surface.

**Reproduce**, with any two of the six policy types the example defines:

```bash
# edit KevinHall_FINCH/new_config.json: interventions.active_type_id = "simple", run; then
# set it to "marketing" and run again with the same seed
diff <result of run 1> <result of run 2>       # identical
```

We did this for all six intervention types on that surface: six runs, one pair of futures, identical
files.

**What we would like to know.** Is a policy *meant* to apply on that surface? This build refuses such
a configuration at load time — naming the intervention, how many impacts it declares and the dynamic
model file — because the alternative is to run it silently and hand back a result that looks like a
policy evaluation and is not. That is the right behaviour only if the inertness is a defect. If a
policy is meant to apply, **where** it applies changes the answer: before the energy balance runs,
after it, or to the intake targets. That is a modelling decision with a fitted model behind it.

**What it blocks.** Four of your six examples can only be compared with a no-op policy active.

---

## 3. `KevinHall_FINCH`'s legacy `static_model.json` names two files the pack does not contain

**What happens.** The legacy configuration for that example refers to two data files that are not in
the example directory, so the legacy path cannot be loaded as shipped.

**What we did.** This build reads the example's modern `new_config.json` instead, which is the file
that carries `project_requirements`, and records the choice as a scope decision rather than a
workaround ([ADR 0030](decisions/0030-policy-scenario-selection-for-the-broken-finch-example.md)).
The audit records the missing files as finding D-02.

**What we would like to know.** Whether the legacy file is meant to still work, or whether it is
superseded and could be removed — the second answer is a perfectly good one, and it saves the next
reader the hour we spent.

---

## 4. The baseline exits on a signal about one `KevinHall_FINCH` run in forty-five

**What happens.** Of **180** baseline runs of `KevinHall_FINCH` made for this project's equivalence
comparison, **4 exited on a signal** — three `SIGTRAP`, one `SIGABRT` — and every one of them
succeeded when re-run with the same binary, config and seed. `HLM_France` ran 80 times over the same
comparisons without a single failure, so it is the FINCH surface that provokes it.

**Why we think it happens.** Two scenario threads run concurrently, and the disease repository is
populated lazily from inside a parallel loop behind a lock-free fast path that races a concurrent
insert. Those are audit findings B-01 and B-02; this is them showing up as a crash rather than as a
reordering of the output.

**Reproduce.** Run the example in a loop and count; it is about one in forty-five, so forty-five
runs is roughly an hour:

```bash
for i in $(seq 1 60); do HealthGPS.Console -f KevinHall_FINCH/new_config.json || echo "FAILED $i"; done
```

**What we do about it.** The comparison harness retries a baseline run up to three times, counts the
retries and prints every one, so the flake is visible in the record rather than smoothed away
([docs/equivalence.md](equivalence.md), *The baseline does not always finish*). Across the same
runs this build has not exited on a signal once, which is what the sequential-scenario design buys
([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)).

---

## 5. Every demographic standard deviation that is also a declared risk factor has its square root taken twice

**What happens.** In `KevinHall_FINCH`'s result file, `std_region` is **0.122097** in a band of 50
people whose `mean_region` is 1.38. Region takes the values 1 to 4, so the spread of that band is
about 0.745 — and 0.122097 is `sqrt(0.745 / 50)`. The square root has been taken twice.

**Why.** `AnalysisModule::calculate_standard_deviation` finishes the accumulated squared deviations
in two loops, and a name can be in both. The first walks every mapping entry:

```cpp
for (const auto &factor : context.mapping().entries()) {
    divide_by_count_sqrt(factor.key().to_string(), core::Gender::female, age, count_F);
    divide_by_count_sqrt(factor.key().to_string(), core::Gender::male, age, count_M);
}
```

and the second is an explicit list of the demographic channels — `age`, `age2`, `age3`, `gender`,
`region`, `ethnicity`, `sector`, `income`, `income_category`, `physical_activity`.
`divide_by_count_sqrt` lower-cases the name and writes back in place:

```cpp
const double sum = series.at(sex, std_channel).at(age);
series.at(sex, std_channel).at(age) = std::sqrt(sum / count);
```

so a channel reached by both loops gets `sqrt(sqrt(sum_of_squares/n)/n)`. `KevinHall_FINCH`
declares `Region`, `Ethnicity`, `Income`, `income_category`, `Age`, `Age2`, `Age3` and `Gender` as
level-0 risk factors, so all eight are in both lists. `std_bmi` is in the mapping only, is finished
once, and is right.

**It is in the income-stratified files too**, through the same shape in
`calculate_income_based_standard_deviation` (`analysis_module.cpp:1735-1800`).

**What it does and does not affect.** Four of the eight are zero either way — `mean_age` is set to
the band's own age and `mean_gender` to the file's own sex, so their deviations are zero before
anything is square-rooted, and `income_category` is constant within a stratum file. The ones that
carry a number are `std_region`, `std_ethnicity`, `std_income` and, where a pack assigns it,
`std_sector`. No mean is affected and no other column is.

**Reproduce.** Run the example and look at the band with the most people in it: `std_region` should
be the spread of a 1-to-4 category over that band, and is its square root over the head count
instead.

```bash
HealthGPS.Console -f KevinHall_FINCH/new_config.json -T 1
```

**What we do about it.** This build **reproduces it**, in the whole-population series and in the
income-stratified one, and does not fix it. The equivalence harness compares these files column for
column, so a quiet correction here would be a difference nobody chose; and a standard deviation is
a number somebody may have published, so changing it is a decision that belongs with the model
rather than with a reimplementation. `AnalysisIncomeSeries.ADemographicThatIsAlsoADeclaredFactorHasItsSquareRootTakenTwice`
pins the reproduced behaviour by name and says why.

---

## What this file is not

It is not a patch set. Three of the four need a decision that belongs to whoever fitted the models
or assembled the data packs, and the fourth is a concurrency defect in a design this implementation
deliberately does not share, so a fix from here would be a rewrite rather than a patch.

[docs/deviations.md](deviations.md) has the differences between the two implementations that this build has already fixed,
each with its evidence and the test that pins it; those are recorded rather than reported because
this build has already fixed them, and [ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md)
makes each one switchable so its effect can be measured.
