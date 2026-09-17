# 0001 — Record architecture decisions

## Status

Accepted, 2026-09-17.

## Context

Most of `docs/audit/06-rewrite-changes.md` had to mark its rationale **inferred**, because the
earlier rewrite recorded none: 19 comment lines in 13,059, no design notes, no history. Every
question of the form "why is this different from the baseline?" had to be answered by reading two
codebases side by side and guessing.

That is the failure this file exists to prevent. The audit also found that the baseline states
ordering requirements in comments (`/* Note: order is very important */`) without saying what
breaks if the order changes, which is the same problem one level down.

## Decision

Every design choice is recorded as an Architecture Decision Record in `docs/decisions/NNNN-title.md`
with the sections **Context**, **Decision**, **Alternatives**, **Consequences**, **Status**.

- A choice ruled by the project owner says so explicitly, and names the audit finding that motivated
  the ruling.
- Where an idea came from the baseline or from the earlier rewrite, Context says which.
- ADRs are append-only. A reversal is a new ADR that supersedes an old one; the old one's Status
  becomes `Superseded by NNNN`.

## Alternatives

- **Comments in the code only.** Where the reason spans several files — determinism, for instance —
  there is no single file to put it in, and the reason is what future readers need most.
- **One long design document.** `docs/design.md` exists and describes the system as it is. It is the
  wrong shape for recording rejected alternatives and the date a choice was made.

## Consequences

Writing an ADR is a cost paid on every design decision. The payoff is that the next audit does not
have to infer anything. `docs/design.md` links to the ADR for each of its claims, so the two stay
tied together.
