# 0018 — No swallowing catch blocks; every name is validated at load time

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

## Context

The earlier rewrite contains twenty `catch (const std::exception &)` / `catch (...)` blocks that
continue. The worst (audit R-05) sits in derived-predictor resolution and **substitutes an expected
value when a predictor lookup fails**, so a misspelled coefficient name in a model JSON produces
plausible numbers rather than an error. The same codebase contains a good diagnostics system that
these sites do not use.

The baseline has the same class of problem in a milder form: model parsing prints
`Missing key "policy_start_year"` with no file name, no severity, and no statement of what value was
used instead (audit D-08).

Both behaviours are unacceptable in a program whose output informs policy: a wrong number that looks
right is worse than a failure.

## Decision

Ruled by the project owner:

- **No swallowing catch blocks.** No `catch (...)`, and no `catch (const std::exception &)` that
  continues with a substituted value. The only catch sites in the tree are: the top of `main`, which
  reports and exits; and adapters around third-party calls that immediately convert the exception
  into a located `InputIssue` (for example a `std::stod` failure on a CSV cell becoming
  `IssueCode::csv_bad_value` with the file, line and column).
- **Unknown predictor names and metadata rows are rejected at model-load time** with a located
  diagnostic. Every coefficient name in every model JSON and model CSV is checked against the set of
  registered risk factors and derived predictors *before the simulation starts*, and an unknown name
  is an error naming the file, the JSON pointer or CSV field, and the nearest known name.
- A missing key is never silently defaulted. Where a documented default exists it is applied
  explicitly and reported as a warning naming the key, the value used and the file.

## Alternatives

- **Fail on the first unknown name.** Simpler, and it makes the user re-run once per typo — the
  behaviour ADR 0007 exists to remove.
- **Warn and continue on unknown names.** This is effectively what the earlier rewrite does. A
  warning that is followed by a plausible number gets scrolled past.
- **Check names lazily, at first use.** Moves the failure into the middle of a long run, after the
  output file has been created.

## Consequences

Model loading does more work up front and needs the full factor set before any model is parsed,
which fixes the order of operations in `app`: risk factors are registered, then model definitions are
loaded. Typos that the baseline tolerated now stop the run — including, potentially, in upstream
example model files. Any such case is recorded in `docs/examples.md` rather than worked around.
