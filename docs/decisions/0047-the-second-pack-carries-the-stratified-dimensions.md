# 0047 — The second fixture pack carries income, region, ethnicity and sector

## Status

Accepted, 2026-09-19. Extends [ADR 0044](0044-two-fixture-packs-and-a-parameterised-suite.md),
which introduced the second pack, and is constrained by
[ADR 0046](0046-what-runs-under-which-sanitizer.md), which runs only the first under
ThreadSanitizer.

## Context

The previous run found that 49 columns of every income-stratified result file were identically zero
in this build and filled in the baseline's, and that they had been so for as long as this build has
written those files. Four were fixed; 45 were left, and the reason nothing had noticed was recorded
as three findings that are one thing:

- **the equivalence harness compared one file per run** — `find_result_csv` existed precisely to
  exclude the stratified ones;
- **no test ran the income series at all**;
- and the second of those has a cause that no amount of test-writing would have fixed: **neither
  synthetic pack assigns an income category**. Both were `HLM`, and only the `StaticLinear` family
  gives a person a `person.income`. There was nowhere to put such a test.

The same is true of three more dimensions. `person.region` and `person.ethnicity` are assigned by
the demographic module from prevalence tables that are read out of a **`StaticLinear` model file**
and nowhere else; `person.sector` comes from that file's `RuralPrevalence`. So four of the columns
that appear in a result file — `mean_income_category`, `mean_region`, `mean_ethnicity`,
`mean_sector`, and their four standard deviations — were reachable by no fixture, and were exercised
end to end only by the two upstream examples that the equivalence harness runs.

That is a thin place to stand. The harness needs the baseline binary and the upstream data, neither
of which CI has; `scripts/check.sh` runs it and a contributor without a built baseline does not. So
the columns with the most output surface had the least test coverage, and the run that measured it
found 45 empty ones.

## Decision

**The second fixture pack's static model becomes `StaticLinear`**, with a categorical income model
per category, a `RegionFile`, an `EthnicityFile`, a `RuralPrevalence` block and a simple
physical-activity model — and its `project_requirements` switch on every dimension those make
possible, including `income_based_csv_output`.

It keeps its `EBHLM` dynamic model. The two model slots are independent in the loader, and the
pack's active intervention has to reach a dynamic model that applies it, which `EBHLM` does and
`KevinHall` does not (deviation **B-25**). Pairing `StaticLinear` with `EBHLM` is not a combination
any upstream example uses; it is a combination this engine accepts, and a fixture is the right place
to find out that it works.

The first pack is **unchanged**, and deliberately so. It stays `HLM`, assigns none of these, and
writes no stratified files. That is what makes
`TestSimulation.AChannelExistsOnlyWhenAModelActuallyAssignsIt` a test of both halves of its own
rule: one pack must gain the columns when the requirement is switched on and the other must not.
Until this run both packs were the "must not" half, which is a rule that would pass if the column
could never appear at all.

## Consequences

**Every output family this engine can write is now produced by a fixture**, and that sentence is a
test rather than a claim: `hgps::output::all_output_families()` enumerates them and
`OutputFamilies.EveryFamilyTheEngineCanWriteIsProducedByAFixture` fails if one has no pack behind
it. A new output family is therefore work in three places — the enumeration, a fixture, and the
equivalence harness's own family list — which is the cost that was missing when the stratified files
were added.

**Nothing about the packs is tested by name.** A test that needs to know what a pack assigns reads
its static model file's `ModelName`, and one that needs to know how many stratum files to expect
reads `project_requirements.income` out of the config. That is ADR 0044's rule and this change is
where it earns its keep: the facts a test needs about the second pack are now numerous, and every
one of them is asked rather than assumed.

**Under ThreadSanitizer the stratified families are not produced at all**, because only the first
pack runs there (ADR 0046) — and the enumeration test has to say so rather than assert the whole
list. It got that wrong on the first attempt and ThreadSanitizer caught it: with one pack,
"every family the engine can write is produced by a fixture" is false, and asserting it there is
asserting something about the sanitizer.

What it does now is derive the expectation from **each pack's own configuration** — a pack produces
the stratum family when its `project_requirements.income` enables the stratified output — and then
assert two things separately: that what the packs are configured to produce is what they actually
write, everywhere; and that the configured set is the *whole* enumeration, where both packs run.
The second is the claim this suite exists for, and it is the one that fails when a family is added
with no fixture behind it. The sibling test states the same thing from the other side, asserting
with one pack that the stratum family is *absent*, which is also a check that the first pack has not
quietly acquired an income model.

**A dead condition became live, and the column order had to be protected.** Setting
`AssignedAttributes::region` and `::ethnicity` — which nothing had ever set — made the branch in
`initialise_output_channels` that reads them fire for the first time, and it sat *before* the
mapping loop. `KevinHall_FINCH` declares `Region` and `Ethnicity` as level-0 risk factors, so its
two columns had always come from that loop, and firing the branch first moved them: its result file
changed. The block is now after the mapping loop, where it adds nothing for a configuration that
names the factors and adds the columns for one that does not. Both examples are byte-identical
before and after, which is the check that the position is right.

**The pack's numbers are still invented.** A `StaticLinear` model whose Box-Cox `Lambda` is 1 and
whose intercept is 0 puts every person within a fraction of the FactorsMean value for their age and
sex. That is a fixture that runs, not a fit; `SYNTHETIC.md` says so of the whole pack and it is no
less true of this part of it.

## Alternatives considered

**A third pack.** It would leave the second pack's 90-odd parameterised tests untouched and cost no
churn. Rejected because it would make the parameterised suite half as fast again for coverage the
second pack could carry, and because the second pack exists to be the one where nothing is safe to
assume — a third pack would dilute that rather than extend it.

**Assigning income from the demographic module instead.** It would let an `HLM` pack produce
stratified output, which is tempting and wrong: upstream assigns income in the static model, and a
fixture that reached the income series by a route no real configuration uses would be testing this
project's own invention.

**Leaving the fixtures alone and testing `calculate_income_based_series` directly.** That is what
the previous run did, and `tests/model/analysis_income_test.cpp` is still the right place for the
per-column assertions — a cohort built by hand is how a column gets pinned to a number. What it
cannot do is run the writer, the channel list, the config loader and the demographic module
together, which is where the 45 columns went missing.
