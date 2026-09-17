# 04 — Baseline issues

Clear, stand-out problems in `hgps_main` (Health-GPS 3.0.0.0). This is deliberately not an
exhaustive defect list: cosmetic and speculative items were dropped. Every entry below was
**confirmed by reading the code**, and most are additionally demonstrated by a test run, a
sanitizer, or the built binary.

**Confidence** — `confirmed`: demonstrated by test, sanitizer or unambiguous code reading.
`likely`: strong code evidence, not demonstrated. `suspected`: worth checking.
**Severity** — `critical`: wrong model outputs or crashes. `high`: nondeterminism, undefined
behaviour, data-handling errors. `medium`: robustness, performance. `low`: style, maintainability.

---

## Findings

| ID | Sev | Conf | Location | Description | Evidence | Fix direction |
|---|---|---|---|---|---|---|
| **B-01** | high | confirmed | `runner.cpp:85-92`; `event_monitor.h:47-53` | Result-file **row order** varies run to run when an intervention is active. Values do not. | 3 same-seed runs produced 3 different file hashes; `sort`ing made them byte-identical (`03` §1.2 D/E) | Serialise results through a single owner in a defined order, or sort by (source, run, time, gender, index) before writing |
| **B-02** | high | confirmed | `repository.cpp:56-58` vs `:63/:134` | **Data race** on `std::map diseases_`: a deliberately lock-free read path races concurrent locked insertion. Undefined behaviour. | ThreadSanitizer: locked write at `repository.cpp:134` (T10) vs unlocked read at `repository.cpp:57` (T4), same address, both via `disease.cpp:79` | Take the lock before the `contains` check; or load all disease definitions once, before any concurrency starts |
| **B-03** | high | confirmed | `identifier.cpp:45,50`; `string_util.cpp:7,12,22,30,60,77,96,112,125` | `std::isdigit`/`isalpha`/`isspace`/`tolower`/`toupper` called with plain `char`. For any byte ≥ 0x80 on a signed-`char` platform this is **undefined behaviour**. | Code reading; `char` is signed on x86-64/arm64 Linux and macOS. Reachable from any non-ASCII byte in a CSV header, country name or identifier | Cast to `unsigned char` at every call site |
| **B-04** | high | confirmed | `identifier.cpp:32-34` + `identifier.h:70` | `Identifier::operator==` compares **only the 64-bit hash**, while the defaulted `operator<=>` compares the **string**. Equality and ordering are mutually inconsistent. | Code reading, unambiguous | Compare `value_`; keep the hash for `std::unordered_map` bucketing only |
| **B-05** | high | confirmed | `static_linear_model.cpp:1952-1991` | A **probability CDF is built by iterating an `std::unordered_map`**, so income-category assignment depends on unspecified bucket order. Same seed, different standard library → different person income. | Code reading; `income_models_` is `std::unordered_map` (`static_linear_model.h:474`) | Iterate an ordered, explicitly-sequenced container of categories |
| **B-06** | high | confirmed | `mtrandom.cpp:8-11`; `program.cpp:205-208`; `program.cpp:54` | A config with no `running.seed` runs from `std::random_device` **silently**, and the results file then records the seed as `0` — a provenance record that is actively wrong. | Code reading; `seed().value_or(0u)` at `program.cpp:54` | Require an explicit seed, or generate one, report it, and record the value actually used |
| **B-07** | medium | confirmed | `random_algorithm.h:20-27` vs `random_algorithm.cpp:76-78` | `next_int` is documented as returning `[min, max_value)` but returns `[min, max_value]`. All four call sites compensate by passing `size()-1`. | Code reading; header text vs implementation | Make the contract half-open and fix call sites, or rename to `next_int_inclusive` |
| **B-08** | medium | confirmed | `configuration.cpp:231-261` | `output.file_name` is **silently ignored** unless it contains a `{…}` token, because `tk_end` stays `0` and `:259` gates on `tk_end > 0`. | Ran with `"file_name": "result.json"`; got `HealthGPS_result_<timestamp>.json` | Use the configured name; treat an unknown token as the error it already is |
| **B-09** | medium | confirmed | `program.cpp:130-135` vs `schemas/v1/config.json` | The `-s/--storage` option is **unusable**: the program errors unless exactly one of `-s` / `config.data` is present, but the schema makes `data` **required**. | Ran `-s <dir>` with a valid config → `Invalid configuration - : Required property 'data' not found.` | Make `data` optional in the schema, or remove `-s` |
| **B-10** | medium | confirmed | `program_dirs.cpp:37-38`; `population.cpp:37`; `static_linear_model.cpp:236`… | **Does not build on macOS**: `#error "Unsupported platform"`, plus `std::execution::par` and `std::osyncstream`, neither available in Apple libc++. | Build log; four shims were required (`00-inventory.md` §4.2) | Add a `__APPLE__` branch (`_NSGetExecutablePath`); avoid `<syncstream>` and PSTL, or feature-detect them |
| **B-11** | medium | confirmed | `KevinHallHeight.Test.cpp:21-26` and 4 sibling files | **35 tests (7.4%) silently skip** on an undocumented, `__FILE__`-relative path `input-data/data/KevinHall_FINCH` that matches neither upstream data repository's layout. A green CI run proves less than it appears to. | `[ PASSED ] 436 tests. [ SKIPPED ] 35 tests` | Make the fixture path explicit and configurable; fail rather than skip when it is expected to exist |
| **B-12** | medium | likely | `population.cpp:51-63`, `:85-102` | `Population::add` is `noexcept` but calls `emplace_back`, `at()` and a vector-returning helper. A `std::bad_alloc` or `std::out_of_range` becomes `std::terminate`. | Code reading | Drop `noexcept`, or make the body genuinely non-throwing |
| **B-13** | medium | likely | `population.cpp:52`, `:85-102` | `Population::add` calls `find_index_of_recyclables`, which **rescans the population from index 0 on every call**. Immigration calls it once per migrant per age per gender per year → quadratic in population size. | Code reading; the scan cannot start later than 0 because recycled slots become active | Maintain a free-slot list, amortising to O(1) |
| **B-14** | medium | likely | `random_algorithm.cpp:43-74` | `next_empirical_discrete` validates only that the two vectors are the same size. If both are empty, `values.back()` is **undefined behaviour**. | Code reading; called from `default_cancer_model.cpp:328` with data-driven vectors | Reject empty inputs |
| **B-15** | medium | likely | `random_algorithm.cpp:84-93` | Marsaglia polar rejection loop excludes `p >= 1.0` but **not `p == 0.0`**, giving `log(0)` → `-inf` → `NaN` propagated into a risk factor. Probability is minute but the failure is silent. | Code reading | Add `p == 0.0` to the rejection condition |
| **B-16** | low | confirmed | `model_parser.cpp:2249` | Production console output reads `FINISHED ALL THE LOADING REQUIRED CUTIEPIE :)`. | Printed by every run | Remove |
| **B-17** | low | confirmed | 157 sites across `src/`; `simulation.cpp:61-79`; `disease_check_scripts- Mahima/` | Personal working material in the main tree: 157 `MAHIMA:` comments, a dead debug block that builds a string and discards it on every run, and a top-level directory with a space and a personal name. | Code reading; build | Strip before release |
| **B-18** | low | confirmed | `CMakeLists.txt:25` vs `vcpkg.json` | Project version is `3.0.0.0`; the vcpkg manifest says `1.2.2.0`. The version reported in every results file comes from the former. | Code reading | Single source of truth |
| **B-19** | low | confirmed | `analysis_module.cpp:821, 1221, 1847` | The only compiler warnings at the project's own `-Wall -Wextra -Wpedantic` level: three `std::atomic_size_t` progress counters incremented but never read. | Build log | Remove, or use them |

**Tool output.** cppcheck 2.21 produced 15 diagnostics over `src/` (6 `uninitMemberVar`,
3 `uninitMemberVarNoCtor`, 2 `duplInheritedMember`, 2 `returnByReference`, 1 `passedByValue`,
1 `throwInEntryPoint`). clang-tidy produced 987 warnings, dominated by
`misc-const-correctness` (485) and `bugprone-narrowing-conversions` (221). ASan+UBSan were
**clean** on both the 471-test suite and a full France simulation. Treated as leads: only items
confirmed by reading appear above. The cppcheck `duplInheritedMember` lead on `mtrandom.h:27,30`
is real but benign — `MTRandom32::min/max` hide the base's non-virtual statics with numerically
identical values.

---

## Detail — critical and high findings

### B-01 — Result row order is nondeterministic

**What happens.** `Runner::run` starts baseline and intervention on two concurrent `std::jthread`s
sharing one `run_seed` (`runner.cpp:83-92`). Each publishes results with `publish_async`, which
enqueues onto `tbb::concurrent_queue`s drained by `tbb::task_group` workers in the Console's
`EventMonitor`. The writer appends rows in arrival order, so the interleaving of `Baseline` and
`Intervention` rows follows thread scheduling.

**Evidence.** Three runs, seed `123456789`, `simple` intervention, otherwise identical:

```
out_I1  md5 812eacb736066332e58b24bf494f45c9
out_I2  md5 85c20c7a51d3b6e7f76b9eaefaf8b0c6
out_I3  md5 0af434c2800eeeb3eb4cc6e0e708d2bb

sort <file> | md5   →  addc428d7cabf4b91c5a4edd4658f5c2   (all three)
row counts          →  3232 Baseline + 3232 Intervention  (all three)
```

**Why it matters, and what it is not.** The simulated numbers are fully reproducible — this is a
serialisation defect, not a modelling one. But it means a result file cannot be checksummed,
`diff`ed or content-addressed across runs, which rules out the cheapest and most convincing form of
regression test for a model whose entire purpose is reproducible comparison.

**Fix direction.** Give rows a total order and sort before writing, or have one writer own the file
and emit scenarios in a fixed sequence. Making the runner sequential also removes it — that is what
the old rewrite did (`06-rewrite-changes.md` §3).

### B-02 — Data race on the shared disease-definition cache

**What happens.** `CachedRepository::get_disease_definition` takes a deliberate lock-free fast path:

```cpp
// repository.cpp:53-64
DiseaseDefinition &CachedRepository::get_disease_definition(const core::DiseaseInfo &info,
                                                            const ModelInput &config) {

    // lock-free multiple readers
    if (diseases_.contains(info.code)) {
        return diseases_.at(info.code);
    }

    std::scoped_lock<std::mutex> lock(mutex_);
    try {
        load_disease_definition(info, config);
        return diseases_.at(info.code);
```

`diseases_` is a `std::map<Identifier, DiseaseDefinition>` (`repository.h:144`).
`load_disease_definition` inserts into it at `repository.cpp:134`. The unsynchronised `contains`
at `:57` therefore reads the red-black tree while another thread is relinking it. Concurrency is
real and immediate: `build_disease_module` (`disease.cpp:72`) calls this from inside a
`tbb::parallel_for_each` over the disease list, and both scenario threads share one repository
instance created at `program.cpp:147`.

The comment is a misunderstanding: lock-free concurrent reads are safe only against *other reads*,
not against a concurrent write.

**Evidence — ThreadSanitizer**, France config with `simple` intervention:

```
WARNING: ThreadSanitizer: data race
  Write of size 8 at 0x00010d60b708 by thread T10 (mutexes: write M0):
    #0  std::__1::__tree<…hgps::core::Identifier, hgps::DiseaseDefinition…>::__insert_node_at()
    #6  hgps::CachedRepository::load_disease_definition()   repository.cpp:134
    #7  hgps::CachedRepository::get_disease_definition()    repository.cpp:63
    #8  hgps::build_disease_module()::$_0                   disease.cpp:79
  Previous read of size 8 at 0x00010d60b708 by thread T4:
    #0  hgps::CachedRepository::get_disease_definition()    repository.cpp:57
    #1  hgps::build_disease_module()::$_0                   disease.cpp:79
```

Note `(mutexes: write M0)` on the write and no mutex on the read — exactly the asserted pattern.
The run reported 1,762 TSan warnings in total, but the overwhelming majority are false positives
inside oneTBB's own partitioner and task dispatcher, which vcpkg builds without instrumentation so
TSan cannot see its happens-before edges. 27 reports implicate `repository.cpp` directly, and those
are genuine.

**Fix direction.** Simplest correct change: move `std::scoped_lock` above the `contains` check.
Better for the new rewrite: resolve all disease definitions once during setup, before any worker
threads exist, and hand the modules immutable references thereafter.

### B-03 — Undefined behaviour in character classification

**What happens.** Eleven call sites pass a plain `char` to `<cctype>` functions. `std::isdigit`,
`std::isalpha`, `std::isspace`, `std::tolower` and `std::toupper` are defined only for arguments
representable as `unsigned char` or equal to `EOF`; any other value is undefined behaviour. On
every platform this project targets, `char` is signed, so any byte ≥ 0x80 produces a negative
argument.

```cpp
// identifier.cpp:44-52
void Identifier::validate_identifier() const {
    if (std::isdigit(value_.at(0))) {
    …
    if (!std::all_of(std::begin(value_), std::end(value_),
                     [](char c) { return std::isalpha(c) || std::isdigit(c) || c == '_'; })) {
```

**Reachability.** `Identifier` is constructed from disease codes, risk-factor names and CSV column
headers; `to_lower` (`string_util.cpp:19-23`) is applied to essentially every string read from
config and data. The data set includes country and disease names, and the config declares
`"encoding": "UTF8"` for the data index. A single accented character or a stray UTF-8 continuation
byte reaches these functions. In practice glibc and libc++ implement them as table lookups, and a
negative index reads before the table — silently returning whatever is there, or faulting.

**Fix direction.** `static_cast<unsigned char>` at every call site. The old rewrite did exactly this
(`06-rewrite-changes.md` §6).

### B-04 — `Identifier` equality and ordering disagree

**What happens.**

```cpp
// identifier.cpp:32-34
bool Identifier::operator==(const Identifier &rhs) const noexcept {
    return hash_code_ == rhs.hash_code_;
}
```

```cpp
// identifier.h:70
std::strong_ordering operator<=>(const Identifier &rhs) const noexcept = default;
```

The defaulted `<=>` compares members in declaration order — `value_` (the string) first, then
`hash_code_`. So `<`, `>`, `<=` and `>=` use lexicographic string ordering, while `==` and `!=` use
a 64-bit hash comparison.

**Consequences.** Two distinct identifier strings whose `std::hash` values collide compare **equal**
under `==` while `std::map` — which uses `operator<` — correctly keeps them as separate keys. The
containers and the comparison operator would disagree about identity. That breaks the consistency
that standard algorithms and containers assume between `==` and a strict weak ordering, and it makes
`a == b && a < b` simultaneously true, which is not a coherent equivalence relation.

**Practical likelihood.** A 64-bit hash collision among the few hundred identifiers a model uses is
vanishingly unlikely, so this is almost certainly not producing wrong results today. It is rated
high because the defect is silent, has no diagnostic, and its blast radius — every risk factor and
disease lookup in the model — is total. There is also no reason to keep it: string comparison of
short identifiers is not a measured bottleneck.

**Fix direction.** Compare `value_`. Retain `hash_code_` solely as the `std::hash<Identifier>`
implementation for `unordered_map` bucketing, where collisions are handled correctly by the
container.

### B-05 — Income category assignment depends on hash-bucket order

**What happens.** `StaticLinearModel::initialise_categorical_income` assigns a person's income
category by softmax over per-category linear models, then samples from the resulting distribution:

```cpp
// static_linear_model.cpp:1978-1983
double rand = random.next_double();
double cumulative_prob = 0.0;
for (const auto &[income, probability] : probabilities) {
    cumulative_prob += probability;
    if (rand < cumulative_prob) {
        person.income = income;
```

`probabilities` is `std::unordered_map<core::Income, double>`, populated by iterating `e_logits`
(also unordered), populated by iterating `logits` (also unordered), populated by iterating
`income_models_` — `std::unordered_map<core::Income, LinearModelParams>` (`static_linear_model.h:474`).

The **order in which categories are visited determines which category a given `rand` selects.**
Unordered-container iteration order is unspecified by the standard and differs between
implementations, so the same seed can assign a different income category to the same person on a
different platform. The normalising sum at `:1966-1969` is likewise an unordered floating-point
reduction.

**Why the experiments did not catch it.** Within a single build, `std::unordered_map` iteration is
a deterministic function of the key set and insertion history, so runs A–F all produced identical
results. Detecting this needs a second standard library, which was not available on the audit host.
The defect is nevertheless confirmed by reading: nothing in the code establishes an order.

**Why it matters.** `person.income` feeds risk-factor models, disease relative risks, and the
income-stratified CSV outputs. A cross-platform difference here is a difference in published model
results, not a cosmetic one — and it would be extremely hard to diagnose, because each platform is
internally self-consistent and reproducible.

**Fix direction.** Drive the CDF from an explicitly ordered sequence of categories — the same
`income_category_layout` the output writer already uses (`program.cpp:56`) — so that iteration
order is part of the model definition rather than an artefact of the container.

### B-06 — An unseeded run is silent and its recorded seed is wrong

**What happens.** Two lines, in different files, combine badly.

```cpp
// mtrandom.cpp:8-11
MTRandom32::MTRandom32() {
    std::random_device rd;
    engine_.seed(rd());
}
```

```cpp
// program.cpp:205-208
auto seed_generator = std::make_unique<hgps::MTRandom32>();
if (const auto seed = model_input->seed()) {
    seed_generator->seed(seed.value());
}
```

If `running.seed` is absent, the `if` does not fire and the master generator keeps its
`std::random_device` entropy. The run proceeds normally, prints nothing unusual, and is
irreproducible.

It is then mis-recorded. The results file's metadata is built as:

```cpp
// program.cpp:54
.seed = input.seed().value_or(0u)
```

so the file claims the run used seed `0`. Re-running with seed `0` will not reproduce it.

**Why it matters.** For a model whose output informs policy, the seed is provenance. A file that
records a seed it did not use is worse than one that records none: it invites a reproduction attempt
that silently produces different numbers.

All six shipped examples set a seed, so this is latent — but it is reachable by any user-written
config, and nothing warns them.

**Fix direction.** Treat a missing seed as either an error or an explicit opt-in; in the opt-in case,
draw the seed once, print it, and record the drawn value. Removing the entropy-seeded default
constructor entirely — as the old rewrite did (`06-rewrite-changes.md` §2) — makes the mistake
impossible to express.
