# Health-GPS audit

An audit of the upstream Health-GPS microsimulation, its data and examples repositories, and an
earlier deterministic C++ rewrite — carried out to inform a new rewrite. Audit date **2026-09-17**.

**Start with [SUMMARY.md](SUMMARY.md)** — one page covering repository states, build and test
status, the top issues on both sides, and recommended starting points.

## Documents

| Document | Contents |
|---|---|
| [SUMMARY.md](SUMMARY.md) | One-page overview. Read first. |
| [00-inventory.md](00-inventory.md) | Per-folder inventory: paths, versions, sizes, languages, build systems, dependencies, licences, test frameworks. Full baseline licence text. Build and test results, including the four deviations needed to build on macOS. |
| [01-baseline-architecture.md](01-baseline-architecture.md) | Subsystems and responsibilities, the simulation lifecycle from config load to output write, model components, config schema, I/O formats, threading model, RNG usage. Mermaid module-dependency and sequence diagrams. |
| [02-data-and-examples.md](02-data-and-examples.md) | How the baseline locates and loads data, the data inventory keyed to consuming component, what each of the six examples demonstrates, and eight confirmed broken/stale/inconsistent items. |
| [03-baseline-determinism.md](03-baseline-determinism.md) | Twenty nondeterminism sources with file:line references, and a six-experiment reproducibility study against the built binary. |
| [04-baseline-issues.md](04-baseline-issues.md) | Nineteen confirmed baseline findings with severity and confidence, detailed for the six high-severity items. |
| [05-rewrite-mapping.md](05-rewrite-mapping.md) | Baseline-to-rewrite component map, marked unchanged-in-intent / restructured / merged / split / removed / added. Mermaid module diagram of the rewrite. |
| [06-rewrite-changes.md](06-rewrite-changes.md) | Each significant change: what the baseline did, what the rewrite does, why, and whether behaviour is preserved, intentionally altered, or possibly altered by accident. Covers determinism, RNG, parallelism, data structures, I/O, config, and the status of all nineteen baseline issues. |
| [07-rewrite-data-examples.md](07-rewrite-data-examples.md) | Which data and examples the rewrite ships, how they differ from upstream, and what is missing. |
| [08-rewrite-issues.md](08-rewrite-issues.md) | Fifteen findings in the rewrite, split into issues it introduced and upstream fixes it lacks. |
| [09-ideas-and-questions.md](09-ideas-and-questions.md) | Prose. Ideas worth carrying forward, ideas to drop, and seven questions needing a ruling before the new rewrite starts. |

## Conventions

**Severity** — `critical` (wrong model outputs or crashes), `high` (nondeterminism, undefined
behaviour, data-handling errors), `medium` (robustness, performance), `low` (style,
maintainability).

**Confidence** — `confirmed` (demonstrated by test, sanitizer, or unambiguous code reading),
`likely` (strong code evidence, not demonstrated), `suspected` (worth checking).

Compiler warnings, clang-tidy, cppcheck and the sanitizers were treated as leads only. Nothing
appears as a finding without confirmation by reading the code.

**Rationale marking** — reasons for the old rewrite's changes are marked `original` only where
evidenced by something in that repository, and `inferred` otherwise. Because the rewrite has no git
history, no notes and 19 comment lines in 13,059, nearly everything is necessarily `inferred`; this
is stated explicitly in [06](06-rewrite-changes.md) §1 rather than glossed over.

## Method and limitations

The four source folders were treated as read-only and were not modified. Both the baseline and the
old rewrite were built out-of-tree under `/tmp/hgps-audit-build/` with vcpkg pinned to the
baseline's declared builtin-baseline, and both were run. The baseline required four audit-only
shims to compile on macOS, described in [00](00-inventory.md) §4.2; the resulting binary is
therefore not a stock baseline binary, which is noted wherever a measurement depends on it.

**No source folder is a git repository.** The tasks that depended on version history — dating the
rewrite's changes and diffing it against upstream commits since divergence — could not be performed
as specified, and were done by differential code reading instead. Where a conclusion rests on that
inference rather than on evidence, it says so.
