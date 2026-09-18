# 0041 — A deliberate deviation is always switchable, so its effect can be measured

## Status

Accepted, 2026-09-18. Establishes a rule that binds every future deviation. Extends
[ADR 0024](0024-deviations-recorded-baseline-bugs-fixed.md), which decided that baseline defects get
fixed and recorded; this decides what else a recorded deviation must come with.

## Context

This build fixes 22 defects in the upstream baseline that change the numbers, each one recorded in
[docs/deviations.md](../deviations.md) with its evidence and the test that pins it. Until this run
that record was a **claim in prose**: the code did the fixed thing, the baseline did the broken
thing, and the difference between them was whatever the equivalence harness happened to see.

Then the harness saw one. The `HLM_India` comparison put 31 `mean_bmi` comparisons out of tolerance,
and the fourth run's analysis attributed all of them to **B-24** — the food-labelling policy
re-applying its impact to somebody who failed an early coverage draw and passed a later one. The
attribution was careful and it was right: the curve had the shape the defect predicts, it was zero in
the policy's first year because the defect needs a *previous* failed draw, and it reproduced on
`HLM_France` at the same size.

It was still an **argument**, not a measurement. Nothing in the repository could produce the number
"this deviation is worth +0.0311 of mean BMI in 2026" other than by reasoning about which
out-of-tolerance cells looked like it. And the asymmetry is worse than it sounds: an out-of-tolerance
comparison that a deviation explains and one that nothing explains **look exactly the same** in the
harness's output. The only thing separating them is a person reading the numbers and deciding.

That is the wrong place for the distinction to live. A harness that cannot tell a deliberate
difference from a defect will eventually pass a defect off as a deliberate difference, and the more
deviations accumulate the likelier that becomes.

## Decision

**Every deliberate deviation that changes outputs gets a named compatibility flag. With the flag on,
the engine reproduces the baseline's behaviour exactly — bug and all.**

- The flags live in one place, `include/hgps/baseline_compat.h`, as `api::BaselineCompat`: a small
  set with one bit per deviation.
- **A flag is named after its deviation's ID in [docs/deviations.md](../deviations.md)** — `B-24`,
  not `food_labelling_retry`. A manifest carrying `"B-24"` leads straight to the row explaining what
  it restores. A test asserts that every flag has such a row, so a flag cannot be added without one.
- They are **off by default**, everywhere: the fixed behaviour is what this project stands behind,
  and a flag is a request to reproduce a defect on purpose.
- They are reachable from all three surfaces: the config document's `baseline_compat` array, the
  public API's `LoadOptions::baseline_compat`, and the CLI's repeatable `--baseline-compat NAME`.
  The document's and the caller's are **unioned** rather than one overriding the other, because both
  are requests to restore a behaviour and there is no sensible precedence between them.
- **Every run manifest records the set that was on**, always present and empty for an ordinary run —
  so "was this result produced with a deviation restored?" is a question every result file answers.
- An unrecognised flag name is an **error**, not a shrug. A misspelling would otherwise run with the
  fixed behaviour while its author believed they had asked for the baseline's, and the comparison
  would then be read as evidence about the wrong thing.
- Turning a flag on changes nothing else: a compatibility run is an ordinary run, and keeps the
  determinism contract. A test asserts byte-identical output across thread counts with `all` on.

**And the equivalence harness uses them.** Every comparison against the baseline runs with the
compatibility flags **on**, so it tests everything *except* the deliberate deviations and an
out-of-tolerance cell means something is wrong. The affected example is then run **once more with
the flags off**, and the harness reports the size and direction of each deviation's effect per
variable per year as a separate **deviation impact** section — reported, not graded.

## Why this shape

**Why a flag per deviation rather than one "baseline mode" switch.** A single switch would answer
"do the two implementations agree when we stop fixing things?", which is a weaker question than "what
is *this* fix worth?". With 22 output-changing deviations recorded, an aggregate number attributable
to all of them jointly would be nearly useless. Per-deviation flags cost one bit each.

**Why the harness compares with flags on rather than off.** The comparison's job is to find
*unintended* differences, and with flags off every intended one is noise in the same channel. The
fourth run's `HLM_India` result is the argument: 28 of 34 out-of-tolerance comparisons were one known
deviation, which had to be identified by hand before the remaining 6 could be looked at at all. With
flags on, those 28 do not appear, and the 6 are the finding.

**Why the impact is reported and not graded.** A deviation's effect has no right size. B-24's is
+0.2% of mean BMI; the next one's might be zero or might be large. A pass/fail threshold on it would
be a number nobody could justify, and it would fail for the wrong reason the first time somebody
fixed a bigger defect. The figure is evidence for a reader, so it is printed as evidence.

**Why the flag is in the public API rather than an internal detail.** A host that wants to show a
user "here is what our fix to B-24 is worth on your data" needs to run both, and the engine is a
library ([ADR 0032](0032-library-and-a-thin-cli.md)). Making this internal would mean the harness
could measure a deviation and nobody else could.

## What it costs

A branch in the code the deviation lives in, for as long as the deviation is recorded — for B-24,
one `if` around one statement, chosen so that the two spellings sit side by side and a reader sees
both. That is a real maintenance cost and it is paid per deviation, which is the reason this ADR
says *deviations that change outputs* rather than all 45: a difference nobody can observe in the
output needs no flag, and 23 of the recorded deviations are of that kind.

The alternative costs more. Without the flag, the only way to price a deviation is to build a
modified binary by hand, which is exactly the sort of measurement that gets done once, written into
a document, and then quoted for three runs after it stopped being true — which is a thing that has
already happened in this repository ([ADR 0040](0040-a-bounded-search-for-the-long-vectors.md)).

## Alternatives rejected

**Leave it in prose, attribute by hand.** Status quo. It worked once, for one deviation, on one
example, done carefully by somebody who had just read the code. It does not survive a second
deviation showing up in the same comparison, and it gives a reader no way to check the attribution.

**Build a separate "baseline-compatible" binary.** A compile-time switch would avoid the runtime
branch. It also doubles the build matrix, makes the harness's two runs use two binaries that differ
in more than the flag, and means the manifest's engine identity no longer pins the behaviour. The
branch is not on a hot path — it runs once per exposed person per year inside a policy that only
`food_labelling` has — and a measured deviation is worth more than the branch costs.

**Widen the harness's tolerance to admit known deviations.** Rejected on sight, and recorded here
because it is the tempting one. It would make the comparison pass by making it weaker, in exactly
the variable where a real defect would hide, and this project has already been bitten three times by
a harness rule that admitted what it should have flagged ([docs/equivalence.md](../equivalence.md)).

**Exclude the affected cells, as the emptying-age-band rule does.** That rule excludes cells where
*neither* implementation has anything to compare — an empty band has no mean to be wrong about. A
deviation is the opposite case: both implementations have a number and they differ on purpose.
Excluding it would discard the measurement, which is the thing worth having. The exclusions were
reviewed against this distinction when this ADR was written; see
[docs/equivalence-method.md](../equivalence-method.md).

## Consequences

- `docs/deviations.md` gains a column saying which deviations have a flag, and B-24's row names it.
- Any future deviation that changes outputs is incomplete until it has one. That is the rule this
  ADR exists to establish, and the test tying flags to `docs/deviations.md` is half of its
  enforcement; the other half is review.
- The harness's reported figure for a deviation is now a measurement of that deviation alone, taken
  by running the same binary twice on the same seeds.
