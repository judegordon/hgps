# 06 — Old rewrite: what changed and why

Describes each significant change `hpgs_og_rewrite` made relative to `hgps_main`. Per the
clean-room rule, the rewrite's code is described in prose with file and line citations; none of it
is reproduced here. Short excerpts from the BSD-3-Clause baseline appear only where a defect cannot
be shown otherwise.

---

## 1. A note on rationale evidence — read this first

The audit brief asks that each change's reason be marked **original** (evidenced in commit
messages, code comments or notes in the repo) or **inferred** (deduced now). Applying that rule
honestly gives an uncomfortable answer.

- **There is no git history.** `hpgs_og_rewrite` is not a git repository (`00-inventory.md` §1), so
  there are no commit messages to trace *when* or *why* anything changed.
- **There are no notes.** No README, no design document, no CHANGELOG, no TODO file.
- **There are almost no comments.** The rewrite's engine contains **19 comment lines across 13,059
  lines** — 0%, against the baseline's 3,363 lines across 19,446 — and of those 19, most are section
  dividers, filename headers, or `NOLINT` pragmas.

The only substantive explanatory comments in the entire rewrite are the seven `MAHIMA:` lines in
`src/hgps/data/population.cpp:6-65`, and those are **inherited from the baseline**, not written for
the rewrite. They are therefore evidence about the baseline's intent, not the rewrite's.

**Consequence: essentially every rationale below is marked `inferred`.** Where a change is so
tightly coupled to a specific, demonstrable baseline defect that the intent is not seriously in
doubt, that is said explicitly — but it is still labelled `inferred`, because deducing intent from
a fix is not the same as the author having stated it. Nothing below is presented as original
rationale that is not.

The one genuinely original piece of evidence points the *other* way, and is covered in §9: the
inherited `MAHIMA:` comments in `population.cpp` describe a **different** person-ID policy from the
baseline's current one, which dates the rewrite's divergence.

---

## 2. RNG design

### 2.1 The entropy-seeded default constructor was deleted

**Baseline.** `MTRandom32` has a default constructor that seeds from `std::random_device`
(`mtrandom.cpp:8-11`). `Random` holds an `MTRandom32` by value, and `RuntimeContext` default-
constructs a `Random` (`runtime_context.h:113`), so unseeded generators exist throughout the object
graph. A config without `running.seed` runs from entropy, silently (baseline issue B-06).

**Rewrite.** `src/hgps/utils/mt_random.h:10` declares the default constructor `= delete` and the
seeded constructor `explicit`. `src/hgps/utils/random_algorithm.h:11` gives `Random` a default
constructor that explicitly seeds the engine with `0`. An unseeded Mersenne Twister can no longer
be constructed anywhere in the program.

**Rationale: inferred.** The change makes irreproducibility a compile error rather than a runtime
surprise. Given that it is the only change to the class and that it removes the sole source of
entropy in the program, the determinism intent is not seriously in doubt.

**Behaviour:** *alters behaviour intentionally.* Configs that previously ran unseeded now run from a
fixed seed of 0 rather than from entropy. Note the rewrite does not otherwise fix B-06's second
half: it inherits `program.cpp`'s pattern of recording the seed as `value_or(0)`, which in the
rewrite is at least now truthful, since 0 is what an unseeded run actually uses.

### 2.2 Integer sampling was reimplemented

**Baseline.** `next_int_internal` computes `min + (int)((max - min + 1) * next_double())`
(`random_algorithm.cpp:76-78`). Two properties follow: the result is **inclusive** of `max` despite
the header documenting a half-open range (B-07), and if `next_double()` ever returned exactly `1.0`
the result would be `max + 1`, out of range at every call site — all four of which use it to index
a container.

**Rewrite.** `src/hgps/utils/random_algorithm.cpp:78-81` computes the range as an `unsigned int`
and takes `engine_.next() % range`. The out-of-range outcome becomes structurally impossible, and
one `mt19937` draw is consumed instead of the two that `generate_canonical` uses.

**Rationale: inferred.** Eliminating the out-of-range edge is the obvious motive.

**Behaviour:** *alters behaviour intentionally, with an unintended side effect.* Two consequences
follow that are worth being explicit about:

1. **The random streams diverge completely.** Changing from two draws per integer to one shifts
   every subsequent draw in the single shared stream. The rewrite and the baseline cannot produce
   comparable results from the same seed, even where the models are otherwise identical. This is
   not a defect, but it does mean the rewrite can never be validated by diffing its output against
   the baseline's — a significant loss for a reimplementation.
2. **It introduces modulo bias.** `engine_.next() % range` is non-uniform unless `range` divides
   2³². The bias magnitude is about `range / 2³²`, so for the ranges actually used — population
   indices and matrix rows, at most a few hundred thousand — it is around 10⁻⁵ to 10⁻⁴ relative.
   Small, but a statistical defect where the baseline had none, and avoidable with rejection
   sampling. Recorded as R-06 in `08-rewrite-issues.md`.

### 2.3 Two robustness bugs in the distributions were fixed

**Normal sampling.** The baseline's Marsaglia polar loop rejects `p >= 1.0` but not `p == 0.0`
(`random_algorithm.cpp:86-90`), so a double-zero draw yields `log(0)` → `-inf` → `NaN` in a risk
factor (baseline issue B-15). The rewrite's loop at `src/hgps/utils/random_algorithm.cpp:96` rejects
both conditions.

**Empirical discrete sampling.** The baseline validates only that the value and CDF vectors are the
same size, then falls through to `values.back()` — undefined behaviour if both are empty
(B-14). The rewrite adds an explicit empty check to both overloads
(`src/hgps/utils/random_algorithm.cpp:38-40` and `:59-61`) before any access.

**Rationale: inferred.** Both are unambiguous defensive fixes for defects that exist in the
baseline. **Behaviour:** *preserves behaviour* on all valid inputs; converts undefined behaviour
into a diagnosable exception on invalid ones.

### 2.5 Categorical income sampling was made order-deterministic and numerically stable

**Baseline.** `initialise_categorical_income` (`static_linear_model.cpp:1952-1991`) computes softmax
probabilities and then walks a cumulative distribution, iterating `std::unordered_map` at every
stage. Category assignment therefore depends on unspecified hash-bucket order, so the same seed can
assign a different income category on a different standard library (baseline issue B-05). It also
computes `exp(logit)` directly, and can fall through the CDF loop to a thrown exception when
floating-point rounding leaves the accumulated probability just below the draw.

**Rewrite.** `src/hgps/models/static_linear_model.cpp:824-863` fixes all three:

1. A helper at `:40-61` returns the income categories as a `std::vector` built from a hard-coded
   braced-init-list — `{low, lowermiddle, uppermiddle, high}` for four categories, `{low, middle,
   high}` otherwise — filtered by membership in the model map. Every subsequent stage (`logits`,
   `probs`, the CDF walk) iterates a `std::vector`, never the `unordered_map`. Ordering is now part
   of the model definition and is identical on every platform.
2. `:846` uses `std::exp(logit - max_logit)` — the numerically stable softmax — where the baseline
   exponentiates the raw logit and can overflow to infinity for large coefficients.
3. `:855` terminates the CDF walk with `draw < cumulative || i + 1 == probs.size()`, guaranteeing a
   category is always assigned rather than falling through to the throw.

**Rationale: inferred.** **Behaviour:** *alters behaviour intentionally.* This is the rewrite's
cleanest single fix, and the pattern — derive an explicit order from the domain, then sample over a
sequence — is the one to carry into the new rewrite wherever a distribution is sampled.

### 2.4 What was not changed

`next_double()` is unchanged — still `std::generate_canonical<double, 53>`
(`src/hgps/utils/mt_random.cpp:13-15`). The single-shared-`Random`-per-context topology
(`runtime_context`) is unchanged: there are still no per-module or per-person substreams, and
reproducibility still depends on every module consuming draws in a fixed order. The hand-rolled
distributions are retained, so the baseline's good instinct about `std::normal_distribution`
portability survives.

---

## 3. Determinism strategy and parallelism

This is the largest and most consequential change, and the two topics are one decision.

### 3.1 Scenarios now run sequentially

**Baseline.** `Runner` has two near-duplicate overloads. The paired one spawns a `std::jthread` per
scenario and joins both (`runner.cpp:85-92`). Baseline and intervention execute concurrently,
rendezvousing each simulated year through `SyncChannel`.

**Rewrite.** Both overloads delegate to a single internal `run_simulation_series`
(`src/hgps/simulation/runner.cpp:16`). There are no threads: `run_one(baseline, …)` is called, and
then `run_one(*intervention, …)` (`:52`, `:60`), both on the calling thread. The `std::stop_source`
cancellation mechanism is retained; `std::jthread` is gone entirely.

**Rationale: inferred.** It directly and completely removes baseline issue B-01 — the only
nondeterminism actually observable in the baseline's shipped output.

**Behaviour:** *alters behaviour intentionally.* Confirmed by measurement (`00-inventory.md` §4.6):
three same-seed runs of the rewrite's own FINCH example with an intervention active produced
**byte-identical** output files (`863625e9d2b917c177de2907ba64ff57` ×3), where the equivalent
baseline experiment produced three different files. Two costs come with it:

- **Wall-clock.** Both scenarios now run end to end rather than overlapping, so an intervention
  experiment takes roughly twice as long in scenario time.
- **Channel semantics.** The baseline's `SyncChannel` is a year-by-year rendezvous with a
  `sync_timeout_ms` deadline. Running sequentially means the baseline arm must buffer *every* year's
  net-migration message before the intervention arm starts draining them. This works because the
  channel is unbounded, but `sync_timeout_ms` is now dead configuration, and the channel has silently
  become a queue rather than a synchroniser. Nothing in the rewrite documents this.

### 3.2 Most data parallelism was removed

**Baseline.** Seven `tbb::parallel_for_each` sites plus twelve `core::parallel_for` calls in
`analysis_module.cpp` (`01-baseline-architecture.md` §6).

**Rewrite.** Two `tbb::parallel_for_each` sites remain, and **zero** `core::parallel_for` calls:

| Baseline parallel site | Rewrite |
|---|---|
| `default_disease_model.cpp:59`, `:116` (float accumulation under mutex) | serial |
| `default_cancer_model.cpp:61`, `:126` (float accumulation under mutex) | serial |
| `risk_factor_adjustable_model.cpp:261` | serial |
| `disease.cpp:72` (`build_disease_module`) | serial (`src/hgps/disease/disease.cpp:69`) |
| `analysis_module.cpp` × 12 | serial |
| `simulation.cpp:234` (integer counting under mutex) | retained (`src/hgps/simulation/simulation.cpp:187`) |
| `demographic.cpp:530` (float accumulation under mutex) | **retained unchanged** (`src/hgps/data/demographic.cpp:344-353`) |
| `population.cpp:37` (`std::execution::par`) | retained (`src/hgps/data/population.cpp:22`) |

**Rationale: inferred.** Removing the mutex-guarded floating-point accumulations removes baseline
nondeterminism source N-7 at those sites. Removing the `build_disease_module` parallelism removes
the concurrency that triggers the B-02 data race.

**Behaviour:** *alters behaviour intentionally,* with one gap that looks accidental. The two
retained sites are instructive:

- `simulation.cpp:187` accumulates **integers** under a mutex. Integer addition is associative, so
  this is order-independent and deterministic. Keeping it is correct.
- `demographic.cpp:344-353` accumulates **doubles** under a mutex — exactly the pattern removed
  everywhere else. If the removals were a deliberate determinism campaign, this one was missed.
  Recorded as R-07 in `08-rewrite-issues.md`.

**Measured cost.** The rewrite scales poorly, as expected: its FINCH example took 61 s at one
thread and 45 s at ten — a 1.36× speed-up on a 10-core machine. The baseline scales no better
(37 s vs 39 s on its France example), because its coarse global mutex serialises each parallel loop
body anyway. **The throughput actually surrendered by removing the parallelism is therefore small**,
which makes the trade a good one. That is a useful and non-obvious result for the new rewrite: the
baseline's parallelism was mostly not buying anything.

### 3.3 Two determinism traps were closed in `thread_util.h`

**Baseline.** `find_index_of_all` returns indices in nondeterministic order; correctness depends on
every caller remembering to `std::sort` the result, which both current callers do
(N-8). `parallel_for(first, last)` is called as `parallel_for(0, container.size() - 1)`, which on an
empty container underflows to `SIZE_MAX` and is saved only by a second unsigned wraparound in
`last - first + 1`.

**Rewrite.** `src/hgps_core/utils/thread_util.h:46` sorts inside `find_index_of_all` before
returning, making the contract deterministic regardless of caller discipline; `:34` returns early on
an empty container; `:21` guards `last < first` explicitly instead of relying on wraparound.

**Rationale: inferred.** **Behaviour:** *preserves behaviour* — both current call sites already
sorted (the rewrite's `apply_net_migration` at `src/hgps/simulation/simulation.cpp:208` still sorts
redundantly) — while removing the trap for future callers.

The `parallel_for` implementation itself is unchanged, including the baseline's habit of
materialising a full `std::vector<size_t>` of the index range on every call just to iterate it.
That inefficiency is inherited (R-08).

---

## 4. Concurrency correctness

**Baseline.** `CachedRepository::get_disease_definition` reads the `diseases_` map outside the mutex
on a fast path commented `// lock-free multiple readers` (`repository.cpp:56-58`), racing concurrent
locked insertion — confirmed by ThreadSanitizer (B-02).

**Rewrite.** Fixed twice over, independently:

1. `src/hgps/data/repository.cpp:62` takes `std::scoped_lock` **before** the `contains` check. The
   fast path is gone.
2. `src/hgps/disease/disease.cpp:69` builds the disease module with a serial `for` loop, so the
   concurrent callers no longer exist.

**Rationale: inferred.** **Behaviour:** *preserves behaviour,* removing undefined behaviour.

The rewrite also completes a related piece of tidying the baseline left unfinished: the baseline's
`clear_cache` (`repository.cpp:82-89`) clears five members but not `lms_parameters_`; the rewrite's
(`src/hgps/data/repository.cpp:93`) resets it too. This is invisible in practice — `clear_cache` is
never called anywhere in either codebase — but it is a fair signal of the care taken.

---

## 5. Runner lifecycle

**Baseline.** The single-scenario overload resets `source_ = std::stop_source{}` (`runner.cpp:30`);
the paired overload does not (`:75`). A `Runner` reused after `cancel()` would abort every
subsequent paired run immediately (N-9). Unreachable from the Console, which builds one `Runner` per
process.

**Rewrite.** Unifying both overloads into `run_simulation_series` puts the reset on one path
(`src/hgps/simulation/runner.cpp:20`), so both entry points get it.

**Rationale: inferred** — and plausibly incidental to the unification rather than a targeted fix.
**Behaviour:** *preserves behaviour* for the reachable cases; fixes the latent one.

---

## 6. `Identifier` and character handling

**Baseline.** Two defects, both high severity:

```cpp
// identifier.cpp:32-34
bool Identifier::operator==(const Identifier &rhs) const noexcept {
    return hash_code_ == rhs.hash_code_;
}
```

with `std::strong_ordering operator<=>(...) = default` (`identifier.h:70`) comparing the string —
so equality and ordering disagree (B-04); and eleven `<cctype>` calls taking a plain, signed `char`,
which is undefined behaviour for any byte ≥ 0x80 (B-03).

**Rewrite.** Both fixed:

- `src/hgps_core/types/identifier.cpp:33-35` compares `value_`; `:41-43` does the same for
  `equal(const Identifier&)`. The defaulted `<=>` is retained, and is now consistent with `==`,
  because both compare the string first. `hash_code_` survives purely as the
  `std::hash<Identifier>` implementation for unordered containers — which is the correct role for
  it.
- Every `<cctype>` call in `src/hgps_core/types/identifier.cpp:46,50` and
  `src/hgps_core/utils/string_util.cpp` (10 sites) casts to `unsigned char` first.

**Rationale: inferred.** Both are textbook corrections with no plausible alternative motive.
**Behaviour:** *preserves behaviour* on ASCII input, which is all the shipped data exercises;
removes undefined behaviour on non-ASCII input and removes a latent hash-collision correctness hole.

---

## 7. Error handling and configuration diagnostics

This is the rewrite's most original design work — the one place it does something the baseline does
not do at all, rather than doing the same thing differently.

**Baseline.** A single exception type, `core::HgpsException` (`exception.h:19`), carrying a message
and a `std::source_location`. Config and data problems are reported by throwing. The first problem
aborts the load, so a user with five malformed fields discovers them one run at a time. Some
problems are not reported as errors at all: `Missing key "policy_start_year"` is printed as a bare
line with no file, no field path and no severity (`02-data-and-examples.md`, D-08).

**Rewrite.** The single exception type is split into two distinct concepts
(`src/hgps_core/diagnostics/`):

- **`InternalError`** (`internal_error.h:10`) — a `std::runtime_error` with `std::source_location`,
  for programmer errors. This is the baseline's `HgpsException`, renamed to say what it is for.
- **`InputIssue`** and **`InputIssueReport`** (`input_issue.h:40`, `:58`) — a *value type*, not an
  exception, for user input problems. Each issue carries an `IssueLevel` (`warning` or `error`), an
  `IssueCode` from a closed enumeration of ten (`missing_key`, `wrong_type`, `invalid_value`,
  `invalid_enum_value`, `missing_file`, `invalid_path`, `parse_failure`, `missing_column`,
  `duplicate_entry`, `unknown`), an `IssueLocation` (source path, dotted field path, line, column,
  each individually optional and queryable), and a message.

Parsers thread an `InputIssueReport` through and **accumulate** rather than throw: the config
parser references it at 28 points and the shared model-parser helpers at 17. `io/json_access.h`
provides typed accessors (`get_to`) that record a structured issue on failure instead of raising.

**Rationale: inferred.** The design is too deliberate to be incidental — a closed error-code
enumeration, a separate warning level, and optional-field-aware locations are choices, not
accidents — but nothing in the repository states the goal.

**Behaviour:** *alters behaviour intentionally.* A user gets every input problem at once, each
located by file and field path, with warnings distinguished from errors. This is a clear
improvement over the baseline and is the single strongest candidate to carry into the new rewrite
(`09-ideas-and-questions.md` §1).

---

## 8. Predictor resolution — a change whose intent is genuinely unclear

**Baseline.** Predictor names in model JSON are resolved through two components that the rewrite
has no counterpart for: `predictor_resolver.{cpp,h}` and `linear_model_evaluator.{cpp,h}`. Between
them they handle dynamically-named predictors — `age`/`age2`/`age3` polynomials parsed from a
trailing digit, `log_*` prefixed names, `income_*` names, region and ethnicity dummy names matched
against the person's string attribute, a `gender2` regression dummy configurable to either sex, and
`is_metadata_predictor`, which skips CSV/JSON rows that are model metadata rather than regression
terms. `Person::get_risk_factor_value` (`person.cpp:73-75`) uses `resolve_derived_predictor` as its
final fallback before throwing.

**Rewrite.** Both components are absent. The concern is partly redistributed:

- `gender2` moved into the static `Person::current_dispatcher` table
  (`src/hgps/data/person.cpp`), which the baseline's dispatcher does not contain — the baseline
  handles it dynamically instead.
- Log coefficients are handled inline in `src/hgps/models/static_linear_model.cpp:723-735`, with a
  `try`/`catch (const std::exception &)` that silently substitutes an expected value when a
  predictor cannot be resolved.
- `src/hgps/data/person.cpp:56-72` has **no** derived-predictor fallback: a name not in the
  dispatcher and not in `risk_factors` throws `std::out_of_range`.
- There is no equivalent of `is_metadata_predictor` anywhere in the rewrite.

**Rationale: inferred, and with low confidence.** Two readings fit the evidence equally well, and
without history there is no way to choose between them:

1. The rewrite **simplified deliberately**, replacing dynamic name parsing with an explicit
   dispatcher because string-parsed predictor names are fragile.
2. The baseline **added these components after the rewrite diverged**, and the rewrite simply
   predates them.

Reading (2) is somewhat better supported: the baseline's version is 3.0.0.0 against the rewrite's
1.0.0, `predictor_resolver` is threaded through baseline code that carries `MAHIMA:` comments
(i.e. the baseline's most recent work), and §9 below independently dates the divergence.

**Behaviour:** *alters behaviour, possibly by accident.* Either way the consequence is the same and
is a real limitation: a model JSON whose coefficients use dynamically-named predictors that the
rewrite's dispatcher does not enumerate will throw where the baseline resolved them, and metadata
rows that the baseline skips will be treated as regression terms. Recorded as R-05 in
`08-rewrite-issues.md`.

---

## 9. Person identity — the one piece of original evidence

**Baseline.** `Population` assigns **lifetime-unique** IDs from a monotonic counter
(`population.h:106`, `:114`). Slots are recycled; IDs are not. The baseline's own comments are
explicit that this is a deliberate evolution:

```
// population.h:102-105
// MAHIMA: Lifetime-unique person ID counter for post-initial entrants.
// Initial cohort keeps deterministic IDs [1..initial_size_] for baseline/intervention
// alignment. All later entrants (births/immigration) get IDs from this monotonic counter and
// IDs are never reused even when slots are recycled.
```

```
// population.cpp:56
// MAHIMA: Reused slot gets a fresh lifetime-unique ID (slot reuse != ID reuse).
```

**Rewrite.** `src/hgps/data/population.cpp:42`, `:59`, `:66` assign `slot_index + 1` — a
**slot-based** ID that is reused when a slot is recycled. Its inherited comments state the older
policy directly: *"Newborn in recycled slot keeps that slot's ID (index + 1)"* (`:58`).

**Rationale: original — uniquely so.** These comments are the only place in the rewrite where an
inherited note describes the rewrite's own behaviour, and they establish the direction of travel:
the baseline's phrasing (`slot reuse != ID reuse`, "IDs are never reused **even when** slots are
recycled") is written to contrast with an earlier scheme, and that earlier scheme is exactly what
the rewrite implements. **The rewrite predates the baseline's lifetime-unique-ID change.**

**Behaviour:** *alters behaviour, by accident* — this is a divergence the rewrite inherited rather
than chose. It matters because the rewrite also retains `IndividualIDTrackingWriter`: when a person
dies and a newborn takes their slot, the tracking CSV reports both under the same ID, silently
conflating two people in a per-person output. Recorded as R-03 in `08-rewrite-issues.md`.

---

## 10. Structural changes with no behavioural intent

Recorded for completeness; all **inferred**, all *preserve behaviour*.

- **Directory topology.** Flat per-target directories became topic sub-directories
  (`hgps/models/`, `hgps_input/config/`, `hgps_core/diagnostics/`, …). File names normalised to
  snake_case.
- **Monolith splitting.** `model_parser.cpp` (2,252 lines) → 11 units; `analysis_module.cpp`
  (2,202) → 5; `datamanager.cpp` (806) → 7; `configuration.cpp` (406) → 5. See
  `05-rewrite-mapping.md` §3–4.
- **Umbrella headers removed.** The three `api.h` files and both `poco.h` files are gone;
  translation units include what they use.
- **`concepts.h` added** (`src/hgps_core/utils/concepts.h`) — C++20 concepts constraining templates
  that the baseline left unconstrained.
- **`finally.h` removed**; the one use in `Runner` became a local RAII struct
  (`src/hgps/simulation/runner.cpp:94-97`).
- **`dummy_model` removed** — consistent with removing the test suite.
- **Build tidying.** `PreventInSourceBuilds.cmake` → `OutOfSource.cmake`; Doxygen generation and
  codecov configuration dropped; `gtest` removed from `vcpkg.json`. The vcpkg builtin-baseline is
  **unchanged** from the baseline's (`bd2b5483…`), which is itself weak evidence that the rewrite
  branched from this lineage rather than being independently bootstrapped.

---

## 11. Baseline issues: fixed, inherited, or not applicable

| Baseline issue | Status in the rewrite | Where |
|---|---|---|
| **B-01** output row order nondeterministic | **Fixed** — sequential runner; verified byte-identical across 3 same-seed runs | §3.1 |
| **B-02** repository data race | **Fixed twice** — lock taken before the read, and the concurrent caller made serial | §4 |
| **B-03** `<cctype>` with signed `char` (UB) | **Fixed** — all 12 sites cast to `unsigned char` | §6 |
| **B-04** `Identifier::operator==` compares hash only | **Fixed** — compares the string | §6 |
| **B-05** income CDF over `unordered_map` iteration | **Fixed** — the CDF is built over an explicitly ordered vector | §2.5 |
| **B-06** unseeded run silent, seed mis-recorded | **Half fixed** — entropy seeding made impossible; the `value_or(0)` reporting pattern remains, though now truthful | §2.1 |
| **B-07** `next_int` inclusive vs documented half-open | **Moot** — reimplemented; still inclusive, still undocumented (no doc comments at all) | §2.2 |
| **B-08** `output.file_name` silently ignored | **Inherited** | R-10 |
| **B-09** `-s/--storage` unusable with a valid config | **Fixed incidentally** — the `data` schema's `oneOf` now permits a local `index`, so a config need not name a packaged source | §12 |
| **B-10** does not build on macOS | **Inherited** — same `#error`, same `std::execution::par`; `std::osyncstream` is gone, so one of three blockers is removed | R-11 |
| **B-11** 35 tests silently skip | **Not applicable** — there is no test suite at all, which is strictly worse | R-02 |
| **B-12** `Population::add` is `noexcept` but allocates | **Inherited** verbatim | R-08 |
| **B-13** `Population::add` rescans from index 0 | **Inherited** verbatim | R-08 |
| **B-14** `next_empirical_discrete` UB on empty input | **Fixed** | §2.3 |
| **B-15** polar method `p == 0.0` → `NaN` | **Fixed** | §2.3 |
| **B-16** `CUTIEPIE` debug output | **Fixed** — absent | §10 |
| **B-17** personal working material | **Mostly fixed** — 157 `MAHIMA:` comments reduced to 7; dead debug block and the personal directory removed | §10 |
| **B-18** version mismatch CMake vs vcpkg | **Fixed** — both say `1.0.0` | `00` §2.4 |
| **B-19** unused-but-set variables | **Fixed** — absent; 5 new `-Wpessimizing-move` warnings appear instead | R-12 |

Twelve of nineteen fixed, five inherited, one half-fixed, one made worse.

---

## 12. Configuration and I/O handling

**Data source.** The baseline's `data` block requires `source` and treats `checksum` as optional.
The rewrite's `schemas/config/data.json` uses a `oneOf` to accept **either** `{source, checksum}` —
with checksum now **required**, closing an integrity gap — **or** a new `{index}` naming a local
data index file directly. The rewrite's own example uses the latter
(`models/kevinhall_finch/config.json`). Zip download, SHA-256 verification and cache extraction are
all retained in `src/hgps_input/io/`.

**Rationale: inferred.** **Behaviour:** *alters behaviour intentionally* — and usefully, since it
removes the baseline's awkward situation where the only supported way to point at an unpackaged
local data tree was the `-s` flag that the schema makes unusable (B-09).

**Config schema.** Four incompatible changes, covered in detail in `07-rewrite-data-examples.md` §3:
the `$schema` URL contract moved from `/schemas/v1/config.json` to `/schemas/config/config.json`;
the `version` member was removed; `project_requirements` became **required**; `running.seed` became
a scalar rather than an array. The net effect is that **no upstream example config can be run by the
rewrite unmodified**, which is why the reference-run comparison in `00-inventory.md` §4.6 had to use
the rewrite's own FINCH pack rather than a shared configuration.

**Output.** `ResultFileWriter` and `IndividualIDTrackingWriter` are retained with the same file set.
`income_category_layout` — which the baseline uses to drive the ordering and membership of the
income-stratified CSVs — has no counterpart, so that ordering is now determined elsewhere.
