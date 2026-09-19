#!/usr/bin/env python3
"""Does the comparison rule deliver the false-positive rate it claims, and is a given offset real?

`run.py` applies the rule. This scores stored sweeps (`sweep.py`) with it, which is the only way to
answer the two questions a comparison cannot answer about itself:

  `--mode null`      **Calibration.** One build against itself, on disjoint seed sets, scored by
                     exactly the rule the real comparison uses. Every failure here is a false
                     positive by construction, so the observed count is the rule's realised rate
                     and can be held against the rate it promises (FAMILY_WISE_ALPHA). A rule whose
                     observed rate exceeds its stated one by more than sampling noise is wrong and
                     must not be adopted: docs/equivalence-method.md 4.4 has the table this
                     produced.

  `--mode series`    **One question about one variable**, at whatever seed count the sweep has:
                     both implementations' mean and spread, year by year, with the same two tests
                     the comparison uses and no multiplicity correction, because the point is to
                     measure an effect rather than to police a family. This is what answered
                     whether `std_polyunsaturatedfattyacid` is really 1.1% low
                     (docs/equivalence.md).

  `--mode spread`    **How much a twenty-seed standard deviation wanders**, against the same
                     series' standard deviation over the whole sweep. It is the measurement that
                     decided between the two rules ADR 0048 considered: it is the size of the
                     thing the old allowance was estimating from the draws it judged.

    python3 tests/equivalence/calibrate.py --mode null --block 20 \\
        --pairs-from /tmp/hgps-sweeps/HLM_France-400:new --label HLM_France

    python3 tests/equivalence/calibrate.py --mode series \\
        --sweep /tmp/hgps-sweeps/KevinHall_FINCH-200 \\
        --variable std_polyunsaturatedfattyacid --variable std_fat
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run as harness  # noqa: E402  (the path has to be set first)

# The raw-p thresholds the tail table reports. The last two bracket `alpha / m`, which is the level
# Holm leaves for the smallest p-value in a run, and is therefore the only part of the distribution
# the verdict depends on.
TAIL_THRESHOLDS = (1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7)


def load(sweep: Path, side: str) -> dict[int, dict]:
    path = sweep / f"{side}.csv.gz"
    if not path.is_file():
        raise SystemExit(f"{path}: no such sweep; run sweep.py first")
    return harness.read_reference(path)


def blocks(seeds: list[int], size: int) -> list[list[int]]:
    return [seeds[start:start + size] for start in range(0, len(seeds) - size + 1, size)]


def score_pair(left_seeds: list[int], right_seeds: list[int], data: dict[int, dict],
               other: dict[int, dict], alpha: float) -> harness.Outcome:
    """One null comparison: two disjoint seed sets of the same build, through `compare`.

    The two sides are re-keyed onto one seed list because `compare` wants both keyed the same way
    and the pairing is arbitrary — the comparison is between two *distributions*, not between run i
    and run i. `self_check.py` does the same thing for the same reason.
    """
    common = list(range(1, len(left_seeds) + 1))
    left = {common[i]: data[left_seeds[i]] for i in range(len(common))}
    right = {common[i]: other[right_seeds[i]] for i in range(len(common))}
    outcome = harness.Outcome(example="null", seeds=common)
    harness.compare(left, right, common, outcome, alpha=alpha)
    return outcome


def null_mode(arguments) -> dict:
    alpha = arguments.alpha
    sources: list[dict] = []

    for specification in arguments.pairs_from:
        sweep, _, side = specification.partition(":")
        side = side or "new"
        data = load(Path(sweep), side)
        seeds = sorted(data)
        grouped = blocks(seeds, arguments.block)
        pairs = [(grouped[i], grouped[i + 1]) for i in range(0, len(grouped) - 1, 2)]
        if not pairs:
            raise SystemExit(f"{sweep}: {len(seeds)} seeds is not enough for one pair of "
                             f"{arguments.block}")

        manifest = json.loads((Path(sweep) / "manifest.json").read_text())
        runs = []
        for left_seeds, right_seeds in pairs:
            outcome = score_pair(left_seeds, right_seeds, data, data, alpha)
            testable = [c for c in outcome.comparisons if not c.below_floor]
            failures = [c for c in outcome.comparisons if not c.passed]
            runs.append({
                "left_seeds": [left_seeds[0], left_seeds[-1]],
                "right_seeds": [right_seeds[0], right_seeds[-1]],
                "tests": len(outcome.comparisons),
                "testable": len(testable),
                "below_printed_precision": len(outcome.comparisons) - len(testable),
                "failures": len(failures),
                "failing": sorted({f"{c.key[0]}/{c.key[4]}/{c.statistic}" for c in failures}),
                "smallest_holm_adjusted_p": min((c.adjusted for c in testable), default=1.0),
                "tail": {f"{threshold:g}": sum(1 for c in testable if c.p_value < threshold)
                         for threshold in TAIL_THRESHOLDS},
            })
            print(f"    {manifest['example']} {side}: seeds {left_seeds[0]}-{left_seeds[-1]} "
                  f"vs {right_seeds[0]}-{right_seeds[-1]}: {runs[-1]['failures']} failure(s) of "
                  f"{runs[-1]['testable']} testable, smallest Holm p "
                  f"{runs[-1]['smallest_holm_adjusted_p']:.3g}", flush=True)

        sources.append({
            "sweep": str(sweep),
            "side": side,
            "example": manifest["example"],
            "label": arguments.label or manifest["example"],
            "block": arguments.block,
            "pairs": len(pairs),
            "runs": runs,
        })

    total_pairs = sum(source["pairs"] for source in sources)
    total_failures = sum(run["failures"] for source in sources for run in source["runs"])
    runs_with_a_failure = sum(1 for source in sources for run in source["runs"]
                              if run["failures"])
    testable = sum(run["testable"] for source in sources for run in source["runs"])
    tail = {f"{threshold:g}": sum(run["tail"][f"{threshold:g}"]
                                  for source in sources for run in source["runs"])
            for threshold in TAIL_THRESHOLDS}

    result = {
        "mode": "null",
        "alpha": alpha,
        "block": arguments.block,
        "pairs": total_pairs,
        "runs_with_at_least_one_failure": runs_with_a_failure,
        "expected_runs_with_at_least_one_failure": alpha * total_pairs,
        "total_failures": total_failures,
        "testable_tests": testable,
        "tail_observed": tail,
        "tail_expected": {f"{threshold:g}": threshold * testable
                          for threshold in TAIL_THRESHOLDS},
        "sources": sources,
    }

    print()
    print(f"=== null calibration: {total_pairs} pair(s) of {arguments.block} seeds, "
          f"alpha {alpha:g}")
    print(f"    runs with at least one failure: {runs_with_a_failure} observed, "
          f"{alpha * total_pairs:.2f} expected")
    print(f"    failures in total: {total_failures} of {testable} testable tests")
    print(f"    the raw p-value tail, over every test the printed-precision floor does not waive:")
    print(f"      {'threshold':>10} {'expected':>12} {'observed':>10}")
    for threshold in TAIL_THRESHOLDS:
        key = f"{threshold:g}"
        print(f"      {key:>10} {threshold * testable:>12.2f} {tail[key]:>10}")
    return result


def series_mode(arguments) -> dict:
    baseline = load(arguments.sweep, "baseline")
    mine = load(arguments.sweep, "new")
    seeds = sorted(set(baseline) & set(mine))
    manifest = json.loads((arguments.sweep / "manifest.json").read_text())

    wanted = set(arguments.variable)
    keys = sorted({key for seed in seeds for key in baseline[seed]
                   if key[4] in wanted and key[0] in arguments.family})

    rows = []
    for key in keys:
        family, scenario, year, sex, variable = key
        left = [baseline[seed][key] for seed in seeds if key in baseline[seed]]
        right = [mine[seed][key] for seed in seeds if key in mine[seed]]
        if len(left) != len(seeds) or len(right) != len(seeds):
            continue

        mean_left, mean_right = statistics.fmean(left), statistics.fmean(right)
        sd_left = statistics.stdev(left)
        sd_right = statistics.stdev(right)
        _, df, p_location = harness.welch_t_test(left, right)
        _, _, p_dispersion = harness.welch_t_test(harness.absolute_deviations(left),
                                                  harness.absolute_deviations(right))
        # The same floor the comparison applies: a difference the baseline's six printed digits
        # cannot express is not a difference, however certain a test of it is. A series pinned to
        # one value on each side gives p = 0 without it, which would make this table a list of
        # rounding.
        scale = max(abs(mean_left), abs(mean_right), sd_left, sd_right, 1e-12)
        floor = harness.PRINTED_PRECISION_FLOOR * scale
        below_floor = abs(mean_right - mean_left) <= floor
        rows.append({
            "family": family, "scenario": scenario, "year": year, "sex": sex,
            "variable": variable, "n": len(seeds),
            "baseline_mean": mean_left, "new_mean": mean_right,
            "relative": (mean_right - mean_left) / mean_left if mean_left else math.inf,
            "baseline_sd": sd_left, "new_sd": sd_right,
            "standard_errors": ((mean_right - mean_left)
                                / math.sqrt(sd_left ** 2 / len(seeds) + sd_right ** 2 / len(seeds))
                                if sd_left or sd_right else math.inf),
            "welch_df": df,
            "p_location": p_location,
            "p_dispersion": p_dispersion,
            "printed_precision_floor": floor,
            "below_printed_precision": below_floor,
        })

    by_variable: dict[str, list[dict]] = {}
    for row in rows:
        by_variable.setdefault(row["variable"], []).append(row)

    summary = {}
    for variable, group in sorted(by_variable.items()):
        group = [row for row in group if not row["below_printed_precision"]] or group
        relatives = [row["relative"] for row in group]
        negative = sum(1 for value in relatives if value < 0)
        summary[variable] = {
            "series": len(group),
            "median_relative": statistics.median(relatives),
            "largest_relative": max(relatives, key=abs),
            "same_sign": max(negative, len(group) - negative),
            "location_below_0.05": sum(1 for row in group if row["p_location"] < 0.05),
            "location_below_bonferroni": sum(1 for row in group
                                             if row["p_location"] < 0.05 / len(group)),
            "dispersion_below_0.05": sum(1 for row in group if row["p_dispersion"] < 0.05),
            "dispersion_below_bonferroni": sum(1 for row in group
                                               if row["p_dispersion"] < 0.05 / len(group)),
        }

    print()
    print(f"=== {manifest['example']}: {len(seeds)} seeds, both implementations")
    for variable, counts in summary.items():
        print(f"    {variable}: {counts['series']} series "
              f"(scenario x sex x year), median difference "
              f"{counts['median_relative'] * 100:+.3f}%, largest "
              f"{counts['largest_relative'] * 100:+.3f}%, "
              f"{counts['same_sign']}/{counts['series']} with the same sign")
        print(f"      mean:   {counts['location_below_0.05']}/{counts['series']} below p=0.05, "
              f"{counts['location_below_bonferroni']}/{counts['series']} below "
              f"Bonferroni over these series")
        print(f"      spread: {counts['dispersion_below_0.05']}/{counts['series']} below p=0.05, "
              f"{counts['dispersion_below_bonferroni']}/{counts['series']} below Bonferroni")

    return {"mode": "series", "example": manifest["example"], "seeds": seeds,
            "summary": summary, "rows": rows}


def spread_mode(arguments) -> dict:
    """How far a block of `--block` seeds' standard deviation is from the whole sweep's.

    This is the size of the thing the rule before ADR 0048 estimated from the twenty draws it was
    judging: if a twenty-seed `s` is routinely 0.7 or 1.4 times the truth, then an allowance built
    as `4.5 s` is routinely 30% too narrow or 40% too wide, and the failure count it produces is
    partly a property of the seed set.
    """
    data = load(arguments.sweep, arguments.side)
    seeds = sorted(data)
    grouped = blocks(seeds, arguments.block)
    keys = sorted({key for seed in seeds for key in data[seed]})

    ratios = []
    for key in keys:
        values = [data[seed].get(key) for seed in seeds]
        if any(value is None for value in values):
            continue
        whole = statistics.stdev(values)
        if whole <= 0.0 or abs(statistics.fmean(values)) <= 0.0:
            continue
        # Only series that really vary: a pinned one has nothing to estimate and its comparison is
        # decided by the printed-precision floor either way.
        if whole / abs(statistics.fmean(values)) < harness.PRINTED_PRECISION_FLOOR:
            continue
        for block in grouped:
            ratios.append(statistics.stdev([data[seed][key] for seed in block]) / whole)

    ratios.sort()
    result = {
        "mode": "spread",
        "side": arguments.side,
        "block": arguments.block,
        "whole_sweep_seeds": len(seeds),
        "blocks": len(grouped),
        "samples": len(ratios),
        "quantiles": {name: harness.quantile(ratios, q)
                      for name, q in (("p1", 0.01), ("p5", 0.05), ("p25", 0.25), ("p50", 0.50),
                                      ("p75", 0.75), ("p95", 0.95), ("p99", 0.99))},
        "min": ratios[0] if ratios else math.nan,
        "max": ratios[-1] if ratios else math.nan,
    }
    print()
    print(f"=== a {arguments.block}-seed standard deviation against the {len(seeds)}-seed one, "
          f"over {result['samples']} (series, block) pairs")
    for name, value in result["quantiles"].items():
        print(f"    {name}: {value:.3f}")
    print(f"    min {result['min']:.3f}, max {result['max']:.3f}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=("null", "series", "spread"), required=True)
    parser.add_argument("--pairs-from", action="append", default=[],
                        help="null mode: <sweep directory>[:<side>], repeatable. Each contributes "
                             "as many disjoint pairs of --block seeds as it has room for.")
    parser.add_argument("--sweep", type=Path, default=None,
                        help="series and spread modes: the sweep directory")
    parser.add_argument("--side", default="new", help="spread mode: which side of the sweep")
    parser.add_argument("--variable", action="append", default=[],
                        help="series mode: which variable(s) to test, repeatable")
    parser.add_argument("--family", action="append", default=None,
                        help="series mode: which output family(ies); default the whole population")
    parser.add_argument("--block", type=int, default=20,
                        help="how many seeds one comparison uses (default 20, what check.sh runs)")
    parser.add_argument("--alpha", type=float, default=harness.FAMILY_WISE_ALPHA,
                        help="the family-wise rate to score at; the default is the one the "
                             "harness uses, and a calibration that scored at another number "
                             "would be calibrating a rule nothing runs")
    parser.add_argument("--label", default=None)
    parser.add_argument("--json", type=Path, default=None)
    arguments = parser.parse_args()
    arguments.family = arguments.family or [harness.MAIN_FAMILY]

    if arguments.mode == "null":
        if not arguments.pairs_from:
            parser.error("null mode needs at least one --pairs-from")
        result = null_mode(arguments)
    else:
        if arguments.sweep is None:
            parser.error(f"{arguments.mode} mode needs --sweep")
        if arguments.mode == "series":
            if not arguments.variable:
                parser.error("series mode needs at least one --variable")
            result = series_mode(arguments)
        else:
            result = spread_mode(arguments)

    if arguments.json is not None:
        arguments.json.parent.mkdir(parents=True, exist_ok=True)
        arguments.json.write_text(json.dumps(result, indent=2) + "\n")
        print(f"wrote {arguments.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
