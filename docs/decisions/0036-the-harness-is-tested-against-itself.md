# 0036 — The equivalence harness is tested against itself, including a deliberately wrong build

## Status

Accepted, 2026-09-18.

## Context

`tests/equivalence/run.py` decides this project's headline result. That is an uncomfortable position
for a 1,100-line script to be in, and the discomfort is not hypothetical: **two of its rules have
been wrong, once each.**

- An early version of the lattice rule bucketed values at full double precision, so a series whose
  values differed in the fifteenth digit counted as continuous — and the rule therefore did not fire
  on the series it existed to catch.
- An early version of the emptying-band exclusion was applied to one side only, which biased every
  comparison that used it.

Both were found by running the whole thing for twenty minutes and reading the output, not by a test.
That is the wrong way round, because **a mistake in a harness does not produce a wrong number: it
produces a confident one.** A harness that is too tight sends somebody looking for a defect that is
not there. A harness that is too loose blesses one that is.

The previous run added `tests/equivalence/run_test.py` — 26 tests of the individual rules, running
in milliseconds under CTest. Those are worth having and they are not enough. They pin the arithmetic
of `quantile`, `lattice_keys`, `modal_share` and `fisher_exact_two_sided`; they say nothing about
whether the assembled thing, run end to end against real output, passes when it should and fails when
it should.

## Decision

**Two end-to-end self-checks, both using `run.py`'s own `compare` and `report`.**
`tests/equivalence/self_check.py` imports them rather than reimplementing them, because the point is
to test the rules that decide the real result and not a second set that resembles them.

### (a) The same build against itself, at two disjoint seed sets

Seeds 1–20 on one side, 1001–1020 on the other, same binary, same config. The two sides differ by
nothing but sampling noise, which is **exactly the null hypothesis every allowance in the method is
derived under**. This must pass, and a failure here is a false positive in the real comparison.

The pairing between the two seed sets is arbitrary — seed *i* against seed *i* + 1000 — and has to
be. The comparison is between two distributions; a pairing that meant something would make it a
different test.

### (b) The same build against a deliberately perturbed copy of itself

A run-time knob, `--perturb`, transforms named output channels after the simulation and before the
writer. The specification the test uses:

```
mean_bmi=scale:1.01;mean_energy=scale:1.05;emigrations=step:1
```

Three channels, three different reasons: a **1%** shift in a continuous, nearly deterministic
aggregate, which is the smallest difference anybody would call a difference and the one an allowance
derived from across-seed noise is most at risk of swallowing; a **5%** shift as a control, so that a
1% failure with no 5% failure would be read as a broken test rather than a working harness; and one
person added to **one** age band, which is one whole **lattice step** of a counted series — the case
the lattice rule exists for, where quantiles are not compared numerically at all, so what has to
catch it is the mean and the exact distribution test that replaced them.

**The test passes only when the set of variables with failures is exactly those three.** A missing
one means the harness cannot see a difference that is really there. An extra one means the
perturbation leaked into a series it does not name, which would make the whole result untrustworthy
in the other direction — and that is a real risk, because `count` is the reduction's weight, so
perturbing it would move every weighted mean in the file. (Which is why `count` is not one of the
three.)

### The perturbation cannot be mistaken for an ordinary run

Four things, and they are the reason a deliberately-wrong-output knob is acceptable inside the
engine at all:

1. It is **off unless asked for**: an empty specification is the default, and no example config sets
   it.
2. It is **refused unless it parses**, and refused unless every channel it names exists in the
   output. A typo is an error, not a silently unperturbed run — which would make the failure test
   pass for the wrong reason, the single worst outcome available here.
3. Every **run manifest records it**, and records `null` when it was not set
   ([ADR 0034](0034-a-run-manifest-beside-the-results.md)). "Was this output perturbed?" is answered
   by every manifest the program has ever written.
4. It lives on `RunOptions`, not in a config file, so it cannot travel with a scenario.

### Where they run

Both default to the **synthetic fixture pack**, where a run is about sixty milliseconds, so twenty
seeds of both sides is a couple of seconds. That makes them ordinary CTest tests inside
`scripts/check.sh` rather than something somebody remembers to do. `--example HLM_France` runs the
same two checks against a real example in about ten minutes, which is what
[docs/equivalence.md](../equivalence.md) reports.

### And the rules go in one document

[docs/equivalence-method.md](../equivalence-method.md) holds the method — the reduction, the
exclusions, the allowance and its derivation, the lattice rule, the verdict, and the self-checks — so
that a reviewer can check what the comparison does without reading the script.
[docs/equivalence.md](../equivalence.md) keeps the results and the story of how two of the rules turned
out to be wrong, which is evidence about the method rather than part of it.

## Alternatives

- **More unit tests of the rules.** Already there, and they cannot answer either question: a rule can
  be individually correct and assembled wrongly, and that is what happened both times.
- **A synthetic pair of result files, hand-written, with a known difference.** Cheaper and weaker. It
  tests `compare` on inputs somebody chose, which is what `run_test.py` already does; it does not test
  the path from two real runs through the reduction and the exclusion to a verdict, and the exclusion
  is where one of the two mistakes was.
- **A second implementation of the statistics, cross-checked.** The usual answer for a calculation you
  do not trust, and the wrong one here: two implementations of the same misconception agree.
- **Perturb by patching the CSV after the run, outside the engine.** Tempting — it keeps the knob out
  of the engine entirely. Rejected because the harness finds the result file by looking, reduces it,
  and applies the emptying-band exclusion: a post-hoc patch would have to reproduce enough of that to
  know which column and which band it was editing, and a bug in *that* would be indistinguishable
  from a bug in the harness. The knob is 120 lines inside the thing that already knows the shape of
  its own output.
- **A separate perturbed build, behind a compile flag.** Removes any chance of the knob being set
  accidentally, at the cost of a second full build in the check and a code path that is compiled in
  one configuration and not the other — which is how a feature stops being exercised. The manifest
  record and the refusal-on-typo give the same protection for none of that cost.
- **Assert only that *something* failed.** Would pass a harness that had become oversensitive and now
  failed on everything, which is the failure mode check (a) exists to catch. The set of failing
  variables has to be checked exactly, in both directions.

## Consequences

`scripts/check.sh` and `ctest` grow two tests that run the simulation, rather than only unit tests.
They are seconds, and they are the only tests in the suite whose subject is the *harness* rather than
the implementation.

`RunOptions::perturbation` is a field on the public API whose only purpose is to make a test fail. That
is a real cost — a published surface with a test-only member — and it is documented as such in
[docs/api.md](../api.md). The alternative was a build flag, above.

`docs/equivalence.md` loses its two method sections to
[docs/equivalence-method.md](../equivalence-method.md) and links to them instead. Splitting a document
somebody may have bookmarked is a cost; having the derivation of a threshold in two places would be a
worse one.
