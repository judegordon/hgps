# 0025 — `Identifier` compares its string; the hash is for bucketing only

## Status

Accepted, 2026-09-17.

## Context

The baseline's `Identifier` wraps a lower-cased string and a 64-bit `std::hash` of it. Its
`operator==` compares **only the hash** (`identifier.cpp:32-34`), while its defaulted
`operator<=>` compares members in declaration order, i.e. **the string first**
(`identifier.h:70`).

So `==` and `!=` use hash equality, while `<`, `>`, `<=` and `>=` use lexicographic string ordering.
Two identifiers whose hashes collide compare equal under `==` while `std::map`, which uses
`operator<`, correctly keeps them as separate keys — and `a == b && a < b` can be simultaneously
true, which is not a coherent equivalence relation (audit B-04). A 64-bit collision among a few
hundred identifiers is vanishingly unlikely, so this is almost certainly not producing wrong results
today; it is rated high because it is silent, has no diagnostic, and its blast radius is every risk
factor and disease lookup in the model.

The baseline also validates identifiers with `std::isdigit` and `std::isalpha` on a plain `char`,
which is undefined behaviour for any byte ≥ 0x80 (B-03).

## Decision

- `Identifier::operator==` compares **`value_`**, the string. `operator<=>` also compares `value_`,
  so equality and ordering agree by construction.
- `hash()` is retained **solely** as the implementation of `std::hash<Identifier>`, for
  `unordered_map` bucketing, where the container handles collisions correctly. The comment in the
  header says exactly that, because the next reader will wonder why both exist.
- Identifier validation and all other character classification goes through `core::chars::`
  wrappers, which cast to `unsigned char` internally (determinism contract D12). The raw `<cctype>`
  functions are not called anywhere else in the tree, and `tests/core/chars_test.cpp` covers bytes
  ≥ 0x80.
- Identifiers remain lower-cased at construction and keep the baseline's rule — must not start with
  a digit, and may contain only `[a-z0-9_]` — so the ported `Identifier.Test.cpp` expectations stand.

## Alternatives

- **Compare the hash, and document the caveat.** Cheap, wrong, and the baseline's version of this is
  the finding.
- **Drop the hash entirely.** `std::hash<Identifier>` would then hash the string on every lookup;
  the cached hash is a real saving in the disease and risk-factor lookups that run per person per
  year, and keeping it costs nothing once `==` is fixed.
- **Intern identifiers and compare pointers.** Fastest, and adds a global table with lifetime rules,
  for a saving that is not the measured bottleneck.

## Consequences

String comparison for `==` instead of an integer compare, on strings of about a dozen characters,
inside lookups that already do a map traversal. Not measurable. `docs/deviations.md` records the
change against B-04, and the ported identifier tests gain a case asserting that equality and
ordering agree.
