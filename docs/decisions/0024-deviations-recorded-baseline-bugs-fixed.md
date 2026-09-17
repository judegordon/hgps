# 0024 — Baseline bugs are fixed, not reproduced; every deviation is recorded

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

## Context

`docs/audit/04-baseline-issues.md` lists 19 confirmed findings in the baseline, six of them high
severity. The earlier rewrite fixed twelve, inherited five, half-fixed one and made one worse — but
recorded none of that, which is why the audit had to reconstruct its intent by differential reading.

Two distinct risks follow from fixing bugs in a port. A fixed bug changes the numbers, so a
divergence from the baseline can mean either "we fixed something" or "we broke something", and
without a record there is no way to tell them apart. And a ported test that encodes the buggy
behaviour will fail for the right reason, which looks exactly like failing for the wrong one.

## Decision

Ruled by the project owner:

- **Baseline bugs from `docs/audit/04-baseline-issues.md` are fixed here, not reproduced.**
- **Every behaviour that intentionally differs from the baseline is recorded in
  `docs/deviations.md`**, with the baseline finding ID and the evidence.
- Where a **ported test encodes a baseline bug**, its expectation is changed and the finding ID is
  named in a comment in the test, so a reader of the test sees why it differs from its original.

`docs/deviations.md` has one row per deviation: the finding ID, what the baseline does, what this
implementation does, the evidence, and the test that pins the new behaviour.

## Alternatives

- **Reproduce baseline behaviour exactly, bugs included**, and fix them later behind flags. Would
  mean deliberately writing undefined behaviour (B-03), a data race (B-02) and an inconsistent
  equality operator (B-04).
- **Fix silently.** Cheaper, and it destroys the equivalence harness's ability to attribute a
  divergence.

## Consequences

`docs/deviations.md` must be updated as part of the change that deviates, not afterwards. The
equivalence harness reads it: an out-of-tolerance variable whose suspected cause is a listed
deviation is a different finding from one with no explanation, and `docs/equivalence.md` reports
them separately.
