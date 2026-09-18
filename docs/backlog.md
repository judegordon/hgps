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

**This run closed the previous one's first two items and part of a third.** The graphical host
exists — a local JSON server and a single-page app over it ([ADR 0042](decisions/0042-a-local-server-in-the-same-binary.md),
[ADR 0043](decisions/0043-a-plain-typescript-frontend.md)) — and CI has run, which promoted GCC from
"has never built this tree" to a required, green check. It also added one thing nobody asked for and
the ruling did: every deliberate deviation is now switchable, so its effect is measured rather than
argued ([ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md)).

So the ranking changes shape again. What is left is: one question that belongs to whoever owns the
model (interventions on Kevin Hall models), the performance item the profile keeps pointing at, the
validation this project's own documents hedge about, and Windows. There is no large piece of new work
at the top any more, which is the first time that has been true.

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

### 2. Resolve names to indices at the call site — `cleanup`

**Value: medium-high. Effort: medium, and it touches the model loaders.** Unchanged from the
previous run except in rank: it is the largest remaining performance item and the profile has
pointed at it for two runs.

[ADR 0037](decisions/0037-index-keyed-risk-factor-store.md) made `Person::risk_factors` index-keyed
and [ADR 0040](decisions/0040-a-bounded-search-for-the-long-vectors.md) fixed the lookup that left
behind — `KevinHall_FINCH` is 1.54× and then a further 1.44× faster, byte-identical across both.
**The first half of this item is done**; what is left is the half the profile said was bigger.

**About 31% of the FINCH profile is names being resolved at the call site.** The store no longer
compares strings; its callers still hand it a `core::Identifier`, which costs a hash probe, and some
of them *construct* one per person per year — which is what `Identifier::validate_identifier` and
`chars::is_alnum` at 8.3% of a profile mean. The fix is ADR 0037's idea one level up: the linear
model's coefficient list and the Kevin Hall model's nutrient names hold **resolved indices**, so no
name reaches the hot loop.

`DataSeries` is still keyed by channel name and is the smaller half again: the analysis module and
the result writer are well below the per-person work in the profile.

Whatever is done here, the check that matters is the one ADR 0040 used and that caught a defect in
the first attempt at ADR 0037 within minutes: run both examples before and after and compare the
result files **byte for byte**. A statistical comparison over twenty seeds calls a last-bit
difference agreement.

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

### 4. Individual-level tracking output — `scope`

**Value: medium. Effort: low-medium.** `output.individual_tracking` is parsed, validated and
carried in `config::IndividualTracking`, and nothing writes the file. The baseline's
`individual_id_tracking_writer.cpp` is small. Worth noting: this is the feature that makes the
baseline's person IDs matter, and it is why this implementation kept the monotonic lifetime-unique
counter rather than the earlier rewrite's slot reuse
([ADR 0017](decisions/0017-person-ids-monotonic-and-free-slots.md)).

## Worth doing soon

### 5. `HLM_India` at the cohort it ships — `validation`

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
off the lattice that item 11 is about.

So this is worth doing once, on a machine that can be left alone for a few days, and the stored
reference would be large — which is item 8.

### 6. A second country for the FINCH surface — `validation`, and it needs upstream

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

### 10. Make the GCC entries required — `platform` — **done**

Done this run. `experimental: true` is gone from both GCC entries, so a GCC regression fails the
build instead of being reported quietly.

### 11. The lattice detector should classify on the numerator — `validation`

**Value: medium-high. Effort: low-medium, and the measurement is already taken.** The equivalence
harness compares a quantile of a **lattice-valued** series with an exact test of the counts rather
than numerically, because the normal-theory allowance shrinks as 1/√n while the lattice step does
not — a test that gets worse with more evidence. That rule was derived and added last run, and it is
right.

**Its detector looks at the wrong number.** It classifies a series as lattice-valued when the pooled
sample has at most 6 distinct values, or when one value covers more than half the seeds. Both rules
are applied to the **rate**. But a disease rate is a small integer count of cases over a band head
count that differs from seed to seed, so dividing smears the lattice: the `HLM_India` comparison at
60 seeds produced four such series with **43 to 79 distinct values** and a modal share of 0.30 to
0.40 — under both rules — while a third of their seeds were **exactly zero**. Six comparisons, at
1.02× to 1.09× of their allowance, in both the `simple` and the `food_labelling` runs, which is what
says they belong to the comparison and not to either implementation
([docs/equivalence.md](equivalence.md)).

The fix is specific: classify on the **numerator**. The harness reduces to a rate and throws the
count away; carrying the count alongside it would let the detector ask the question it means to ask —
"how many distinct case counts are there, and how often is it zero?" — instead of asking it of a
quotient.

**Do not do this by widening the thresholds.** Moving 0.5 to 0.3 after seeing which comparisons it
excludes is not evidence, which is the same rule this project applied to the one `HLM_France`
residual ([docs/equivalence.md](equivalence.md), "The threshold was not changed"). The detector
either measures the right quantity or it does not.

### 12. A schema for the model definition files — `docs`

**Value: medium. Effort: low.** `schemas/v2/` covers the config. The static and dynamic model
files have no published schema, which is why their member names were wrong for a week in the
previous run and why this run spent an afternoon on the FINCH pack's headerless two-column CSVs
and its R row-index columns. The shapes are documented only in `src/config/models/*.cpp` and in
the three loader test files. Write them, and extend `schema_agreement_test.cpp` to cover them the
way it covers the config.

### 13. Sector, and `demographic_models` — `scope`

**Value: low. Effort: low.** `person.sector` (urban/rural) is assigned nowhere; the channel
appears if the mapping declares the factor. `modelling.demographic_models` is carried through as
opaque JSON, deliberately — its shape belongs to the model family that reads it — and no model
family reads it yet.

### 14. The fixture pack's top-age artefact — `validation`

**Value: low. Effort: low.** The synthetic pack's population table stops at the same age as the
config's `age_range`, so anyone reaching the top age leaves the cohort and the pack's simulated
death rate runs above what its mortality table implies. Recorded in the pack's own `SYNTHETIC.md`.
Extending the pack's age range by a few years above the configured one would remove the artefact;
nothing depends on it, because no test reads the pack's death rates as a check on anything.

### 15. Report four things upstream — `docs`

**Value: low here, high upstream. Effort: low.** Four findings belong to the people who own the
data and the baseline, and telling them is not done:

1. **`KevinHall_India` cannot be run by its own baseline** — its `Weight` lower bound is above what
   its weight quantile curve produces (item 3), and its `new_config.json` contradicts itself
   between the deprecated root `trend_type` and `project_requirements.trend.type`, which the
   baseline's own validator refuses.
2. **An intervention scenario is inert on the FINCH model surface** (item 7): `Scenario::apply` has
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

Each is reproducible from this repository with one command, which is most of the work of a good
bug report.

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
