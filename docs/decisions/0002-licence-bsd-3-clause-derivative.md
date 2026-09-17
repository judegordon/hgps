# 0002 — This is a BSD-3-Clause derivative of the baseline

## Status

Accepted, 2026-09-17. **Ruled by the project owner.**

## Context

The baseline (`hgps_main`) is BSD-3-Clause, "Copyright (c) 2021, Centre for Health Economics &
Policy Innovation, Imperial College London; INRAE, France." The data repository adds "Copyright (c)
2024, Centre for Health Economics & Policy Innovation".

The earlier rewrite has **no licence file of any kind** (audit finding R-01), while being a
derivative of BSD-3-Clause code whose clauses 1 and 2 require the notice, the conditions and the
disclaimer to be retained in source and binary redistributions. That is the single highest-severity
finding against it.

BSD-3-Clause permits derivative works, including closed ones, provided the notice is retained and
the copyright holders' names are not used to endorse the derived work without permission.

## Decision

Ruled by the project owner:

- This implementation is a **BSD-3-Clause derivative of the baseline**.
- `LICENSE` carries the baseline's notice verbatim — Imperial College London / INRAE, plus CHEPI
  where the baseline's file has it — followed by a clear statement of derivation.
- Files that derive from baseline source carry a short header naming the baseline as the origin.
- The baseline **may** be read, followed structurally, and ported from.
- The earlier rewrite may be read for ideas and described in prose only. Its code, comments,
  identifiers and file layout must not be copied or closely reproduced. The ideas worth
  reimplementing fresh are listed in `docs/audit/09-ideas-and-questions.md` §1.

## Alternatives

- **An independent clean-room implementation.** Would forbid following the baseline's structure at
  all, which throws away the only behavioural reference that exists and makes the 471-test port
  impossible.
- **No licence file**, as the earlier rewrite has. Not an option: it is a licence violation.

## Consequences

Per-file headers are noise on files that derive from nothing, so they appear only where they are
true. Clause 3 means no marketing or documentation here may imply Imperial or INRAE endorsement.
Because the baseline may be followed structurally, the ported tests can keep their expected values,
which is what makes them evidence.
