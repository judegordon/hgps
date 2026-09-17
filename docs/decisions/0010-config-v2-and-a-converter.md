# 0010 — Define config v2 and ship a v1→v2 converter

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

## Context

Upstream ships two config shapes per example: a legacy `config.json` and a modern
`new_config.json`, and **only the latter carries `project_requirements`** — the block most current
baseline behaviour is gated on. No primary `config.json` has it (audit D-03), so every example
exercises the legacy path while the modern path is reachable only through a secondary file, and
which file is canonical is undocumented. The baseline's own planning document lists adding the block
as unticked work.

The earlier rewrite changed four things incompatibly — required `project_requirements`, dropped
`version`, made `seed` a scalar, changed the `$schema` contract — with the result that **no upstream
example runs unmodified**, and shipped no converter. That is why its output cannot be compared with
the baseline's on a shared configuration.

Other config-level defects: `output.file_name` is ignored unless it contains a `{…}` token (B-08),
the `{TIMESTAMP}` token forces every run to a new path and obstructs regression testing, an absent
`running.seed` silently produces an irreproducible run (B-06), and `${VAR}` expansion of an
undefined variable silently yields an empty string (N-17).

## Decision

Ruled by the project owner: define **config v2** in this repository's `schemas/v2/` and ship
`tools/convert-config`, which reads upstream v1 configs — both the `config.json` and
`new_config.json` variants — and emits v2. Design decisions, all ruled:

| | |
|---|---|
| `project_requirements` | **required**, with explicit defaults documented in the schema |
| `version` | kept (`const 2`) |
| `running.seed` | a **required scalar**; no unseeded runs, ever |
| `$schema` | points at this repository |
| `output.file_name` | used **exactly** as configured; `{TIMESTAMP}` optional, not forced |
| `sync_timeout_ms` | **removed** (ADR 0009) |

Plus, as consequences of the above: `additionalProperties: false` throughout so a misspelled key is
an error; `data.checksum` required when the source is a URL or zip (ADR 0011); an undefined `${VAR}`
is an error; and `population_impact_fraction` is accepted by the schema but rejected at load with
`IssueCode::feature_not_implemented` while PIF is out of scope (ADR 0021).

The converter fills `project_requirements` from `new_config.json` when one exists beside the input,
otherwise from the legacy `trend_type` / `income_categories` fields plus documented defaults, and
reports in its output exactly which route each block took. **The six converted upstream examples are
acceptance tests.**

## Alternatives

- **Accept upstream configs as they are.** Keeps `seed` optional, keeps the ignored `file_name`, and
  keeps `project_requirements` optional — i.e. inherits the defects the ruling exists to remove.
- **A new format with no converter**, as the earlier rewrite did. Costs the ability to run the
  upstream examples at all, and with it every acceptance test and the equivalence harness.
- **Accept both formats in the loader.** Two live paths, one of them legacy, tested by nobody. The
  converter puts that translation in one auditable place that prints what it did.

## Consequences

Upstream examples must be converted before use, which is one command and is scripted. The converter
is itself code that needs tests. A v1 config carrying `sync_timeout_ms` converts with a warning
rather than failing, because dropping a key the user wrote is worth a sentence of output.
