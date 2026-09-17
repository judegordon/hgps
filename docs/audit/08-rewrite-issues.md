# 08 — Old rewrite: issues

Clear, stand-out problems in `hpgs_og_rewrite`. Same format, severity and confidence scale as
`04-baseline-issues.md`. Every entry was confirmed by reading the code; several are additionally
demonstrated by the built binary.

Two categories are mixed here deliberately, and each finding says which it is:

- **Introduced** — the rewrite made this worse than the baseline, or created it.
- **Diverged** — the baseline has since gained a fix or feature the rewrite lacks. Because
  neither repository has git history (`00-inventory.md` §1), *"since"* is an inference from code
  evidence, not from commits. Where that inference rests on something concrete, the evidence is
  given.

---

## Findings

| ID | Sev | Conf | Kind | Location | Description | Evidence | Fix direction |
|---|---|---|---|---|---|---|---|
| **R-01** | high | confirmed | introduced | repository root | **No licence file anywhere.** The code is a derivative of the BSD-3-Clause baseline, whose clauses 1 and 2 require the copyright notice, conditions and disclaimer to be retained in source and binary redistributions. | `find` over the whole tree returns no `LICENSE`, `COPYING` or equivalent; no per-file headers | Add the baseline's BSD-3-Clause notice and state the derivation |
| **R-02** | high | confirmed | introduced | build system | **No test suite.** No test target, no test directory, no `gtest` dependency. The baseline has 471 tests across 48 suites. | `grep` for `test`/`gtest`/`CTest` across all five `CMakeLists.txt` returns nothing; `vcpkg.json` omits `gtest` | Port the baseline's suite before any further behaviour change |
| **R-03** | high | confirmed | diverged | `src/hgps/data/population.cpp:42,59,66` | **Person IDs are slot-based and are reused.** When a slot is recycled, the newborn inherits the dead person's ID. The baseline assigns lifetime-unique IDs from a monotonic counter. | Code reading; the rewrite's own inherited comment at `:58` says *"Newborn in recycled slot keeps that slot's ID"*, while the baseline's says *"slot reuse != ID reuse"* | Adopt the monotonic counter |
| **R-04** | high | confirmed | introduced | `src/hgps/data/demographic.cpp:344-353` | **The one mutex-guarded floating-point accumulation left parallel.** The rewrite removed this exact pattern from five other sites; this one was missed, so the determinism campaign is incomplete. | Code reading; contrast with the serial `default_disease_model` / `default_cancer_model` in the same tree | Make serial, or use a fixed-order deterministic reduction |
| **R-05** | high | likely | diverged | `src/hgps/data/person.cpp:56-72` | **No derived-predictor resolution.** The baseline's `predictor_resolver` + `linear_model_evaluator` handle `log_*`, `income_*`, age polynomials, region/ethnicity dummies and metadata-row skipping. Neither exists here; an unrecognised predictor name throws. | Code reading; `is_metadata_predictor` appears in 5 baseline files and 0 rewrite files | Decide whether dynamic names are supported; if not, validate model JSON against the dispatcher at load time |
| **R-06** | medium | confirmed | introduced | `src/hgps/utils/random_algorithm.cpp:78-81` | **Modulo bias** in integer sampling: `engine_.next() % range` is non-uniform unless `range` divides 2³². The baseline's float-based version had no such bias. | Code reading; bias magnitude ≈ `range / 2³²` | Rejection sampling |
| **R-07** | medium | confirmed | introduced | `src/hgps_input/models/`, `src/hgps/models/static_linear_model.cpp:727` | **20 swallowing catch blocks** (12 `catch (const std::exception &)`, 8 `catch (...)`). At `static_linear_model.cpp:724-735` a failed predictor lookup is silently replaced with an expected value, so a misspelled coefficient name produces plausible numbers instead of an error. | Code reading; count by `grep` | Catch specific types; record an `InputIssue` rather than substituting silently |
| **R-08** | medium | confirmed | diverged | `src/hgps/data/population.cpp:36-68`, `:70+`; `src/hgps_core/utils/thread_util.h:25` | Three baseline defects inherited verbatim: `add`/`add_newborn_babies` are `noexcept` but allocate (B-12); `find_index_of_recyclables` rescans from index 0 on every call, quadratic in population (B-13); `parallel_for` materialises a full index vector per call. | Code reading | As `04-baseline-issues.md` |
| **R-09** | medium | confirmed | introduced | `src/hgps/simulation/runner.cpp:52-60`; `src/hgps/events/channel.h` | **`sync_timeout_ms` is dead configuration.** With scenarios sequential, the baseline arm buffers every year's message before the intervention arm drains them, so the channel is a queue, not a rendezvous. The config key is still required and still parsed. | Code reading; the rewrite's own `config.json` sets `sync_timeout_ms` | Remove the key, or document that it no longer has an effect |
| **R-10** | medium | confirmed | diverged | `src/hgps_input/config/config.cpp:312-327` | `output.file_name` is **silently ignored** unless it contains a `{…}` token — baseline B-08 inherited verbatim, including the `tk_end > 0` gate. | Code reading | Use the configured name |
| **R-11** | medium | confirmed | diverged | `src/hgps/utils/program_dirs.cpp:47`; `src/hgps/data/population.cpp:22` | **Does not build on macOS** — same `#error "Unsupported platform"` and same `std::execution::par` as baseline B-10. One of three blockers is gone (`std::osyncstream` is not used). | Build log; the same shims were required | Add an `__APPLE__` branch; avoid PSTL |
| **R-12** | low | confirmed | introduced | `src/hgps/simulation/simulation_module.cpp:19,26,33,40,47` | Five `-Wpessimizing-move` warnings — `std::move` on a returned temporary defeats copy elision. These are the only warnings in the build, and the baseline has none here. | Build log | Remove the `std::move` |
| **R-13** | medium | confirmed | introduced | `data/undb/` | **All PIF data removed** while `pif_data.{cpp,h}` and `schemas/config/population_impact_fraction.json` remain. The feature cannot be exercised, and the `KevinHall_PIF` example family has no data. | 0 PIF files; 0 `population_impact_fraction` entries in `index.json` vs 1 in the baseline's | Restore the data, or remove the code and schema |
| **R-14** | medium | confirmed | introduced | `models/`, `data/undb/` | **No provenance for vendored data.** Nothing records which upstream release `data/undb` was copied from, which FINCH scenario the unprefixed policy files correspond to, or where `blood_pressure_medication.csv` and `systolic_blood_pressure.csv` came from. With no checksum and no history, it cannot be traced. | `07-rewrite-data-examples.md` §4 | Record source release and checksums |
| **R-15** | low | confirmed | introduced | whole tree | **19 comment lines in 13,059** (0%, against the baseline's 17%), no README, no design notes. The baseline's Doxygen documentation was dropped along with its build rule. | `grep` count | Document the invariants at minimum |

**Not counted as a finding:** the rewrite's `-Wall -Wextra -Wpedantic` build is otherwise clean, and
its structural work (§`05`, §`06`) is sound. The list above is what a reviewer should act on, not a
verdict on the whole.

---

## Detail — high findings

### R-01 — No licence

The tree contains no `LICENSE`, no `COPYING`, and no per-file copyright headers. The baseline it
derives from is BSD-3-Clause, copyright Imperial College London and INRAE, whose clauses 1 and 2
require that redistributions in source and binary form retain the copyright notice, the list of
conditions and the disclaimer. As it stands the rewrite could not be redistributed in compliance.

The `$schema` URLs in `models/kevinhall_finch/config.json` and throughout `schemas/` point at
`raw.githubusercontent.com/judegordon/hgps`, which is consistent with a personal fork of the
upstream project rather than an independent work.

This is a paperwork problem, not an engineering one, and it is cheap to fix — but it is first on
the list because it is the only finding that affects whether the code can be used at all.

### R-02 — No test suite

The baseline ships 471 tests in 48 suites (10,571 lines) and they all pass. The rewrite has none:
no test target in any of its five `CMakeLists.txt`, no test sources, and `gtest` deliberately
removed from `vcpkg.json`.

This is the most consequential difference between the two codebases, and it compounds every other
finding in this document. The rewrite makes substantial, deliberate changes to numerically sensitive
code — it reimplements integer sampling (§`06` 2.2), changes the softmax to a max-subtracted form
(§`06` 2.5), removes parallelism from five reduction sites, and reorganises 2,252 lines of model
parsing into eleven units. Each of those is defensible. None of them is verified.

The problem is sharper than "untested code". The rewrite's RNG changes mean its output **cannot** be
compared against the baseline's even in principle (§`06` 2.2), so the obvious external check —
diff the two implementations on the same input — is unavailable. And no shared configuration exists
anyway (§`07` 3). The rewrite is therefore unverifiable by any means currently available: not by its
own tests, not against the baseline, not against a recorded expected result.

That it produces byte-identical output across three same-seed runs (`00-inventory.md` §4.6)
establishes that it is *deterministic*. It establishes nothing about whether it is *correct*.

### R-03 — Person IDs are reused

`Population` assigns `slot_index + 1` as the person ID in all three places a person enters the
population (`src/hgps/data/population.cpp:42`, `:59`, `:66`). Slots are recycled when their occupant
dies or emigrates, so a newborn taking a dead person's slot receives that person's ID.

**Why it matters.** The rewrite retains `IndividualIDTrackingWriter`, whose entire purpose is
per-person longitudinal output. Under slot-based IDs, a tracking CSV that follows "person 4,312"
across years is silently following one person up to their death and a different person afterwards.
Nothing marks the transition. Any longitudinal analysis over that file is wrong in a way that will
not be obvious from the file.

**Why this is `diverged` rather than `introduced`.** This is the one place where the rewrite carries
original documentary evidence, and it points at the baseline having changed later. The rewrite's
inherited comments describe slot-based IDs as the intended design:

```
// src/hgps/data/population.cpp:58
// MAHIMA: Newborn in recycled slot keeps that slot's ID (index + 1).
```

The baseline's corresponding comments are written to contrast with exactly that scheme:

```
// hgps_main/src/HealthGPS/population.cpp:56
// MAHIMA: Reused slot gets a fresh lifetime-unique ID (slot reuse != ID reuse).
```

```
// hgps_main/src/HealthGPS/population.h:102-105
// MAHIMA: Lifetime-unique person ID counter for post-initial entrants. …
// IDs are never reused even when slots are recycled.
```

Phrases like *"slot reuse != ID reuse"* and *"never reused **even when** slots are recycled"* are
corrective: they are written against a prior scheme, and the prior scheme is what the rewrite
implements. The baseline additionally added a debug-only duplicate-ID assertion
(`population.h:114-123`) that has no counterpart in the rewrite.

**Fix direction.** Adopt the monotonic counter. Keep the baseline's property that the *initial*
cohort gets IDs `1..N` aligned by index, since that is what makes baseline and intervention
comparable person-by-person; only post-initial entrants need the counter.

### R-04 — One floating-point reduction was left parallel

`DemographicModule::calculate_residual_mortality` (`src/hgps/data/demographic.cpp:335-353`)
accumulates `double` values into a shared gender/age table from inside a `tbb::parallel_for_each`,
guarded by a single `std::mutex`. The mutex prevents a data race; it does not fix the order of
accumulation, and floating-point addition is not associative.

This is precisely the pattern the rewrite removed from five other sites — both
`DefaultDiseaseModel` accumulations, both `DefaultCancerModel` accumulations, and
`RiskFactorAdjustableModel` — by making them serial (§`06` 3.2). The one other retained
`parallel_for_each`, at `src/hgps/simulation/simulation.cpp:187`, accumulates **integers**, which is
order-independent and therefore correct to leave parallel.

So the rewrite's authors appear to have applied a deliberate rule — *remove parallel floating-point
reductions, keep parallel integer ones* — and missed one case. The residual-mortality table feeds
death probabilities for every person every year, so it is not a peripheral quantity.

**Caveat on severity.** The equivalent baseline behaviour (N-7) was measured and produced no
observable difference at 125,000 people over 25 years, for the arithmetic reasons given in
`03-baseline-determinism.md` §2.3. The same reasoning applies here, so this is very unlikely to be
changing anyone's results today. It is rated high because the rewrite's central claim is
determinism, and this is the one place that claim rests on an accident of floating-point magnitude
rather than on the design.

**Fix direction.** Make it serial, consistent with its five siblings. If the parallelism is wanted,
use fixed-partition per-thread accumulators combined in index order, which is deterministic and
faster than a contended global mutex.

### R-05 — Derived predictor resolution is absent

`Person::get_risk_factor_value` (`src/hgps/data/person.cpp:56-72`) resolves a predictor name
against a stored income override, the static `current_dispatcher` table, and the per-person
`risk_factors` map — then throws `std::out_of_range`. The baseline has a fourth step
(`person.cpp:73-75`): `resolve_derived_predictor`, backed by `predictor_resolver.cpp` and
`linear_model_evaluator.cpp`, which handles predictor names that are computed rather than
enumerated — age polynomials parsed from a trailing digit, `log_`-prefixed names, `income_` names,
region and ethnicity dummies matched against the person's string attribute, and a configurable
`gender2` dummy. It also provides `is_metadata_predictor`, which causes the evaluator to skip
CSV/JSON rows that carry model metadata rather than regression terms.

The rewrite compensates partially and inconsistently: `gender2` was moved into the static
dispatcher, which the baseline's dispatcher does not contain, and log coefficients are handled
inline at `src/hgps/models/static_linear_model.cpp:723-735`. There is no equivalent of
`is_metadata_predictor` anywhere.

**Consequences.** A model JSON using a dynamically-named predictor the dispatcher does not enumerate
throws where the baseline resolved it; and a metadata row the baseline would skip is treated as a
regression term, silently contributing a coefficient times a lookup that either throws or — via the
swallowing catch at `:727`, see R-07 — is replaced by an expected value. The second failure mode is
worse than the first, because it produces numbers.

**Confidence is `likely`, not `confirmed`,** for the specific claim that this is a *divergence*
rather than a deliberate simplification. That these components are absent is certain. Which
direction the change ran is inferred, from three weak signals pointing the same way: the baseline is
version 3.0.0.0 against the rewrite's 1.0.0; `predictor_resolver` is threaded through the baseline's
most recently-worked code; and R-03 independently dates the divergence to before the baseline's
person-ID change. None of these is conclusive, and without history none can be.

**Fix direction.** Decide explicitly whether dynamically-named predictors are supported. If they
are, port the resolver. If they are not, validate every coefficient name in a model JSON against the
dispatcher **at load time** and reject unknown names with a located `InputIssue`, so the failure
happens once at startup with a clear message rather than deep in a per-person loop.
