#!/usr/bin/env python3
"""The comparison rule's false-positive rate, measured on a null, at a scale a test suite can pay for.

`calibrate.py --mode null` is the real measurement: thirty pairs of twenty seeds across the three
runnable examples, half an hour of machine time, and the table in docs/equivalence-method.md 4.4.
This is the same measurement over the synthetic fixture pack, in about twenty seconds, so that the
rate is checked by CTest at every commit rather than by a document nobody re-runs.

**Every failure here is a false positive by construction.** Both sides are this build; the two seed
sets are disjoint; nothing is perturbed. So the number of runs that report any failure is the rule's
realised family-wise rate, and it can be held against the rate the rule promises.

The assertion, and the arithmetic behind it. The rule promises `FAMILY_WISE_ALPHA` — a 1% chance
that one comparison reports anything — so over the four pairs this runs, the expected number of runs
with a failure is 0.04. It asserts **at most one**, and that number is a trade rather than a
preference: at most zero would fail 4% of the time for no reason, which over the ten build
configurations CI runs is a flake a third of the time; at most one fails 0.06% of the time, and
still catches a rule whose realised rate is anywhere near the 25% the old one had on the same kind
of null. The pooled tail is printed beside it, because four pairs cannot see the far tail Holm
actually operates in and a reader should not have to guess that from the verdict.

    python3 tests/equivalence/null_check.py --seeds 160 --block 20
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import calibrate  # noqa: E402  (the path has to be set first)
import run as harness  # noqa: E402
import self_check  # noqa: E402

REPO = Path(__file__).resolve().parents[2]

# How many of the pairs may report a failure before this is a defect rather than a draw. See the
# module docstring for the arithmetic; it is one, not zero, and that is deliberate.
ALLOWED_PAIRS_WITH_A_FAILURE = 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seeds", type=int, default=160,
                        help="how many runs of the fixture pack; they are cut into disjoint blocks "
                             "of --block and paired, so this is 2 x --block x the pairs")
    parser.add_argument("--block", type=int, default=20,
                        help="how many seeds one comparison uses (default 20, what check.sh runs)")
    parser.add_argument("--new", type=Path, default=REPO / "out/build/release/src/healthgps")
    parser.add_argument("--workdir", type=Path, default=None)
    parser.add_argument("--json", type=Path, default=None)
    parser.add_argument("--allowed", type=int, default=ALLOWED_PAIRS_WITH_A_FAILURE)
    arguments = parser.parse_args()

    if not arguments.new.is_file():
        print(f"null_check: no binary at {arguments.new}", file=sys.stderr)
        return 2
    config = self_check.synthetic_config(arguments.new)
    if not config.is_file():
        print(f"null_check: no config at {config}", file=sys.stderr)
        return 2

    seeds = list(range(1, arguments.seeds + 1))
    blocks = calibrate.blocks(seeds, arguments.block)
    pairs = [(blocks[i], blocks[i + 1]) for i in range(0, len(blocks) - 1, 2)]
    if not pairs:
        parser.error(f"{arguments.seeds} seeds is not enough for one pair of {arguments.block}")

    workdir = arguments.workdir or (Path("/tmp") / "hgps-null-check")
    if workdir.exists():
        shutil.rmtree(workdir, ignore_errors=True)
    workdir.mkdir(parents=True, exist_ok=True)

    started = time.monotonic()
    # One pass over every seed, through exactly the code path the real comparison uses: the same
    # derived config, the same runner, the same reduction and the same emptying-band exclusion,
    # taken as the union over the whole sweep.
    files, empty, seconds = self_check.measure(arguments.new, config, seeds, workdir, "null",
                                               None, None, None)
    reduced = self_check.reduce_all(files, empty)

    runs = []
    for left_seeds, right_seeds in pairs:
        outcome = calibrate.score_pair(left_seeds, right_seeds, reduced, reduced,
                                       harness.FAMILY_WISE_ALPHA)
        testable = [c for c in outcome.comparisons if not c.below_floor]
        failures = [c for c in outcome.comparisons if not c.passed]
        runs.append({
            "left_seeds": [left_seeds[0], left_seeds[-1]],
            "right_seeds": [right_seeds[0], right_seeds[-1]],
            "tests": len(outcome.comparisons),
            "testable": len(testable),
            "failures": len(failures),
            "failing": sorted({f"{c.key[4]}/{c.statistic}" for c in failures}),
            "smallest_holm_adjusted_p": min((c.adjusted for c in testable), default=1.0),
            "tail": {f"{threshold:g}": sum(1 for c in testable if c.p_value < threshold)
                     for threshold in calibrate.TAIL_THRESHOLDS},
        })

    with_a_failure = sum(1 for run in runs if run["failures"])
    testable = sum(run["testable"] for run in runs)

    print(f"=== the rule against a null: {len(pairs)} pair(s) of {arguments.block} seeds of the "
          f"synthetic pack, {seconds:.1f}s of simulation")
    for run in runs:
        print(f"    seeds {run['left_seeds'][0]}-{run['left_seeds'][1]} vs "
              f"{run['right_seeds'][0]}-{run['right_seeds'][1]}: {run['failures']} failure(s) of "
              f"{run['testable']} testable ({run['tests']} tests), smallest Holm p "
              f"{run['smallest_holm_adjusted_p']:.3g}"
              + (f" — {', '.join(run['failing'])}" if run["failing"] else ""))
    print(f"    runs with at least one failure: {with_a_failure} observed, "
          f"{harness.FAMILY_WISE_ALPHA * len(pairs):.2f} expected at alpha "
          f"{harness.FAMILY_WISE_ALPHA:g}, at most {arguments.allowed} allowed")
    print(f"    the raw p-value tail, over the {testable} tests the printed-precision floor does "
          f"not waive:")
    for threshold in calibrate.TAIL_THRESHOLDS:
        key = f"{threshold:g}"
        observed = sum(run["tail"][key] for run in runs)
        print(f"      p < {key:>6}: {threshold * testable:>8.2f} expected, {observed:>5} observed")

    verdict = with_a_failure <= arguments.allowed
    if not verdict:
        print(f"    THE RULE IS NOT DELIVERING ITS RATE: {with_a_failure} of {len(pairs)} null "
              f"comparisons reported a failure, and every failure here is a false positive by "
              f"construction. Read docs/equivalence-method.md 4.4 and ADR 0048 before widening "
              f"anything: the point of the rule is that this number is known.")

    shutil.rmtree(workdir, ignore_errors=True)

    if arguments.json is not None:
        arguments.json.parent.mkdir(parents=True, exist_ok=True)
        arguments.json.write_text(json.dumps({
            "alpha": harness.FAMILY_WISE_ALPHA,
            "block": arguments.block,
            "pairs": len(pairs),
            "runs_with_at_least_one_failure": with_a_failure,
            "expected_runs_with_at_least_one_failure": harness.FAMILY_WISE_ALPHA * len(pairs),
            "allowed": arguments.allowed,
            "elapsed_seconds": time.monotonic() - started,
            "runs": runs,
        }, indent=2) + "\n")

    print()
    print("null check: " + ("PASS" if verdict else "FAIL"))
    return 0 if verdict else 1


if __name__ == "__main__":
    sys.exit(main())
