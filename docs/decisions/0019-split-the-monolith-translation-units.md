# 0019 — Split the monoliths into one unit per concern

## Status

Accepted, 2026-09-17. Idea taken from the earlier rewrite.

## Context

Four baseline files carry most of the complexity: `static_linear_model.cpp` (2,615 lines),
`model_parser.cpp` (2,252), `analysis_module.cpp` (2,202) and `datamanager.cpp` (806). The audit
found the first three hard to review as single units, and found real defects hiding in them: three
unused progress counters (B-19), a CDF over an unordered map at line 1978 of a 2,615-line file
(B-05), and a production `std::cout` line reading
`FINISHED ALL THE LOADING REQUIRED CUTIEPIE :)` at `model_parser.cpp:2249` (B-16).

The earlier rewrite split `model_parser.cpp` into eleven units, `analysis_module.cpp` into five, and
`data_manager` into seven. The audit's verdict: "straightforwardly better and should be preserved".

## Decision

One translation unit per concern, with a target of roughly 400 lines and a hard look at anything over
600. The mapping is tabulated in `docs/design.md` §2.1:

- `static_linear_model.cpp` → `model/riskfactor/static_linear/{model,init,income,physical_activity,region_ethnicity,trend}.cpp`
- `model_parser.cpp` → `config/models/{hlm,dynamic_hlm,static_linear,kevin_hall,dummy,shared}.cpp`
- `analysis_module.cpp` → `model/analysis/{module,prevalence,burden,factors,cost,income_strata}.cpp`
- `datamanager.cpp` → `data/{index,countries,demographics,diseases,relative_risk,analysis,registry}.cpp`

Each unit has a header stating what it owns. Shared helpers are named in one place rather than
duplicated per unit.

## Alternatives

- **Keep the baseline's file layout**, which would make the ports a straight copy. Rejected: the
  layout is the thing the audit criticised, and a straight copy of a 2,615-line file also copies the
  conditions that hid a defect at line 1978.
- **Split further, one function per file.** Loses the cohesion that makes a unit reviewable.

## Consequences

More files and more headers; a little more boilerplate. Reviewing a change to income assignment now
means reading a 300-line file instead of finding line 1978 of 2,615. The file boundaries follow the
earlier rewrite's *idea* but not its layout, naming or code, per ADR 0002.
