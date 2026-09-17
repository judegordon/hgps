# 0007 — Two-tier diagnostics: internal errors thrown, input issues accumulated

## Status

Accepted, 2026-09-17. **Ruled by the project owner.** Idea taken from the earlier rewrite.

## Context

The baseline has one exception type, `core::HgpsException`, which carries a `std::source_location`
and is used for everything — a programmer error and a typo in the user's config are the same kind of
event. The practical consequence is that a user with five config mistakes discovers them one run at
a time, and none of them is located.

The audit found that the earlier rewrite's structured diagnostics system is "the single best thing
in it" (`09-ideas-and-questions.md` §1): `InternalError` carrying a source location and thrown,
versus `InputIssue`/`InputIssueReport` carrying a level, a closed error code, an optional
file/field/line/column and a message, accumulated rather than thrown. Its limitation is reach — it
covers config and model parsing but not CSV loading or data-index resolution, which is where large
data trees actually go wrong.

The same rewrite then undercut the design with twenty swallowing `catch` blocks, one of which
substitutes an expected value when a predictor lookup fails (R-05).

## Decision

Ruled by the project owner: reimplement the two-tier design, and thread it through **config, model
JSON, CSV loading and data-index resolution from day one**.

- `diag::InternalError` — broken invariant or contract violation inside this codebase. Thrown,
  carries `std::source_location`, caught only at the top of `main`.
- `diag::InputIssue` — a problem with user input. Accumulated in a `diag::IssueReport`. Carries
  `level` (`error` | `warning`), a value of the **closed** `diag::IssueCode` enum, an optional
  `IssueLocation` (file, JSON pointer or CSV field, line, column) and a message. All issues are
  reported together.
- **No swallowing catch blocks.** No `catch (...)`, and no `catch (const std::exception &)` that
  continues with a substituted value. An unknown predictor name or metadata row is an error at
  load time with a located diagnostic — never a plausible number.
- Defaults are applied explicitly and reported as warnings, never silently.

A closed enum rather than free-text codes is the point: tooling and tests can assert on
`IssueCode::model_unknown_predictor` without parsing prose.

## Alternatives

- **`std::expected<T, Error>` everywhere.** Composes badly when a loader wants to keep going and
  collect the fifth problem as well as the first, which is the whole objective.
- **One exception type, as the baseline has.** Rejected: it is why baseline users see one error per
  run.
- **A logging library.** A log line is not a structured, coded, located issue, and cannot be
  asserted on.

## Consequences

Every loader signature takes an `IssueReport &`. That is visible plumbing, and it is the cost of
reporting everything at once. Callers must check `report.has_errors()` at defined points — after
config load, after model load, after data resolution — and those points are named in
`docs/design.md` §3.
