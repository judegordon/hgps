# 09 — Ideas and open questions

Prose only, as requested. Three sections: what to carry forward from the old rewrite, what to drop,
and what needs a ruling from you before the new rewrite starts.

---

## 1. Ideas worth carrying into the new rewrite

**The structured input-diagnostics system is the single best thing in the old rewrite.** Splitting
the baseline's one `HgpsException` into `InternalError` (programmer errors, carrying a
`std::source_location`, thrown) and `InputIssue`/`InputIssueReport` (user input problems, carrying a
level, a closed error code, an optional file/field/line/column location and a message, accumulated
rather than thrown) is a genuine design contribution with no baseline counterpart. It changes the
user experience from discovering one config error per run to seeing all of them at once, each
located. The closed `IssueCode` enumeration is what makes it more than a message list — it means
tooling can act on issues rather than parse strings. Carry this forward essentially as-is, and push
it further than the old rewrite did: it threads the report through config and model parsing but not
through CSV loading or data-index resolution, which are exactly the places users hit problems with
large data trees.

**Deriving sampling order from the domain rather than from a container.** The old rewrite's fix for
categorical income assignment is the pattern to generalise. Instead of iterating a hash map to build
a CDF, it derives an explicit ordered vector of categories from a hard-coded, domain-meaningful
sequence, then samples over that. The ordering becomes part of the model definition instead of an
artefact of the standard library. Make this a rule in the new rewrite: no probability distribution
is ever built by iterating an unordered container, and ideally make that structurally impossible
rather than a convention — a small `Categorical<T>` type that can only be constructed from an
ordered sequence would enforce it.

**Making irreproducibility unrepresentable.** Deleting the entropy-seeded default constructor so an
unseeded Mersenne Twister cannot be created is a small change with a good shape: it turns a runtime
hazard into a compile error. The general principle — encode invariants in types rather than in
review discipline — should be applied much more widely in the new rewrite than either codebase
applies it today. Two specific candidates are given in §3 below.

**Sequential-by-default scenario execution.** The old rewrite's decision to run baseline and
intervention one after the other rather than concurrently eliminated the only nondeterminism
actually observable in the baseline's shipped output, and the measurements say it cost very little:
the baseline's own parallelism produced no meaningful speed-up (37 s versus 39 s at one and ten
threads) because a global mutex serialises each parallel loop body anyway. Start the new rewrite
sequential and deterministic; add parallelism later, deliberately, where a measurement justifies it
and where the reduction order is fixed.

**Fixing contracts rather than fixing call sites.** Sorting inside `find_index_of_all`, guarding the
empty range in `parallel_for`, rejecting `p == 0.0` in the polar method, rejecting empty vectors in
empirical-discrete sampling, and taking the repository lock before the read rather than after — each
of these moves a correctness obligation from "every caller must remember" to "the function
guarantees it". That instinct is right and should be the default posture in the new rewrite.

**Splitting the monoliths.** Breaking `model_parser.cpp` (2,252 lines) into eleven units, one per
model family plus shared helpers, and `analysis_module.cpp` (2,202) into five, is straightforwardly
better and should be preserved. The `data_manager` split into one translation unit per data domain
is likewise good.

**Vendoring a runnable example.** Whatever else is decided about data, the old rewrite's property
that a fresh checkout can run something without network access is worth keeping. The baseline cannot
do this — every example points at a release URL — and it is why the baseline's own FINCH tests skip.

---

## 2. Ideas to drop

**Slot-based person IDs.** The baseline's lifetime-unique monotonic counter is correct and the old
rewrite's slot-reuse scheme is not, because the rewrite also ships per-person longitudinal tracking
output in which a recycled ID silently conflates two people. Take the baseline's design.

**Modulo integer sampling.** The old rewrite replaced a float-multiply scheme that could in
principle return an out-of-range value with `% range`, which cannot — but introduced modulo bias
where there was none. Neither is right. Use rejection sampling: it is a few lines, has no bias, and
has no out-of-range edge.

**Swallowing catch blocks.** Twenty `catch (const std::exception &)` and `catch (...)` blocks,
including one that silently substitutes an expected value when a predictor lookup fails, are a
direct regression against the goal of trustworthy output. A misspelled coefficient name should stop
the run, not produce plausible numbers. This is doubly wrong given that the same codebase contains a
good diagnostics system that these sites do not use.

**Dropping the tests.** Self-evident, but worth stating as a decision rather than an oversight: the
old rewrite's most significant change was deleting 471 passing tests, and it is the reason none of
its other changes can be trusted.

**Dropping all comments and documentation.** Nineteen comment lines in 13,059 is not minimalism, it
is an absence of recorded intent — and it is precisely why most of `06-rewrite-changes.md` has to
mark its rationale "inferred". The baseline's 17% is higher than necessary, much of it boilerplate
Doxygen, but the target is not zero. Record invariants and the reasons for non-obvious choices.

**The `undb` directory name for a tree that also holds IHME disease data**, and the `{TIMESTAMP}`
output-filename scheme that makes every run write to a different path. The latter actively obstructs
regression testing; the timestamp belongs inside the file's metadata, which is where it already is.

---

## 3. Questions that need a ruling before the new rewrite starts

**Licensing and provenance.** You said you believe Imperial owns the old rewrite. The audit found no
licence file anywhere in it, and its schema URLs point at a personal fork. The baseline is
BSD-3-Clause, which permits derivatives provided the notice, conditions and disclaimer are retained
— so a derivative is fine, but the new rewrite needs to carry that notice. Is the new rewrite
intended as a BSD-3-Clause derivative of the baseline, carrying Imperial's and INRAE's copyright
notice, or as an independent implementation that must avoid deriving from baseline source at all?
This changes how closely the new code may follow the baseline's structure, and it needs answering
before any code is written rather than after.

**The IHME data licence.** The disease data is declared in `index.json` as CC BY-NC-ND 4.0 —
non-commercial, **no derivatives**. That is materially more restrictive than the BSD licence on the
repository that contains it, and "no derivatives" is an awkward fit for a project whose R scripts
amalgamate and reprocess those CSVs. Should the new rewrite vendor this data at all, and if so under
what terms? If the answer is no, the alternative is fetch-on-demand with checksums, which costs the
offline-runnable property recommended above.

**`pulmonar` or `pulmonary`.** The upstream data is internally inconsistent: the directory is
`pulmonary`, `Metadata.json` says `pulmonar`, and upstream examples use both — which is why
`HLM_India` fails outright. The old rewrite settled on `pulmonar`, disagreeing with the baseline's
directory name, so the two data sets are now mutually incompatible on this disease. Which spelling
is correct? And more usefully: should the new rewrite validate the disease registry against the
directory tree at load time so this class of inconsistency is caught rather than discovered?

**Which config format is canonical.** The baseline's examples carry both a legacy `config.json` and
a modern `new_config.json`, and only the latter has `project_requirements` — which most current
baseline behaviour is gated on. The old rewrite went further and made `project_requirements`
mandatory, dropped `version`, changed `seed` from an array to a scalar and changed the `$schema`
contract, with the result that no upstream example runs unmodified. Should the new rewrite accept
upstream configs as-is, define a new format with a converter, or define a new format and treat the
upstream examples as out of scope? This determines whether the existing examples can serve as
acceptance tests, which bears directly on the next question.

**How the new rewrite will be validated.** This is the question I would most want answered. The old
rewrite is unverifiable by any means currently available: it has no tests, its RNG changes mean its
output cannot be compared to the baseline's even in principle, and no shared configuration exists
anyway. If the new rewrite is to be trustworthy, it needs a validation strategy chosen up front, and
the options are not equivalent. Bit-exact reproduction of the baseline is the strongest, but it
requires preserving every RNG draw order and every floating-point operation order, which forecloses
most of the improvements recommended above. Statistical equivalence — running both and comparing
distributions within tolerance — is weaker but leaves the design free. Porting the baseline's 471
tests establishes component-level correctness but says nothing about end-to-end equivalence. My
recommendation is porting the tests plus statistical equivalence on the reference example, but the
choice is yours and it constrains everything else.

**Whether cross-platform bit-reproducibility is a requirement.** The baseline is reproducible on a
fixed build but has at least one confirmed mechanism (B-05, income CDF over unordered iteration) by
which the same seed gives different results on a different standard library, and several unpinned
floating-point behaviours. If bit-identical results across Linux, Windows and macOS are a
requirement, that implies pinning floating-point contraction, avoiding `std::generate_canonical`,
and treating every reduction order as part of the model specification — a real cost, and much
easier to design in than to retrofit. If it is not a requirement, that should be stated, because
half the recommendations in this audit are motivated by it.

**Scope of the model surface.** The old rewrite kept all six intervention scenarios and all the risk
factor models, but ships data for only one — no HLM example at all, no PIF data, no
income-quintile inputs. Should the new rewrite target the full baseline model surface, or a defined
subset done properly? A smaller surface with real test data and real validation is worth more than a
complete surface that cannot be exercised, but only you can say which models are actually in use.

**Target platforms.** Neither codebase builds on macOS without shims; the baseline's CI targets Linux
and Windows. The audit had to supply four workarounds to build at all. Is macOS a supported
development platform for the new rewrite? It is cheap to support from the start (one `__APPLE__`
branch, avoiding PSTL and `<syncstream>`) and tedious to retrofit.
