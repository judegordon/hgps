# The `HLM_India` baseline references

Two stored reductions of the **baseline's** output, so that the `HLM_India` comparison can be
reproduced without building or running the baseline. Each is keyed by the SHA-256 of the derived
baseline config with the seed removed **and its input paths left relative**, which is why the file
names are hashes: a reference cannot be matched to a config it did not come from. The paths are left
relative deliberately — an absolutised config carries the checkout's location, and a reference keyed
by that can only be found on the machine that wrote it, which is what the first CI run to reach this
step discovered (docs/build-notes.md).

**Both are at `--size-fraction 1e-5`, which is one hundredth of the cohort this example ships.** That
is part of the derived config and therefore part of the hash, so a full-scale run will not find these
and will fall back to running the baseline rather than comparing against the wrong thing.

| Hash | Intervention | Seeds | Bands excluded | Result |
|---|---|---:|---:|---|
| `5e3fda9f…` | `simple` | 20 | 1,641 | 0 of 73,627 out of tolerance |
| `ffe878d7…` | `food_labelling` (the example's own) | 20 | 1,662 | 0 of 73,645 out of tolerance |

Each covers **four output families**: the whole-population CSV and the three income-stratified ones.
The whole-population halves are 66,787 and 66,805, which is what they were when the harness compared
that file alone. **The stratum files of this example are empty on both sides** — `HLM_India` is an
HLM example and nobody in it has an income category — so what the other 6,840 comparisons check is
seven head counts, all zero, agreeing. That is not a check on the income-stratified series and
should not be read as one; `KevinHall_FINCH` is the example that checks those.

Reproduce either with:

```bash
tests/equivalence/run.py --example HLM_India --seeds 20 --size-fraction 1e-5 --use-reference
tests/equivalence/run.py --example HLM_India --seeds 20 --size-fraction 1e-5 --use-reference \
    --intervention simple
```

**Both were regenerated again in the eighth run**, along with the other two, because the reduction
changed again: it now covers every CSV a run writes and its key begins with the output family, so
the stored format gained a `family` column (docs/equivalence-method.md §2.1). A reference written
before that is refused by name rather than failing on its first row. The whole-population comparison
counts did not move — 66,787 and 66,805 are what the seventh run's re-score produced — which is the
evidence that what changed is what is compared and not how.

The seventh run regenerated them for a different reason: the four weight categories are head counts
and were being count-weighted, and a reference holds *reduced* values.

**The `food_labelling` one used to be expected to fail, and is not any more.** `HLM_India` is the
only example that ships an active intervention, and the one it ships is the policy where the baseline
re-applies its impact to a person who failed an early coverage draw and passed a later one
(deviation B-24). Since ADR 0041 the comparison runs with the compatibility flags **on**, so this
build reproduces that behaviour and the comparison is clean; the deviation is then measured by a
second pass with the flags off, which is reported rather than graded. On this reference that pass
puts **194 series apart and 12,533 in agreement** to the baseline's printed precision.
[docs/equivalence.md](../../../../docs/equivalence.md) has the year-by-year curve and the reasoning.

`scripts/check.sh` and CI do not run either of these: they run the two primary comparisons, which are
`HLM_France` and `KevinHall_FINCH`.

**The 60-seed references are not here.** They are about 11 MB each gzipped, against 3.8 MB for
these, and
the same rule was applied to `HLM_France` and `KevinHall_FINCH` — confirm at 60 seeds, keep the
20-seed reference. [docs/backlog.md](../../../../docs/backlog.md) item 8 is the alternative: store
the reduction rather than the raw results, which is two orders of magnitude smaller.
