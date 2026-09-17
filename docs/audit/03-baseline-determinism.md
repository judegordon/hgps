# 03 — Baseline determinism

This document enumerates every source of run-to-run or platform-to-platform variation found in
`hgps_main`, and reports a measured reproducibility experiment against the built binary.

A microsimulation used for policy comparison has a stronger determinism requirement than most
software: the whole method depends on the baseline and intervention futures differing *only* by the
policy. The baseline takes this seriously in places — `simulation.cpp:255` carries the comment
`// Needed for repeatability in random selection` — but the guarantee is assembled by convention at
each call site rather than enforced by the design.

---

## 1. Measured reproducibility

### 1.1 Method

Reference configuration derived from `hgps_main_examples/HLM_France/config.json`, the only example
with no active intervention, run against the local `hgps_main_data` snapshot. The only edits were
`data.source` (local directory instead of the pinned release URL) and the output folder and horizon.
Seed fixed at `123456789`, `trial_runs: 1`. Binary: the Release build described in
`00-inventory.md` §4.2. Comparison is `md5` of the main result CSV.

### 1.2 Results

| Experiment | Configuration | Result |
|---|---|---|
| **A. Repeat runs, baseline only** | 6,244 people, 2010–2015, 3 repeats | **Bit-identical.** `1fda2b34f1739611490330c596bdf477` ×3 |
| **B. Thread-count sweep, baseline only** | as A, `-T 1/2/4/10` | **Bit-identical** across all four. Same hash as A. |
| **C. Scale-up, baseline only** | 125,000 people, 2010–2035, 3 repeats | **Bit-identical.** `16648a4f5ad4ed2515306236188a5ea8` ×3 |
| **D. Repeat runs, baseline + intervention** | 31,000 people, 2010–2025, `simple` intervention, 3 repeats | **Files differ.** `812eacb7…`, `85c20c7a…`, `0af434c2…` |
| **E. Sorted comparison of D** | `sort file \| md5` | **Bit-identical.** `addc428d7cabf4b91c5a4edd4658f5c2` ×3. Row counts identical: 3,232 Baseline + 3,232 Intervention in every run. |
| **F. Thread-count sweep with intervention** | 2010–2030, `-T 1` vs `-T 10`, sorted comparison | **Bit-identical.** `70799d9231a32c15ee96a1e22494fc68` |

### 1.3 What this establishes

**The simulated numbers are reproducible. The output file is not.** Experiments D and E together
isolate the defect precisely: every row, and every value in every row, is identical run to run — but
the *order* in which Baseline and Intervention rows appear in the result file varies. Anyone
checksumming or `diff`-ing result files across runs — for regression testing, for provenance, for
CI — will see spurious differences. Anyone comparing the numbers will not.

Experiments B, C and F are the more interesting negative result: the floating-point accumulation
ordering described in §2.3 below **did not** produce an observable difference, even at 125,000
people over 25 years and across a 10× change in thread count. Section 2.3 explains why, and why it
should still be designed out.

---

## 2. Nondeterminism sources

### 2.1 RNG construction and seeding

| # | Source | Location | Assessment |
|---|---|---|---|
| N-1 | **Default-constructed RNG seeds from `std::random_device`** | `mtrandom.cpp:8-11` | `MTRandom32::MTRandom32() { std::random_device rd; engine_.seed(rd()); }`. Reached whenever `running.seed` is absent from the config: `program.cpp:205` only calls `seed()` `if (const auto seed = model_input->seed())`. A config without a seed therefore produces an irreproducible run **with no warning**. All six shipped examples set a seed, so this is latent rather than active. |
| N-2 | `RuntimeContext::random_` is default-constructed | `runtime_context.h:113` | `mutable Random random_{}` → `Random`'s implicit default constructor → `MTRandom32()` → N-1. It is always re-seeded in `setup_run` (`simulation.cpp:47`) before use, so N-1 does not bite here — but the object spends its construction-to-`setup_run` lifetime holding entropy from `std::random_device`. |
| N-3 | Seed is reported as `seed().value_or(0u)` in output metadata | `program.cpp:52` | When no seed was configured, the results file records seed `0`, which is **not** the seed that was used. The provenance record is silently wrong in exactly the case where it matters most. |

### 2.2 RNG stream sharing

| # | Source | Location | Assessment |
|---|---|---|---|
| N-4 | **A single RNG stream per simulation, consumed by all modules in sequence** | `runtime_context.h:113`, drawn at 30+ sites | Reproducibility depends on every module consuming draws in exactly the same order every run. This holds today only because every RNG call site sits in a *serial* loop — e.g. `default_disease_model.cpp:44` draws inside `for (auto &person : context.population())` while the parallel work in the same file (`:59`, `:116`) carefully draws nothing. Nothing in the type system enforces this. Adding one `context.random()` call inside any existing `tbb::parallel_for_each` would introduce both a data race (`Random` is not thread-safe) and nondeterminism, and would compile without complaint. |
| N-5 | `Random::next_double()` uses `std::generate_canonical` | `mtrandom.cpp:22` | Measured on this platform: consumes **2** `mt19937` draws per call, and over 2×10⁸ samples the maximum observed value was 0.99999999427 with **zero** values ≥ 1.0. `[rand.util.canonical]` does specify the algorithm, so conforming implementations should agree; historically libstdc++ could return exactly 1.0 (LWG 2524). Because `next_int_internal` (§3, B-05) would return `max_value + 1` on an input of exactly 1.0, this is worth eliminating by construction rather than relying on the library. Classified **suspected** for cross-platform divergence — not demonstrated here, as only libc++ was available. |

### 2.3 Threading and scheduling

| # | Source | Location | Assessment |
|---|---|---|---|
| N-6 | **Output row order depends on thread scheduling** | `runner.cpp:85-92`, `event_monitor.h:47-53`, `result_file_writer.cpp` | **Confirmed by experiment D/E above.** Baseline and intervention run on concurrent `std::jthread`s; both publish results asynchronously (`publish_async` → `tbb::concurrent_queue`); the writer drains in arrival order. This is the one nondeterminism that is actually observable in the shipped output. |
| N-7 | **Floating-point accumulation order under a shared mutex** | `default_disease_model.cpp:68-70`; `default_cancer_model.cpp:70-72`, `:134-136`; `demographic.cpp:536-538`; `analysis_module.cpp:147-148`; `risk_factor_adjustable_model.cpp:261` | The pattern is `tbb::parallel_for_each(... { lock; table(age,gender) += value; })`. The mutex prevents a data race but not order variation, and floating-point addition is not associative, so the accumulated sums may differ in their last bits between runs. In `default_disease_model.cpp` these sums become `average_relative_risk`, which divides into `probability`, which is compared against a random `hazard` at `:45`. **Not observed in experiments B/C/F.** The reason is arithmetic: a last-bit perturbation flips `hazard < probability` with probability ≈10⁻¹⁶ per comparison, and experiment C performed on the order of 10⁷ comparisons, giving an expected flip count around 10⁻⁹. So the mechanism is real and confirmed by reading, but its consequences are not reachable at realistic scale today. It is classified **likely** rather than confirmed, and its practical severity is low — but it is a hazard that a future sum-dependent branch, a much larger population, or a different reduction order would activate. |
| N-8 | `find_index_of_all` returns indices in nondeterministic order | `thread_util.h:43-56` | Results are `emplace_back`ed from parallel tasks under a mutex, so the vector's order varies. Both call sites compensate — `simulation.cpp:255` (`std::sort`, with the comment `// Needed for repeatability in random selection`) and `ses_noise_module.cpp:43` (`std::sort`). Correct today; it is a trap, because the function's contract does not mention the requirement and a third caller that forgets it would silently break reproducibility. |
| N-9 | `Runner` does not reset `source_` in the paired overload | `runner.cpp:75` vs `:30` | The single-scenario overload does `source_ = std::stop_source{}`; the paired one does not. A `Runner` reused after `cancel()` would find `stop_requested()` already true and abort every subsequent paired run immediately. Not reachable from the Console, which constructs one `Runner` per process. |
| N-10 | `SyncChannel::close()` notifies before setting the flag, without the mutex | `channel.h:84-87` | `cond_var_.notify_one(); is_closed_.store(true);` — a waiter that has evaluated its predicate but not yet slept can miss the notification and then block until `sync_timeout_ms` expires. `notify_one` also wakes a single waiter. Affects shutdown timing, not values. |

### 2.4 Unordered container iteration

| # | Source | Location | Assessment |
|---|---|---|---|
| N-11 | **A probability CDF is built by iterating an `std::unordered_map`** | `static_linear_model.cpp:1952-1991` | This is the most consequential ordering dependence in the codebase. `initialise_categorical_income` computes softmax probabilities over income categories and then assigns a category by walking a cumulative distribution: `for (const auto &[income, probability] : probabilities) { cumulative_prob += probability; if (rand < cumulative_prob) { person.income = income; ... } }` (`:1980-1983`). Both the normalising sum (`:1966-1969`) and the CDF walk iterate `std::unordered_map<core::Income, double>`, seeded from `income_models_`, itself an `std::unordered_map<core::Income, LinearModelParams>` (`static_linear_model.h:474`). Bucket iteration order is unspecified and differs between standard library implementations. **Within one build the result is reproducible** — which is why experiments A–F did not detect it — but the same seed on a different platform or standard library can assign a *different income category to the same person*, and income feeds risk factors, diseases and the income-stratified output files. Confidence: **confirmed** by code reading. Severity: **high** for cross-platform reproducibility. |
| N-12 | Other `unordered_map` uses | `static_linear_model.cpp:389-406`, `dynamic_hierarchical_linear_model.cpp:13-14`, `:92`, `fiscal_scenario.h:69` | These are used as lookup tables keyed by identifier, not iterated to produce ordered results. No ordering dependence found at these sites. `Person::risk_factors` and `Person::diseases` are `std::map` (ordered), so per-person iteration is stable. |

### 2.5 Floating-point behaviour

| # | Source | Location | Assessment |
|---|---|---|---|
| N-13 | Mixed `float`/`double` precision | `person.cpp:79-179` (`gender_to_value`, `income_to_value`, `region_to_value`, `ethnicity_to_value` all return `float`); `demographic.cpp:515` (`std::min(... , 1.0f)`) | Predictor values enter `double` linear models through `float`, so results depend on where each narrowing happens. Deterministic on a fixed build; a portability and accuracy concern rather than a run-to-run one. |
| N-14 | No explicit floating-point mode is set | `CMakeLists.txt` | The build does not pin `-ffp-contract`, `-fno-fast-math` or equivalent. Compilers may contract `a*b+c` into an FMA at their discretion, so results can differ between compilers, optimisation levels and target architectures for the same source and seed. Nothing in the project records an expected bit-level result to detect this. |

### 2.6 Time, locale and environment

| # | Source | Location | Assessment |
|---|---|---|---|
| N-15 | Output **file names** embed the wall-clock time | `configuration.cpp:235-240`, `result_file_writer.cpp:169` | `{TIMESTAMP}` expands to `%F_%H-%M-%S` UTC. Two runs never write the same filename. This does not affect content, but it does mean a regression harness cannot simply compare a fixed path, and it is why experiments A–F had to locate the output by glob. |
| N-16 | Result **content** embeds a timestamp | `result_file_writer.cpp:169` | The JSON metadata records the run time, so the `.json` output is never byte-identical between runs even when the model results are. Only the `.csv` outputs are comparable. |
| N-17 | `${VAR}` expansion in `output.folder` | `configuration.cpp:373-393` | Recursive `std::getenv` substitution. `HLM_France` uses `${HOME}`. Output location therefore depends on the environment; a missing variable expands to the empty string silently, which can place results in an unexpected relative path. |
| N-18 | Environment variable substitution in data paths | `datamanager.cpp:738` | Same mechanism inside the data manager. |
| N-19 | Locale-sensitive character handling | `string_util.cpp:22,30,60,77,96,112,125`; `identifier.cpp:45,50` | `std::tolower`, `std::toupper`, `std::isspace`, `std::isalpha`, `std::isdigit` are all locale-dependent and are called with plain `char`. The program never calls `setlocale`, so the `"C"` locale applies and behaviour is stable in practice. The `char` argument is a separate and more serious problem — see `04-baseline-issues.md` (B-03). |
| N-20 | Default thread count comes from the machine | `program.cpp:39-41`, `:103-106` | Absent `-T`, TBB sizes its pool from hardware concurrency. Given N-6, this means the output file's row ordering is machine-dependent as well as run-dependent. |

---

## 3. Summary

Two determinism problems are worth carrying into the new rewrite's design as requirements rather
than as bug fixes:

1. **Result serialisation must be order-stable** regardless of how many threads produced the rows
   (N-6, confirmed by experiment). Either results are written by a single owner in a defined order,
   or rows carry a total ordering key and are sorted before writing.
2. **No probability distribution may be built by iterating an unordered container** (N-11). This is
   currently the only place where the same seed can produce different model results on different
   platforms.

Two further properties hold today only by convention and should be enforced structurally:

3. **RNG draws must never occur inside a parallel region** (N-4). The current code obeys this
   perfectly; nothing prevents the next change from breaking it.
4. **Reductions over the population must have a fixed order** (N-7). The present mutex-guarded
   accumulation is order-variable by construction; a deterministic reduction (fixed-partition
   per-thread accumulators combined in index order) costs nothing and removes the class.

And one gap in evidence should be closed: there is **no reproducibility test anywhere in the 471-test
suite** — no test runs a simulation twice and compares. Experiments A–F had to be constructed from
outside. A new rewrite should ship that test.
