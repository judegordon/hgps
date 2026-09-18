# The `HLM_India` baseline references

Two stored reductions of the **baseline's** output, so that the `HLM_India` comparison can be
reproduced without building or running the baseline. Each is keyed by the SHA-256 of the derived
baseline config with the seed removed, which is why the file names are hashes: a reference cannot be
matched to a config it did not come from.

**Both are at `--size-fraction 1e-5`, which is one hundredth of the cohort this example ships.** That
is part of the derived config and therefore part of the hash, so a full-scale run will not find these
and will fall back to running the baseline rather than comparing against the wrong thing.

| Hash | Intervention | Seeds | Bands excluded | Result |
|---|---|---:|---:|---|
| `bf19f4a6…` | `simple` | 20 | 1,641 | 0 of 67,885 out of tolerance |
| `e58181ab…` | `food_labelling` (the example's own) | 20 | 1,657 | 3 of 68,083 — deviation **B-24** |

Reproduce either with:

```bash
tests/equivalence/run.py --example HLM_India --seeds 20 --size-fraction 1e-5 --use-reference
tests/equivalence/run.py --example HLM_India --seeds 20 --size-fraction 1e-5 --use-reference \
    --intervention simple
```

**The `food_labelling` one is expected to fail**, and that is the point of keeping it. `HLM_India` is
the only example that ships an active intervention, and the one it ships is the policy where the
baseline re-applies its impact to a person who failed an early coverage draw and passed a later one
(deviation B-24). This build applies it once. The difference is about +0.2% of mean BMI in the
intervention scenario and it is in the reference, so anyone can see it without running the baseline.
[docs/equivalence.md](../../../../docs/equivalence.md) has the year-by-year curve and the reasoning.

`scripts/check.sh` and CI do not run either of these: they run the two primary comparisons, which are
`HLM_France` and `KevinHall_FINCH`.

**The 60-seed references are not here.** They are 11 MB each gzipped, against 3.5 MB for these, and
the same rule was applied to `HLM_France` and `KevinHall_FINCH` — confirm at 60 seeds, keep the
20-seed reference. [docs/backlog.md](../../../../docs/backlog.md) item 10 is the alternative: store
the reduction rather than the raw results, which is two orders of magnitude smaller.
