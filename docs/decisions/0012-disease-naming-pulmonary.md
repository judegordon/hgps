# 0012 — `pulmonary` is canonical; the registry is validated against the tree

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

## Context

The upstream data is internally inconsistent about one disease: the directory is
`data/diseases/pulmonary`, `data/diseases/Metadata.json` says `pulmonar`, and upstream examples use
both spellings. `HLM_India/config.json` asks for `pulmonar` and **fails outright** (audit D-01):

```
There are 50 diseases in storage, 35 selected.
Failed with message: Disease code: 'pulmonar' not found..
```

while `KevinHall_PIF/config_smoking.json` says `pulmonary` and works. The earlier rewrite settled on
`pulmonar`, disagreeing with the directory name, so its data set and the baseline's are now mutually
incompatible on this disease.

The deeper problem is not the spelling. It is that a registry and a directory tree can disagree and
nothing notices until a run dies half-way through configuration.

## Decision

Ruled by the project owner:

- The canonical spelling is **`pulmonary`**, matching the upstream directory.
- The disease registry (`diseases/Metadata.json`) is **validated against the directory tree at load
  time**, and **every** mismatch is reported as an input issue — a registry entry with no directory
  (`IssueCode::data_disease_not_in_tree`) and a directory with no registry entry
  (`IssueCode::data_disease_not_in_registry`), each naming the name and the path.

Validation reports all mismatches together rather than stopping at the first, and it runs before any
simulation, so `--dry-run` surfaces the whole set.

## Alternatives

- **Accept both spellings via an alias table.** Papers over an inconsistency in the *data*, and an
  alias table is a place for more of them to accumulate.
- **`pulmonar`**, following `Metadata.json` and the earlier rewrite. Loses against the directory,
  which is what the loader actually walks, and against the examples that work today.
- **Validate only what the config asks for.** Would leave a registry/tree mismatch undetected for
  any disease a given config does not select — i.e. exactly until someone selects it.

## Consequences

Any data tree whose registry does not match its directories now produces diagnostics where the
baseline produced silence or a late failure. Running against the local `hgps_main_data` snapshot
therefore reports the upstream `pulmonar` mismatch on every run; that is correct behaviour and is
recorded in `docs/deviations.md` against finding D-01.
