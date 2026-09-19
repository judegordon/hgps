# Glossary

Every term this repository uses as if you already knew it. One sentence each, and a pointer to
where it is defined properly — the pointer is the point of this file, not the sentence.

Alphabetical. Four terms are used in two senses by different documents and each says so in its own
entry: **family**, **model file**, **reference** and **run**.

---

**ADR** — an Architecture Decision Record: one file per design choice, stating the context, the
decision, the alternatives rejected and the consequences, numbered in the order taken and never
rewritten. → [docs/decisions/README.md](decisions/README.md), [ADR 0001](decisions/0001-record-architecture-decisions.md)

**audit, the** — the review of the upstream baseline, its data and examples, and an earlier rewrite,
carried out before this project started; it is the source of every `B-nn`, `N-nn` and `D-nn` finding
ID used here. → [docs/audit/](audit), starting at [docs/audit/SUMMARY.md](audit/SUMMARY.md)

**band** — one (sex, age) cell of the output: every result file is a table of bands per year per
scenario, and most claims in this project are about a band's mean or standard deviation rather than
about a person. → [docs/equivalence-method.md](equivalence-method.md) §2

**baseline** — two unrelated meanings, both common here: (1) the upstream Health-GPS implementation
this build is compared against, and (2) the no-intervention scenario a policy scenario is compared
against inside a single run. Context always disambiguates; "the baseline binary" is always sense 1.
→ [docs/build-notes.md](build-notes.md) for sense 1, [docs/design.md](design.md) §3 for sense 2

**baseline-compat flag** — see **compatibility flag**.

**byte-identical** — the strongest agreement this project asserts: two runs' output files are equal
byte for byte, which is what the determinism contract promises for the same config, seed, data and
binary at any thread count. → [ADR 0008](decisions/0008-determinism-contract-enforced-by-types.md)

**calibration (weight calibration)** — the step that sets each (sex, age) band's mean weight to the
`FactorsMean` table, which is why a single diverging person moves a band's standard deviation and
never its mean. → [docs/SUMMARY.md](SUMMARY.md), finding 5

**cancellation token** — the handle a host passes to `execute` to stop a run; it takes effect at the
end of the current simulated year rather than immediately, so the output stays well formed.
→ [docs/api.md](api.md), *Cancellation*

**census** — a scan of many seeds of **one** implementation in which every output file of every run
is read rather than only the exit code, used to count how often something happens rather than to
compare two sides. → `tests/equivalence/seed_scan.py`; [docs/SUMMARY.md](SUMMARY.md), *The census*

**`check.sh`** — the one command that checks everything: the ADR index, every build preset and its
tests, the frontend and its browser tests, the equivalence harness against the stored references,
and the column inventory. → `scripts/check.sh`

**cohort** — the synthetic population a run builds from survey microdata and demographic
projections and then ages a year at a time; each implementation draws its own, so the same seed is
not the same cohort on both sides. → [docs/design.md](design.md) §3

**column coverage** — the check that asks of the output files themselves which columns are
identically zero on each side, because "one side has numbers here and the other has nothing" is not
a disagreement a statistical comparison can express. → `scripts/column-coverage.py`;
[docs/equivalence.md](equivalence.md), *Every column of every family*

**compatibility flag** (also *compat flag*, *baseline-compat flag*) — a named switch, one per
deviation that changes a number, which puts the baseline's original behaviour back so the
deviation's effect can be measured rather than argued; flags are off by default and every run's
manifest records which were on. → [docs/deviations.md](deviations.md),
[ADR 0041](decisions/0041-deliberate-deviations-are-switchable.md)

**config v1 / config v2** — v1 is the upstream configuration format; v2 is this repository's, which
every run uses. Upstream examples are read through a converter rather than edited.
→ [ADR 0010](decisions/0010-config-v2-and-a-converter.md), [docs/design.md](design.md) §7

**converter** — `tools/convert-config`, which turns a v1 configuration into a v2 one, rebasing its
paths and optionally checking the result. → [ADR 0010](decisions/0010-config-v2-and-a-converter.md)

**coverage draw** — the per-person random draw deciding whether an intervention reaches somebody in
a given year; deviation B-24 is the baseline re-applying an impact to a person who failed an early
one and passed a later one. → [docs/deviations.md](deviations.md)

**data store / data index** — the content-addressed cache the disease data is fetched into, and the
index that names what is in it; the disease data is not vendored here because of its licence.
→ [ADR 0011](decisions/0011-data-fetched-not-vendored.md), [docs/design.md](design.md) §8

**determinism contract** — the fourteen-clause promise that the same config, seed, data and binary
produce byte-identical CSV output on every run regardless of thread count, each clause enforced by
the type system rather than by review. → [docs/design.md](design.md) §4,
[ADR 0008](decisions/0008-determinism-contract-enforced-by-types.md)

**deviation** — a behaviour here that intentionally differs from the baseline, always carrying the
audit finding ID that motivated it, the evidence, the test that pins it and — if it changes a
number — a compatibility flag. → [docs/deviations.md](deviations.md),
[ADR 0024](decisions/0024-deviations-recorded-baseline-bugs-fixed.md)

**dispersion test** — the half of a continuous series' comparison that tests spread rather than
level (Brown–Forsythe on each sample's absolute deviations from its own median); it is what catches
a change that moves a band's `std_` and leaves its mean alone.
→ [docs/equivalence-method.md](equivalence-method.md) §4

**dry run** — `healthgps --config FILE --dry-run`: validates the config, the model files, the data
index and the disease registry and then stops, without simulating anything.
→ [README.md](../README.md), *Running*

**EBHLM** — the dynamic form of the hierarchical linear model, the surface `HLM_India` runs on.
→ [docs/design.md](design.md) §2; `src/model/riskfactor/hlm_model.cpp`

**energy balance** — the Kevin Hall two-compartment model of body fat and lean tissue, solved in
closed form once per simulated year; the place the tenth run's findings live.
→ `src/model/riskfactor/kevin_hall/energy_balance.cpp`,
[ADR 0049](decisions/0049-the-energy-balance-is-integrated-only-where-it-is-defined.md)

**equivalence** — the claim that two implementations agree, established by a hypothesis test per
(family, scenario, year, sex, variable) rather than by a tolerance on a number, because two Monte
Carlo runs cannot agree exactly. → [docs/equivalence-method.md](equivalence-method.md)

**event stream** — the sequence a host subscribes to while a run executes — run started, scenario
started, year completed, run completed — which the simulation itself cannot see or depend on.
→ [ADR 0033](decisions/0033-an-event-stream-the-simulation-cannot-see.md), [docs/api.md](api.md)

**example** — one upstream configuration and its data, such as `HLM_France`, `HLM_India` or
`KevinHall_FINCH`; there are six and three of them run in both implementations.
→ [docs/examples.md](examples.md)

**failure budget** — a number of comparisons a run was allowed to fail and still pass. There is no
longer one, on any example, and the harness has no flag that could grant one; the eighth run's
budget of 3 is the thing [ADR 0048](decisions/0048-a-comparison-with-a-stated-false-positive-rate.md)
removed. → [docs/equivalence.md](equivalence.md), *Verdict*

**family** — two senses. (1) **Output family**: one kind of result file, such as the whole-population
series or the income-stratified one; every key in a comparison begins with it and Holm corrects
across all of them at once. (2) **Model family**: one of the four risk-factor model surfaces (HLM,
EBHLM, static linear, Kevin Hall). → sense 1 in [docs/equivalence-method.md](equivalence-method.md)
§2, sense 2 in [docs/design.md](design.md) §2

**family-wise false-positive rate** — the probability that a comparison of a build against itself
reports **any** failure at all; it is set to 1% here and was measured on a null before the rule was
adopted. → [docs/equivalence-method.md](equivalence-method.md) §4,
[ADR 0048](decisions/0048-a-comparison-with-a-stated-false-positive-rate.md)

**finding** — something a build run established that was not known before it; each run's are
numbered in that run's summary, and a code comment citing "`docs/SUMMARY.md`, finding N" means the
run that wrote the comment, recoverable with `git log -p docs/SUMMARY.md`. → [docs/SUMMARY.md](SUMMARY.md)

**finding ID (`B-nn`, `N-nn`, `D-nn`)** — an audit finding: `B-nn` a defect in the baseline code,
`N-nn` a determinism defect, `D-nn` a defect in the data or examples. → [docs/audit/04-baseline-issues.md](audit/04-baseline-issues.md),
[docs/audit/03-baseline-determinism.md](audit/03-baseline-determinism.md),
[docs/audit/02-data-and-examples.md](audit/02-data-and-examples.md)

**fixture pack** — a synthetic data pack generated by `tools/gen-fixtures`, whose numbers are
invented and must never be used for analysis; there are two of them, differing in every way a
program might have assumed they did not, and every test that runs a configuration runs against both.
→ [ADR 0044](decisions/0044-two-fixture-packs-and-a-parameterised-suite.md),
[ADR 0047](decisions/0047-the-second-pack-carries-the-stratified-dimensions.md)

**guard** — a bound or a check this build adds where the baseline has none; the two of them behave
oppositely on purpose, one continuing from the edge of the model's domain and one stopping the run.
→ [docs/SUMMARY.md](SUMMARY.md), *The two guards*; ADRs [0049](decisions/0049-the-energy-balance-is-integrated-only-where-it-is-defined.md)
and [0050](decisions/0050-no-output-carries-a-number-that-cannot-exist.md)

**harness** — the equivalence harness: the Python under `tests/equivalence/` that runs both
implementations, reduces and compares their output, and returns a verdict; it has its own test
suite, because a mistake in it says PASS. → [docs/equivalence.md](equivalence.md),
[ADR 0036](decisions/0036-the-harness-is-tested-against-itself.md)

**HLM** — the hierarchical linear model, the risk-factor surface `HLM_France` runs on and the only
surface on which an intervention currently applies. → `src/model/riskfactor/hlm_model.cpp`

**Holm correction** — the step-down multiple-comparison procedure applied once to every p-value from
every test of every output family in a run, which is what turns tens of thousands of tests into one
stated error rate. → [docs/equivalence-method.md](equivalence-method.md) §4

**horizon** — the last simulated year of a run, set by the configuration; a scenario runs from the
start year to it, one year at a time. → [docs/design.md](design.md) §3

**income category / stratum** — the income band a person is assigned on the static-linear surface,
and the dimension the income-stratified output files are cut along; nobody in either HLM example has
one, so `KevinHall_FINCH` is the only example that checks those columns against anything.
→ [ADR 0047](decisions/0047-the-second-pack-carries-the-stratified-dimensions.md)

**intervention** — a modelled policy applied in the second scenario of a run, of which there are six
types; five of them share one age-banded shape. → [ADR 0029](decisions/0029-one-banded-intervention-shape.md),
[ADR 0031](decisions/0031-comparing-one-intervention-at-a-time.md)

**issue** *vs* **internal error** — the two tiers of diagnostic: an `Issue` is something the *input*
got wrong, is located, and is accumulated so a host can show every problem at once; an
`InternalError` is something *this program* got wrong and is thrown.
→ [ADR 0007](decisions/0007-two-tier-diagnostics.md), [docs/design.md](design.md) §6

**Kevin Hall** — the model family that carries the energy balance, the surface `KevinHall_FINCH`
runs on. → `src/model/riskfactor/kevin_hall/`

**lattice series** — a series whose values fall on a discrete grid — head counts, case counts, a band
mean that calibration pins — where normal theory does not apply and a quantile comparison gets
*worse* with more seeds; such a series is detected and compared over its whole discrete
distribution instead, by Fisher's exact test per value with a Bonferroni correction across them.
→ [docs/equivalence-method.md](equivalence-method.md) §5

**level (of a risk factor)** — the position in the generation hierarchy at which a factor is drawn;
factors are generated level by level, and a level-0 factor is one drawn before any other depends on
it. → `src/model/mapping.h`

**located** — carrying the place it came from: a located error names the file, the line and the
field; a located warning names the person, the year and the term. → [ADR 0007](decisions/0007-two-tier-diagnostics.md)

**manifest** — the JSON written beside every run's results, recording the config hash, the data
checksum, the seed actually used, the engine version and commit, the host, the times, the scenarios,
the compatibility flags and every located warning. → [ADR 0034](decisions/0034-a-run-manifest-beside-the-results.md)

**mapping** — the ordered list of a configuration's risk factors with their levels and ranges; its
order matters twice, because factors are generated level by level and in declaration order within a
level. → `src/model/mapping.h`

**migration journal** — the record of net migration and adjustment the baseline scenario produces
and the intervention scenario replays, which is what lets the two scenarios run sequentially instead
of concurrently over a channel. → [ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md),
[docs/design.md](design.md) §3

**model file** — two senses. (1) Upstream and in config: a JSON file describing a fitted model, whose
relative paths resolve against itself. (2) In [docs/READING-GUIDE.md](READING-GUIDE.md): one of the
two C++ files carrying a model surface's arithmetic. → sense 1 in
[ADR 0028](decisions/0028-model-files-resolve-their-own-relative-paths.md)

**null calibration** — running the comparison rule on a case where the answer must be "no
difference" — one build against itself on disjoint seed sets — to measure how often it cries wolf;
30 pairs, 922,564 tests, zero failures against 0.30 expected.
→ [docs/equivalence-method.md](equivalence-method.md) §4.5, `tests/equivalence/calibrate.py`

**pack** — a directory of data files an example runs against; see **fixture pack** for the synthetic
ones. Two upstream packs are byte-for-byte identical to each other, which is why the two examples
that cannot run are one defect rather than two. → [docs/examples.md](examples.md)

**perturbation check** — the half of the harness's self-test that runs it against a deliberately
corrupted build and requires it to **fail**; the other half requires it to pass on two disjoint seed
sets of one build. → [ADR 0036](decisions/0036-the-harness-is-tested-against-itself.md),
`tests/equivalence/self_check.py`

**PIF (population impact fraction)** — the fraction of a disease's burden attributable to a risk
factor, loaded from tables named in the data manifest and validated at load; it has never been
compared against the baseline, because the only example that uses it cannot run.
→ [ADR 0038](decisions/0038-population-impact-fraction.md)

**point mass** — a series that takes the same value in every seed; it has one bucket, so its exact
test has nothing to compare and the comparison says so rather than passing silently.
→ [docs/equivalence-method.md](equivalence-method.md) §5

**pole** — a value at which an expression is undefined because its denominator is zero. The one that
matters here is in the energy balance's partition coefficient `p = C / (C + F)`, undefined at a body
fat mass of `F = −2.001012658227848 kg`; past it `p`, the determinant and the time constant all
change sign and a year's step becomes 652 e-foldings.
→ [docs/findings/seed-80.md](findings/seed-80.md),
[ADR 0049](decisions/0049-the-energy-balance-is-integrated-only-where-it-is-defined.md)

**policy scenario (`S1`..`S7`)** — one of the seven modelled policies the FINCH data pack ships; the
converter selects one with `--policy-scenario`, defaulting to `S1`, because the example's own static
model names a policy file its pack does not contain.
→ [ADR 0030](decisions/0030-policy-scenario-selection-for-the-broken-finch-example.md)

**preset** — one CMake configuration: `release`, `debug`, `asan-ubsan` or `tsan`. Not every test runs
under every one, and the reason is recorded. → [ADR 0004](decisions/0004-toolchain-cpp20-cmake-vcpkg-googletest.md),
[ADR 0046](decisions/0046-what-runs-under-which-sanitizer.md)

**reduction** — the step that turns a run's CSV rows into one series per (family, scenario, year,
sex, variable), which is what a comparison actually compares; what it leaves out is listed rather
than implied. → [docs/equivalence-method.md](equivalence-method.md) §2 and §3

**reference** — two senses. (1) **Stored reference**: the baseline's reduced output for an example,
checked in so that a comparison needs no baseline binary; it is tied to the compatibility-flag
setting it was produced under. (2) The **reference build** of the baseline itself.
→ sense 1 in [docs/equivalence.md](equivalence.md), sense 2 in [docs/build-notes.md](build-notes.md)

**risk factor** — a per-person modelled quantity such as BMI, weight, physical activity or income;
they are keyed by index rather than by name, which is the largest performance decision in the
project. → [ADR 0037](decisions/0037-index-keyed-risk-factor-store.md)

**run** — two senses. (1) One execution of the simulation over its horizon, comprising two scenarios.
(2) A **build run**: one session of work on this repository, each ending in a summary; there have
been ten. → sense 2 in [docs/SUMMARY.md](SUMMARY.md)

**scenario** — one future within a run: the baseline scenario and the intervention scenario, run
sequentially from the same seed. → [ADR 0009](decisions/0009-sequential-scenarios-and-the-migration-journal.md)

**scratch directory** — a per-run working directory that **copies** anything the run may write and
symlinks only what it reads, so that many runs can share one data pack without any of them mutating
it. → [ADR 0039](decisions/0039-scratch-directories-copy-what-they-may-write.md)

**seed** — the integer that determines every random draw in a run; an absent seed is a load-time
error here, because the baseline's behaviour of drawing one from `std::random_device` and then
recording it as `0` is finding B-06. → [ADR 0015](decisions/0015-rng-design.md)

**self-check** — see **perturbation check**.

**series** — one variable's values across the seeds of a sweep, for a fixed (family, scenario, year,
sex); the unit a comparison tests. → [docs/equivalence-method.md](equivalence-method.md) §2

**static linear** — the risk-factor model family that gives a person an income category, a region, an
ethnicity and a sector; `KevinHall_FINCH` runs on it together with the Kevin Hall surface.
→ `src/model/riskfactor/static_linear/`

**surface (model surface)** — the set of model families a configuration selects, and the thing an
equivalence claim is a claim about; there are four families and three runnable combinations of them.
→ [docs/design.md](design.md) §2

**sweep** — many seeds of both implementations, with the reductions stored so that a study can score
them many ways afterwards. → `tests/equivalence/sweep.py`

**two-step value** — a value that remembers what it was last year, for the models that need both.
→ `src/model/two_step_value.h`

**upstream** — the Health-GPS project this is a reimplementation of, and its authors; an **upstream
report** is a finding written as a bug report against *their* binary and data, reproducible without
this repository. → [docs/upstream-reports.md](upstream-reports.md), [docs/briefing.md](briefing.md)
