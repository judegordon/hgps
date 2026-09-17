# 0003 — The four source folders are read-only; the baseline builds out of tree

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

## Context

`hgps_main`, `hgps_main_data`, `hgps_main_examples` and `hpgs_og_rewrite` are not git repositories
(audit §1 of `00-inventory.md`): there is no history, so an accidental edit cannot be diffed or
reverted. The audit itself demonstrated the hazard — four `config.json` files in
`hgps_main_examples` were modified by a symlinked scratch directory and could not be restored
byte-exactly (audit §4.5b).

The baseline is nevertheless needed as a runnable binary, for the equivalence harness.

## Decision

Ruled by the project owner: the four source folders are read-only. Nothing in them is edited,
reformatted or written to, including by indirection through symlinks. The baseline is configured and
built out of tree under `/tmp/hgps-build/`, with its shims in `/tmp/hgps-build/shim/`.

Consequently:

- Converted example configs live in `examples/` in **this** repository, and reference the upstream
  model CSVs and JSONs by relative path back into `hgps_main_examples` rather than copying them.
- Fixture packs are generated into `tests/fixtures/`, never into the upstream data tree.
- Scratch and build output go to `/tmp/hgps-build/` or the session scratchpad.

## Alternatives

- **Fix the upstream examples in place** (e.g. `KevinHall_FINCH`'s missing
  `Finch_residual_policy_covariance.csv`, audit D-02). Tempting, and forbidden: it would make this
  run's results depend on an unrecorded edit to a tree with no history. The converted config points
  at the `S1_`-prefixed file that does exist instead, and `docs/examples.md` records that.
- **Copy the four trees first, then edit the copies.** 400 MB of duplication to avoid a rule that
  costs nothing to follow.

## Consequences

`examples/` configs are not self-contained: they only work where `hgps_main_examples` is present at
the expected relative path. That is recorded in `docs/examples.md`, and it is why the synthetic
fixture pack exists for CI.
