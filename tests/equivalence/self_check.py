#!/usr/bin/env python3
"""The equivalence harness, pointed at itself.

`run.py` compares this implementation against the baseline and prints PASS or FAIL. It decides this
project's headline result, which means a mistake in it does not produce a wrong number — it produces
a *confident* one. It has unit tests of its own statistics, and two of its rules have been wrong
once each; both times a twenty-minute run was what found it.

Unit tests cannot answer the two questions that matter about a comparison — and `null_check.py`, in
the same directory, answers a third that neither of these can: does the comparison fail as *often*
as it says it does (docs/equivalence-method.md 4.4)?

  (a) **Does it pass when it should?** Two runs of the *same* implementation at *different* seeds
      differ by nothing but sampling noise. That is exactly the null hypothesis the harness's
      tolerances are derived under, so a harness whose thresholds are too tight fails here — and a
      failure here is a false positive in the real comparison, which is the expensive kind.

  (b) **Does it fail when it should?** A copy of this implementation with one output scaled by 1%,
      another by 5% and one counted series shifted by a whole lattice step must fail, on **those
      series and nothing else**. Anything else failing means the perturbation leaked; nothing failing
      means the harness cannot see a difference that is really there.

Both use the same `compare` and `report` from `run.py` — imported, not copied, because the point is
to test the rules that decide the real result, not a second set that resembles them.

The statistical rules themselves are set out in docs/equivalence-method.md.

    # (a) the same build against itself at two disjoint seed sets
    python3 tests/equivalence/self_check.py --mode seeds --example Synthetic --seeds 20

    # (b) the same build against a deliberately perturbed copy of itself
    python3 tests/equivalence/self_check.py --mode perturbation --example Synthetic --seeds 20

Both run against the synthetic fixture pack by default, which takes seconds, so both are CTest tests
and part of scripts/check.sh. `--example HLM_France` runs the same two checks against a real example
and takes about ten minutes; docs/equivalence-method.md records what that produced.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run as harness  # noqa: E402  (the path has to be set first)

REPO = Path(__file__).resolve().parents[2]

# The perturbation the failure test applies, what each part of it is for, and — measured rather than
# assumed — which parts the harness can see. docs/equivalence-method.md §7.2 has the arithmetic.
# The figures are from twenty seeds under the rule adopted in the ninth run (ADR 0048), which
# reports a Holm-adjusted p-value against a family-wise alpha of 0.01 rather than a ratio against
# an allowance.
#
#   mean_bmi    x1.01  a 1% shift in an aggregate calibration pins: the same value in every seed, on
#                      both sides. The smallest difference anybody would call a difference, against
#                      the tightest test the method has. DETECTED in all ten of its comparisons,
#                      through the exact distribution test the lattice rule routes a point mass to:
#                      p = 2.9e-11, Holm-adjusted over the run's 458 tests to 1.3e-8.
#   mean_energy  x1.05 the same again at 5%, as a control: if the 1% case failed and this did not, the
#                      fault would be in the test rather than in the harness. DETECTED, identically —
#                      both are point masses, and an exact test of two point masses at different
#                      values gives the same p-value whatever the gap between them is.
#   std_energy   x1.05 5% of a series that genuinely varies from seed to seed — twenty distinct values
#                      out of twenty — so this is the one part that exercises Welch's t rather than
#                      the exact path. DETECTED by the *location* test: p = 6.5e-23, Holm 3.0e-20.
#                      Its dispersion test passes (p = 0.84), which is right: scaling a series by
#                      1.05 moves its mean by 5% and its across-seed spread by 5% of a much smaller
#                      number.
#   emigrations   +1    one whole person added to one age band: one lattice step of a counted series,
#                      and the smallest possible change to a count. NOT DETECTED, for a reason worth
#                      knowing rather than working around — see below.
PERTURBATION = ("mean_bmi=scale:1.01;mean_energy=scale:1.05;std_energy=scale:1.05;"
                "emigrations=step:1")

# What must fail. Anything missing means the harness cannot see a difference that is really there;
# anything extra means the perturbation leaked into a series it does not name, which would make the
# whole result untrustworthy in the other direction.
DETECTED = {"mean_bmi", "mean_energy", "std_energy"}

# **Two of those three need enough seeds, and how many is arithmetic rather than judgement.**
#
# `mean_bmi` and `mean_energy` are point masses: one value in every seed on each side, and a
# different value on each side. The exact test (docs/equivalence-method.md 5.2) conditions on the
# margins, so the only arrangement of the 2n observations at least as extreme as the observed one
# is its mirror image, and its p-value is exactly `2 / C(2n, n)` — doubled for the two values
# tested, and then multiplied by the 300 to 460 tests a run of this pack makes, because every test
# in a run is corrected together (ADR 0048). **It does not depend on how big the shift is**: 1% and
# 5% give the same p-value, because a complete separation is a complete separation.
#
#   n                 6        8        10       11       12       20
#   the test's p      4.3e-3   3.1e-4   2.2e-5   5.7e-6   1.5e-6   2.9e-11
#   Holm over ~350    1        0.11     0.0076   0.0020   0.00052  1.0e-8
#
# So against alpha = 0.01 it is out of reach at six seeds, marginal at ten, and clear by a factor
# of fifteen at twelve. **This is a real change from the rule before ADR 0048**, which detected
# these at any seed count — because it compared a point mass against the printed-precision floor
# rather than testing it, and "the two constants differ by more than the baseline can print" is an
# observation rather than evidence that the two distributions differ. At six seeds it is not
# evidence: six against six splitting two values perfectly happens by chance about once in 230
# tries, and a run of this pack makes hundreds of tests.
#
# CTest runs six seeds under ThreadSanitizer and twenty everywhere else
# (docs/decisions/0046-what-runs-under-which-sanitizer.md), so this is not hypothetical, and the
# expectation is derived from the seed count rather than asserted.
POINT_MASS_NEEDS_SEEDS = 12
POINT_MASS_PERTURBATIONS = {"mean_bmi", "mean_energy"}

# What must NOT fail, although it is perturbed. This is an assertion about the *limits* of the method,
# and it is here rather than left out because the limit is worth pinning: if it ever stops holding,
# somebody should read this and find out why.
#
# A one-person shift in `emigrations` is invisible at twenty seeds, in two steps:
#
#   * in the FIRST simulated year the series is identically zero on both sides, so a shift of one
#     would be certain — but that year is skipped for `emigrations`, because the quantity is not
#     defined until a year has passed (docs/equivalence-method.md §3.3);
#   * in every later year the twenty seeds give eight or nine distinct values, so the series is not
#     lattice-valued and Welch's t is what judges it. **The best of its eight comparisons is
#     p = 0.145** — before any correction for multiplicity, and the run's Holm correction takes it
#     to 1.
#
# That number is the fact, and it is a better fact than the one it replaced: under the rule before
# ADR 0048 this was recorded as "0.29 to 0.33 of the allowance", which is a ratio against a
# threshold rather than a statement about evidence. p = 0.145 says plainly that one emigration in
# sixteen is the sort of difference twenty seeds of this pack produce about one time in seven by
# chance, and a test that called it a disagreement would be wrong.
#
# It also shows something subtler about the lattice rule, which is worth having written down: the
# classification is computed from the two samples *pooled*, so a real shift can push a series out of
# the lattice regime and into a numeric comparison too wide to see it. Sixty seeds give p = 0.016,
# measured rather than extrapolated — still nowhere near a family-wise 0.01 over hundreds of tests,
# and the perturbation run at sixty seeds fails in the same three variables and no others.
BELOW_NOISE = {"emigrations"}

def synthetic_config(new_binary: Path) -> Path:
    """The generated fixture pack's config, inside the build tree that produced the binary.

    The binary is at <build>/src/healthgps and the pack at <build>/tests/fixtures/pack, so the build
    root is two levels up from the binary. Derived from the binary rather than assumed, because
    `--new` can point at any build directory and the pack has to be that build's.
    """
    return new_binary.parents[1] / "tests" / "fixtures" / "pack" / "model" / "config.json"


def measure(binary: Path, config: Path, seeds: list[int], workdir: Path, label: str,
            perturbation: str | None, stop_time: int | None,
            intervention: str | None) -> tuple[dict[int, dict], set[tuple], float]:
    """Runs one side and returns its reduction per seed, its empty bands, and the seconds it took.

    Everything about how a run is derived, found and reduced comes from `run.py`, so the two sides of
    a self-check go through exactly the code path the real comparison uses.
    """
    reduced: dict[int, dict] = {}
    files: dict[int, dict[str, Path]] = {}
    empty: set[tuple] = set()
    seconds = 0.0

    for seed in seeds:
        folder = workdir / label / f"seed-{seed}"
        if folder.exists():
            shutil.rmtree(folder)
        folder.mkdir(parents=True)

        document = harness.derive_config(config, seed, folder, intervention, stop_time,
                                         is_baseline=False, overlay={})
        config_path = folder.parent / f"config-seed-{seed}.json"
        harness.stage_example_files(config, config_path.parent)
        harness.write_derived_config(config_path, document)

        extra = ["--threads", "1"]
        if perturbation:
            extra += ["--perturb", perturbation]

        seconds += harness.run(binary, config_path, extra,
                               folder.parent / f"log-seed-{seed}.txt", output_folder=folder)

        # Every family the run wrote, not just the whole-population file: the self-check has to go
        # through the same reduction the real comparison does, and since this run that reduction
        # covers the income-stratified files too. The exclusion still comes from the
        # whole-population file, for the reason `run.py`'s main loop gives.
        families = harness.result_families(folder)
        files[seed] = families
        empty |= harness.empty_bands(families[harness.MAIN_FAMILY])

    return files, empty, seconds


def reduce_all(files: dict[int, dict[str, Path]], excluded: set[tuple]) -> dict[int, dict]:
    return {seed: harness.reduce_families(paths, excluded) for seed, paths in files.items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=("seeds", "perturbation"), required=True,
                        help="'seeds': the same build at two disjoint seed sets, which must pass. "
                             "'perturbation': the same build against a corrupted copy of itself, "
                             "which must fail on exactly the corrupted series.")
    parser.add_argument("--example", default="Synthetic",
                        help="'Synthetic' (the generated fixture pack, seconds) or the name of an "
                             "example from run.py's table (minutes)")
    parser.add_argument("--seeds", type=int, default=20)
    parser.add_argument("--first-seed", type=int, default=1)
    parser.add_argument("--seed-offset", type=int, default=1000,
                        help="how far the second seed set is from the first, in 'seeds' mode. The "
                             "two sets must be disjoint or the comparison is partly a run against "
                             "itself, which passes for the wrong reason.")
    parser.add_argument("--stop-time", type=int, default=None)
    parser.add_argument("--perturbation", default=PERTURBATION)
    parser.add_argument("--new", type=Path, default=REPO / "out/build/release/src/healthgps")
    parser.add_argument("--workdir", type=Path, default=None)
    parser.add_argument("--json", type=Path, default=None)
    parser.add_argument("--verbose", action="store_true")
    arguments = parser.parse_args()

    if not arguments.new.is_file():
        print(f"self_check: no binary at {arguments.new}", file=sys.stderr)
        return 2

    intervention = None
    if arguments.example == "Synthetic":
        config = synthetic_config(arguments.new)
    else:
        available = harness.examples()
        if arguments.example not in available:
            parser.error(f"unknown example '{arguments.example}'; known: Synthetic, "
                         f"{', '.join(available)}")
        config = available[arguments.example].new_config
        intervention = available[arguments.example].intervention

    if not config.is_file():
        print(f"self_check: no config at {config}", file=sys.stderr)
        return 2

    seeds = list(range(arguments.first_seed, arguments.first_seed + arguments.seeds))
    if len(seeds) < 3:
        parser.error("at least 3 seeds are needed for a distribution to mean anything")

    workdir = arguments.workdir or (Path("/tmp") / "hgps-self-check" / arguments.mode)
    workdir.mkdir(parents=True, exist_ok=True)

    label = f"{arguments.example} self-check ({arguments.mode})"
    outcome = harness.Outcome(example=label, seeds=seeds)

    if arguments.mode == "seeds":
        other = [seed + arguments.seed_offset for seed in seeds]
        if set(other) & set(seeds):
            parser.error("the two seed sets overlap")
        left_files, left_empty, left_seconds = measure(
            arguments.new, config, other, workdir, "left", None, arguments.stop_time, intervention)
        right_files, right_empty, right_seconds = measure(
            arguments.new, config, seeds, workdir, "right", None, arguments.stop_time, intervention)

        # The left side ran at different seeds, so its reduction is keyed by those. `compare` wants
        # both sides keyed by the same seed list, and the pairing is arbitrary — the comparison is
        # between two *distributions*, not between run i and run i. Re-keying is what says so.
        left_files = {seeds[i]: left_files[other[i]] for i in range(len(seeds))}
        outcome.timings["left (seeds %d-%d)" % (other[0], other[-1])] = left_seconds
        outcome.timings["right (seeds %d-%d)" % (seeds[0], seeds[-1])] = right_seconds
        expected: set[str] = set()
    else:
        left_files, left_empty, left_seconds = measure(
            arguments.new, config, seeds, workdir, "clean", None, arguments.stop_time, intervention)
        right_files, right_empty, right_seconds = measure(
            arguments.new, config, seeds, workdir, "perturbed", arguments.perturbation,
            arguments.stop_time, intervention)
        outcome.timings["clean"] = left_seconds
        outcome.timings["perturbed"] = right_seconds
        expected = set(DETECTED)
        if len(seeds) < POINT_MASS_NEEDS_SEEDS:
            expected -= POINT_MASS_PERTURBATIONS

    excluded = left_empty | right_empty
    outcome.excluded_bands = len(excluded)

    # Both sides are this build, so the two family sets are the same set by construction and
    # `check_families` has nothing to find. They are recorded anyway, because the report prints a
    # per-family line and a self-check that showed "0 families" while comparing several would be
    # the harness misreporting itself — which is the one thing this script exists to catch.
    outcome.baseline_families = {family: {"file": path.name, "empty_file": False}
                                 for family, path in sorted(left_files[seeds[0]].items())}
    outcome.new_families = {family: {"file": path.name, "empty_file": False}
                            for family, path in sorted(right_files[seeds[0]].items())}
    outcome.family_failures = harness.check_families(outcome.baseline_families,
                                                      outcome.new_families)

    left = reduce_all(left_files, excluded)
    right = reduce_all(right_files, excluded)

    harness.compare(left, right, seeds, outcome)
    # `report` returns whether the comparison passed. In 'seeds' mode that is the whole answer; in
    # 'perturbation' mode a pass would be the failure, so the verdict is computed below and `report`
    # is called only to print.
    passed = harness.report(outcome, arguments.verbose)

    failing = sorted({harness.variable_of(c) for c in outcome.comparisons if not c.passed})
    verdict = passed

    if arguments.mode == "perturbation":
        print()
        print(f"    perturbation: {arguments.perturbation}")
        print(f"    expected to fail in:     {', '.join(sorted(expected))}")
        out_of_reach = sorted(DETECTED - expected)
        if out_of_reach:
            print(f"    out of reach at {len(seeds)} seeds: {', '.join(out_of_reach)} — a point "
                  f"mass shifted by any amount gives an exact p-value of 2/C({2 * len(seeds)}, "
                  f"{len(seeds)}), which Holm over this run's tests cannot take below alpha until "
                  f"{POINT_MASS_NEEDS_SEEDS} seeds. Read POINT_MASS_NEEDS_SEEDS in this file.")
        print(f"    expected NOT to fail in: {', '.join(sorted(BELOW_NOISE))} "
              f"(perturbed, and below the noise floor at these seeds)")
        print(f"    actually failed in:      {', '.join(failing) or '(nothing)'}")

        missed = sorted(expected - set(failing))
        leaked = sorted(set(failing) - expected)
        if missed:
            print(f"    NOT DETECTED: {', '.join(missed)} — the harness cannot see a difference "
                  f"that is really there")
        for variable in sorted(BELOW_NOISE & set(failing)):
            print(f"    NOW DETECTED: {variable} was below the noise floor when this expectation was "
                  f"written and is not any more. That is news rather than a failure: read "
                  f"BELOW_NOISE in this file and docs/equivalence-method.md §7.2, work out which "
                  f"of the two changed — the method or the example — and move it to DETECTED.")
        if leaked:
            print(f"    LEAKED: {', '.join(leaked)} — these are not perturbed and should not have "
                  f"failed")
        verdict = not missed and not leaked

    if arguments.json is not None:
        arguments.json.parent.mkdir(parents=True, exist_ok=True)
        document = harness.as_json(outcome)
        document["mode"] = arguments.mode
        document["failing_variables"] = failing
        if arguments.mode == "perturbation":
            document["perturbation"] = arguments.perturbation
            document["expected_failing_variables"] = sorted(expected)
            document["expected_below_noise_variables"] = sorted(BELOW_NOISE)
            document["out_of_reach_at_this_seed_count"] = sorted(DETECTED - expected)
        arguments.json.write_text(json.dumps(document, indent=2) + "\n")
        print(f"wrote {arguments.json}")

    print()
    print(f"self-check ({arguments.mode}): " + ("PASS" if verdict else "FAIL"))
    return 0 if verdict else 1


if __name__ == "__main__":
    sys.exit(main())
