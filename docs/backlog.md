# Backlog

What is not done, ranked by value for effort. Every item says what it unblocks and what it costs,
because the point of a ranked backlog is to make the next decision easy rather than to be
complete.

Tags:

- `scope` — deliberately left out ([ADR 0021](decisions/0021-scope-finch-and-hlm-france.md)).
- `correctness` — something could produce a wrong number or refuse a valid input.
- `validation` — makes an existing claim checkable, or checks it harder.
- `platform` — build, CI, packaging.
- `cleanup` — internal, no behaviour change.
- `needs-ruling` — cannot be done here: it needs a decision that belongs to whoever owns the model
  or the data, and guessing would put an invented number into somebody else's fitted model.
- `docs`

**This run closed item 2, which was the previous run's only correctness item.** Every CSV a run
writes is now reduced and compared against the baseline's, family by family, with its own counts;
the 45 empty columns of the income-stratified files are filled and checked; `scripts/column-coverage.py`
asks of the files themselves which columns are identically zero on each side and fails where the
baseline has numbers and this build has none; the second fixture pack assigns a categorical income,
a region, an ethnicity and a sector, so every output family this engine can write is produced by a
fixture ([ADR 0047](decisions/0047-the-second-pack-carries-the-stratified-dimensions.md)); and the
summary endpoint and the results screen expose every family with a selector.

What is left is what it was, minus that: **one modelling question that belongs to you**
(interventions on Kevin Hall models), the validation this project's own documents hedge about, and
Windows. The top of the list is a question rather than work for the third run running.

## Do these first

### 1. Interventions on the Kevin Hall model surface — `needs-ruling`

**Value: high. Effort: none here, and that is the point.** It was item 8 in the previous run's
ranking; it is first now because everything above it is done, and because it is the largest thing
this build refuses that a user could reasonably want.

An intervention selected on the `StaticLinear`/`KevinHall` surface has **no effect at all** in the
baseline, and the run reports success: `Scenario::apply` has exactly one call site in the whole
upstream tree, in the dynamic hierarchical linear model, and neither `static_linear_model.cpp` nor
`kevin_hall_model.cpp` calls it (deviation **B-25**). So `KevinHall_FINCH` with `food_labelling`
active produces output byte-identical to the same run with `simple` active.

This build **refuses** such a configuration at load time rather than running it silently, with a
located issue naming the intervention, how many impacts it declares and the dynamic model file. That
is the right behaviour given the choice between "silently do nothing" and "say so"; it is not the
right behaviour if what upstream wants is for the policy to apply.

**The question that has to be answered elsewhere**: on the Kevin Hall surface, a policy that shifts
a nutrient has to propagate through the energy-balance model, and *where* it is applied changes the
answer — before the balance runs, after it, or to the intake targets. That is a modelling decision
with a fitted model behind it, and guessing would put an invented mechanism into somebody else's
work. Until it is answered, four of the six upstream examples can only be run with `simple`.

See [docs/deviations.md](deviations.md) B-25 and `tests/config/intervention_reach_test.cpp`.

### 2. Individual-level tracking output — `scope`

**Value: medium. Effort: low-medium.** `output.individual_tracking` is parsed, validated and
carried in `config::IndividualTracking`, and nothing writes the file. The baseline's
`individual_id_tracking_writer.cpp` is small. Worth noting: this is the feature that makes the
baseline's person IDs matter, and it is why this implementation kept the monotonic lifetime-unique
counter rather than the earlier rewrite's slot reuse
([ADR 0017](decisions/0017-person-ids-monotonic-and-free-slots.md)).

It has moved up this list for a reason that is new: it is now **the one output family the baseline
writes and this build does not**, and both the equivalence harness and the column-coverage script
carry an explicit exclusion for it. Each exclusion holds only while the baseline's own file is
*empty* — on `KevinHall_FINCH` it is zero bytes, because the config asks for ages 80–110 in four
named regions and matches nobody — and both turn back into failures with their reason if upstream
ever puts a row in it. So the cost of not doing this is now a documented hole rather than an
unexamined one, which is the right place for it to be, and closing it would remove two exclusions
rather than add a feature.

### 3. A runnable Kevin Hall example, which needs upstream — `needs-ruling`

**Value: high. Effort: none here.** Population impact fraction is implemented
([ADR 0038](decisions/0038-population-impact-fraction.md)) and `KevinHall_PIF` loads completely: config,
both model files, the registry, 69 fraction tables, both scenarios' modules. It then stops in its first
simulated year, on `KevinHall_India`'s defect, because it is the same data — `India.DataFile.csv` and
both weight-quantile files are byte-for-byte identical between the two examples, and both put `Weight`'s
lower bound at 3.319358 kg, above what the curve produces for the lightest newborns. **The baseline dies
in the same place.**

So the whole Kevin Hall + India data family — `KevinHall_India`, `KevinHall_PIF` and all twelve of the
latter's alternatives — cannot be run by either implementation, and no code change here can fix it:
raising the curve or lowering the bound would both be inventing a number for somebody else's fitted
model. This is item 5's question asked again from a second direction, and answering it would unblock
two examples rather than one, plus the only PIF equivalence comparison there could be.

### 4. `HLM_India` at the cohort it ships — `validation`

**Value: medium. Effort: medium, and all of it is machine time.** `HLM_India` is now compared against
the baseline at 20 and 60 seeds — but at `size_fraction` 1e-5, which is **12,406 people against the
1,240,613 it ships** ([docs/equivalence.md](equivalence.md)). That was the only way to have the
comparison at all: at full scale one seed is about 40 minutes in this build and over an hour in the
baseline, so twenty seeds of both is about a day and sixty is three.

What the reduction cannot tell you is anything that only appears at scale. Two candidates are
specific rather than hypothetical: the emptying-band exclusion is **1,641 bands at 12,406 people**
against 785 on `HLM_France`, and at 1.24 million almost none of those bands would empty at all — so
the full-scale comparison would exclude far less and test more; and a rare disease that gives 0, 1 or
2 cases at this cohort size gives hundreds at the shipped one, which moves several of the comparisons
off the lattice that item 9 is about.

So this is worth doing once, on a machine that can be left alone for a few days, and the stored
reference would be large — which is item 8.

### 5. A second country for the FINCH surface — `validation`, and it needs upstream

**Value: high. Effort: unknown, and not all of it is here.** The FINCH equivalence evidence is one
pack, one country. The obvious second is `KevinHall_India`, which uses the same `StaticLinear` and
`KevinHall` code with a different factor set, different food groups, the JSON shape of the static
model and the income trend — the parts of the surface FINCH does not reach.

It cannot be run. Both implementations stop in its first simulated year because the pack's
configured lower bound on `Weight`, 3.319358 kg, is above what its own weight quantile curve
produces for the lightest newborns; the baseline dies on an uncaught exception and this build
reports it as a located internal error. [docs/examples.md](examples.md) has both messages.

Nothing here can fix that: raising the curve or lowering the bound would be inventing a number for
somebody else's fitted model. What this item needs is upstream to say which of the two is wrong.
Until then the FINCH surface has one country, and that is the largest single gap in the validation.

### 6. The comparison's allowance is estimated from the draws it is judging — `validation`

**Value: medium-high. Effort: medium, and all of it is statistics rather than code.** Found this
run, by the comparison reaching four times as many series and then by re-scoring a 60-seed run over
subsets of itself.

The allowance for a statistic is `4.5 × sqrt((s_b² + s_n²)/n)` — built from the **sample** standard
deviations of the two 20-draw samples. That estimate is itself noisy. When a seed set happens to
give a tight sample the allowance shrinks, and any series sitting at a small **persistent, signed**
offset then fails in *every year at once*, because the offset is in every year.

The measurement, on `KevinHall_FINCH` with every output family compared:

| 20 seeds drawn from a 60-seed run | Out of tolerance, of ~111,800 |
|---|---|
| seeds 1–20 | 3 |
| seeds 21–40 | 1 |
| seeds 41–60 | 1 |
| 100 random draws of twenty | min 0, median 2, mean 3.4, **max 45**; 28 of 100 had none |

**The worst draw's 45 failures are 32 in one whole-population series** —
`result/std_polyunsaturatedfattyacid`, which is about **−1.1%** of the baseline's at its worst cell
while its mean agrees to −0.109%, and which uses 0.60× of its allowance over all 60 seeds. Six
groups account for all 45. So this is **not** about the stratified files, and it is older than this
run: what this run did was add four times as many groups for it to show up in.

**This is the third time a normal-theory allowance has been applied where it does not hold**, and
the first two were fixed rather than widened: a point mass, and then the median of a lattice-valued
series ([docs/equivalence-method.md](equivalence-method.md) §5). Candidates worth measuring here: a
variance estimate pooled across the years of one series rather than taken per year, which is where
the stability is; a floor on the allowance derived from the baseline's own printed precision times
the *value* rather than only from the sample spread; or a test of the whole series at once — a
signed-rank over its years — instead of one comparison per year, which is the shape the failures
actually take.

**Two things to be careful of.** The small signed offsets are worth understanding in their own
right before the rule is changed: `std_polyunsaturatedfattyacid` and `std_fat` are both about 1%
low, their means agree to a tenth of a percent, and both are two-stage factors whose spread depends
on the fraction of people at zero. That may be a real, tiny difference rather than noise, and a
rule change that hides it would be the wrong fix. And **do not reach for the sigma limit**: it is a
Bonferroni correction over "the ~5,000 independent series" and there are now about 25,300, so 4.76
is the honest value rather than 4.5 — but that clears one of the three cells at seeds 1–20 and
neither of the 60-seed ones, it makes the threshold stricter for the smaller sweeps where
`HLM_India` sits at 0.987× of its allowance, and it does nothing about a whole series failing
together.

**Until it is done there is a failure budget of 3 on one example**, the first this project has had
since the fifth run, recorded with the measurement that sized it
([docs/equivalence.md](equivalence.md), *There is a failure budget again*). Closing this item is
what removes it.

### 7. A fallback donor for immigration into an empty band — `correctness`

**Value: low-medium. Effort: low.** When an age-sex band is empty there is nobody to clone an
immigrant from, so both implementations skip it and the cohort falls short of the demographic
projection by a few people. It is recorded as **B-21**, reported per year as
`ImmigrationShortfallPeople` and `ImmigrationShortfallBands`, and it is the mechanism behind the
equivalence harness's one exclusion rule.

Keeping the baseline's rule was the right call for a comparison, but the shortfall is still a run
that misses its own target. The baseline has a nearest-age search in its demographic module
(`demographic.cpp:662`) that it does not use for this. Using it would make the target always
achievable, at the cost of nudging the age distribution. It changes results, so it needs a
deviation entry, an ADR and a re-run of both references.

### 8. More seeds, and a smaller stored reference — `validation`

**Value: low-medium. Effort: low.** Both references are 20 seeds, confirmed at 60 and then
discarded. Keeping the 60-seed references would be about 12 MB gzipped. The alternative is to
store the reduction rather than the raw results — the harness reduces to (scenario, year, sex,
variable) before it compares anything, and the reduction is two orders of magnitude smaller — at
the cost of not being able to change the reduction without a re-run.

### 9. Windows — `platform`

**Value: unknown. Effort: medium, and better understood than it was.** Not targeted
([ADR 0013](decisions/0013-platforms-linux-and-macos.md)). The code avoids PSTL and
`<syncstream>` and has one `__APPLE__` branch, so the likely work is the executable-path lookup,
`posix_spawn` (used for `curl` and `unzip`), `gmtime_r`, and the file-system assumptions in the
cache. `CMakeLists.txt` refuses a Windows build outright, so the first step is deciding to stop
refusing.

**CI changed the estimate, in both directions.** The tree now builds and passes on Linux with two
compiler families as well as on macOS, which removes the "it has only ever been one toolchain"
uncertainty that made this unquotable. But the five defects CI found are exactly what a *third*
platform would find more of, and three of them were invisible to the development compiler: a type
whose width differs, a warning one compiler has and another does not, and headers one standard
library supplies transitively. MSVC is a third standard library and a fourth warning set, and the
honest expectation is a comparable list. [docs/build-notes.md](build-notes.md) has what the second
platform cost, which is the only evidence available for what a third would.

Still only worth doing if someone needs it.

## Smaller things

### 10. Run the sanitizer presets' tests in parallel — `platform`

**Value: low-medium, and lower than it was. Effort: low, and the risk is what makes it an item
rather than a one-liner.** This run took the other half of the problem instead: TSan runs one
fixture pack and six self-check seeds, which cut the job roughly in half without touching how the
tests are run ([ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md)). What is left is the
92 `Packs/` tests that still run under TSan, at about 1,200 seconds of the preset's 1,458.

`ctest -j` is the obvious next answer: these tests are almost all single-threaded and run serially
today, on runners with several cores. What makes it an item rather than a one-liner is that some of
them are *about* threads — the byte-identity-at-N-threads tests spawn workers, the server tests bind
sockets, and the stress test runs several clients — so the right degree of parallelism has to be
found rather than assumed, and under a sanitizer the memory cost multiplies too. It is also the
change most likely to make a flaky test look like a real one, which is the thing a test suite can
least afford.

### 11. A schema for the model definition files — `docs`

**Value: medium. Effort: low.** `schemas/v2/` covers the config. The static and dynamic model
files have no published schema, which is why their member names were wrong for a week in the
previous run and why this run spent an afternoon on the FINCH pack's headerless two-column CSVs
and its R row-index columns. The shapes are documented only in `src/config/models/*.cpp` and in
the three loader test files. Write them, and extend `schema_agreement_test.cpp` to cover them the
way it covers the config.

### 12. Sector, and `demographic_models` — `scope`

**Value: low. Effort: low.** `person.sector` (urban/rural) is assigned nowhere; the channel
appears if the mapping declares the factor. `modelling.demographic_models` is carried through as
opaque JSON, deliberately — its shape belongs to the model family that reads it — and no model
family reads it yet.

### 13. The fixture pack's top-age artefact — `validation`

**Value: low. Effort: low.** The synthetic pack's population table stops at the same age as the
config's `age_range`, so anyone reaching the top age leaves the cohort and the pack's simulated
death rate runs above what its mortality table implies. Recorded in the pack's own `SYNTHETIC.md`.
Extending the pack's age range by a few years above the configured one would remove the artefact;
nothing depends on it, because no test reads the pack's death rates as a check on anything.

### 14. Report four things upstream — `docs`

**Value: low here, high upstream. Effort: what is left of it is not ours.** The four reports are
written: [docs/upstream-reports.md](upstream-reports.md) has each one with the command that
reproduces it against the baseline's own binary and data. What is not done is **sending them**,
which is a thing a person does with an account on somebody else's tracker. The four:

1. **`KevinHall_India` cannot be run by its own baseline** — its `Weight` lower bound is above what
   its weight quantile curve produces (item 3), and its `new_config.json` contradicts itself
   between the deprecated root `trend_type` and `project_requirements.trend.type`, which the
   baseline's own validator refuses.
2. **An intervention scenario is inert on the FINCH model surface** (item 1): `Scenario::apply` has
   one call site and neither Kevin Hall model calls it, so a config selecting `food_labelling` on
   that surface runs with no error and no effect.
3. **`KevinHall_FINCH`'s legacy `static_model.json` names two files the pack does not contain**
   (audit D-02, [ADR 0030](decisions/0030-policy-scenario-selection-for-the-broken-finch-example.md)).
4. **The baseline crashes on `KevinHall_FINCH` about one run in forty-five** — 4 of 180 measured
   runs, three on `SIGTRAP` and one on `SIGABRT`, each succeeding on the retry with the same
   binary, config and seed. That is audit findings B-01 and B-02 (concurrent scenarios, a
   lazily-populated repository) showing up as a crash rather than as a reordering. The equivalence
   harness retries up to three times and prints every retry, so it is visible here rather than
   smoothed away.

Each is reproducible from this repository with one command, which is most of the work of a good bug
report and is why writing them up was worth doing before anybody had agreed to file them.

## Closed this run

Kept as a record of what the numbers above used to be, because three documents and a test reference
them by number.

| Was | | |
|---|---|---|
| **2** | The weight-category columns are counts and both reductions treat them as means | done, and it was **not** a deviation from the baseline: the baseline emits head counts and so does this build, so the defect was in this project's own two reductions and in the income series that never filled the four columns at all. No compatibility flag — ADR 0041's flag is for a deliberate difference from the baseline, and there was none here. All four stored references regenerated against the baseline binary ([docs/equivalence.md](equivalence.md)) |
| **9** | `DataSeries` keyed by channel name | done; the analysis module resolves its channels once a year instead of per person per year, byte-identical on `HLM_France` and `KevinHall_FINCH`, and on `HLM_India` at the cohort it ships ([docs/performance.md](performance.md)) |

## Explicitly not planned

- **Reintroducing the event bus.** Fifteen baseline tests, no purpose here: the runner hands each
  row to a sink, which is the one place output is written
  ([ADR 0020](decisions/0020-output-single-owner-defined-row-order.md)).
- **Reintroducing `SyncChannel`.** Nine baseline tests, and the mechanism behind audit B-01
  ([ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)).
- **A lazy, cached data repository.** Three baseline tests, and the mechanism behind audit B-02.
- **Bit-exact reproduction of the baseline.** Ruled out up front
  ([ADR 0006](decisions/0006-validation-strategy.md)); it would have foreclosed most of
  [docs/deviations.md](deviations.md).
- **Cross-platform bit-reproducibility.** Not a requirement
  ([ADR 0008](decisions/0008-determinism-contract-enforced-by-types.md)). The determinism
  contract is per-binary, and the cheap wins that happen to help across platforms — no unordered
  CDF, pinned floating-point contraction, no `std::generate_canonical` — are in it anyway.
- **The `dummy` model family.** `Dummy_disease_test` selects it. It is a test double that upstream
  ships as an example; this implementation has its own fixtures for that job.
