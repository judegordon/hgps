# 0022 — Validate config in code, not with an embedded JSON-Schema validator

## Status

Accepted, 2026-09-17.

## Context

The baseline validates its config against JSON Schema 2020-12 documents using `jsoncons`, resolving
the `$schema` URL to a **local** copy of the schema shipped beside the executable
(`schema.cpp:42`). The audit found two consequences: the schema and the code can disagree — the
`-s/--storage` option is unusable because the program requires either `-s` or `config.data` while
the schema makes `data` required (B-09) — and a schema violation produces a message like
`Invalid configuration - : Required property 'data' not found.`, with no file, line or column.

The diagnostics ruling (ADR 0007) requires every input problem to carry a level, a closed code and a
location. A general-purpose JSON-Schema validator cannot produce a `IssueCode`, and its locations are
JSON pointers into an already-parsed document rather than lines in the user's file.

## Decision

- The **loader in `config/` is the authority.** It walks the parsed document explicitly, and every
  problem it finds becomes a located `InputIssue` with a closed code.
- `schemas/v2/*.json` is the **published contract**: documentation for humans, and a machine-readable
  description for editors and external tooling. It is not consulted at runtime, and no JSON-Schema
  library is linked.
- The two are kept in step **mechanically**, not by hope:
  `tests/config/schema_agreement_test.cpp` reads `schemas/v2/config.json`, extracts every `required`
  property list and every `additionalProperties: false`, and asserts that the loader rejects a
  document missing each required property and rejects an unknown property at each such level. A
  schema and a loader that drift apart fail that test.
- `nlohmann::json` parse errors are converted at the boundary into
  `IssueCode::json_parse_error` with the byte offset mapped to a line and column.

## Alternatives

- **Embed a JSON-Schema validator**, as the baseline does. One more dependency (ADR 0014), worse
  messages, and the schema-versus-code disagreement the audit already found.
- **Generate the loader from the schema.** Attractive in principle; a code generator is a
  significant piece of machinery, and hand-written validation is where the good diagnostics come
  from.
- **Ship no schema at all.** Loses the published contract that `tools/convert-config` writes against
  and that a config author can read.

## Consequences

A schema change needs a matching loader change, and the agreement test says so. Validation logic is
ours to test, which `tests/config/` does case by case. The `$schema` value is checked for being this
repository's URL and is otherwise not dereferenced — nothing is fetched at runtime.
