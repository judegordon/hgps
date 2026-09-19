#!/usr/bin/env python3
"""One implementation, many seeds, reduced once and stored — the raw material every calibration
in docs/equivalence-method.md is scored from.

`run.py` runs both implementations and compares them in the same process, which is right for the
check but wrong for a study: answering "does the rule deliver its stated false-positive rate?" or
"is the `std_polyunsaturatedfattyacid` offset real?" means scoring the *same* seeds many different
ways, and re-running a two-hundred-seed sweep for each way is hours of machine time for nothing.

So a sweep is separated from a scoring. This script runs the seeds and writes one reduction per
side in the stored-reference format `run.py` already reads; `calibrate.py` scores them.

Everything about how a run is derived, staged, executed and reduced comes from `run.py` — imported,
not copied — so a sweep goes through exactly the code path the real comparison does. In particular
the emptying-band exclusion is the union over **every side and every seed of the sweep**, taken from
the whole-population file, which is `run.py`'s rule applied to a larger set of runs.

    # the 200-seed study behind docs/equivalence.md, "Is the offset real?"
    python3 tests/equivalence/sweep.py --example KevinHall_FINCH --seeds 200 --sides both \
        --out /tmp/hgps-sweeps/KevinHall_FINCH-200

    # one build only, for a null calibration
    python3 tests/equivalence/sweep.py --example HLM_France --seeds 400 --sides new \
        --out /tmp/hgps-sweeps/HLM_France-400
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run as harness  # noqa: E402  (the path has to be set first)

REPO = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--example", required=True)
    parser.add_argument("--seeds", type=int, default=200)
    parser.add_argument("--first-seed", type=int, default=1)
    parser.add_argument("--sides", choices=("both", "new", "baseline"), default="both")
    parser.add_argument("--out", type=Path, required=True,
                        help="a directory; it gets baseline.csv.gz and/or new.csv.gz in the stored "
                             "reference format, and a manifest.json")
    parser.add_argument("--stop-time", type=int, default=None)
    parser.add_argument("--size-fraction", type=float, default=None)
    parser.add_argument("--intervention", default=None)
    parser.add_argument("--baseline", type=Path,
                        default=Path("/tmp/hgps-build/baseline-release/src/HealthGPS.Console/"
                                     "HealthGPS.Console"))
    parser.add_argument("--new", type=Path, default=REPO / "out/build/release/src/healthgps")
    parser.add_argument("--baseline-compat", default="all")
    parser.add_argument("--workdir", type=Path, default=None,
                        help="where the result files live between the two passes. They are deleted "
                             "when the sweep finishes unless --keep-files is given.")
    parser.add_argument("--keep-files", action="store_true")
    parser.add_argument("--tolerate-failures", action="store_true",
                        help="a seed either implementation refuses is DROPPED from the sweep on "
                             "both sides and recorded in the manifest, instead of ending the "
                             "sweep. A comparison must never do this — a run that does not "
                             "finish is a failure and `run.py` treats it as one. A two-hundred-"
                             "seed study is a different thing: the seed that failed is itself a "
                             "measurement, and losing the other 199 to it would be the wrong "
                             "trade. Use it, read `failed_runs` in the manifest, and quote the "
                             "count.")
    arguments = parser.parse_args()

    available = harness.examples()
    if arguments.example not in available:
        parser.error(f"unknown example '{arguments.example}'; "
                     f"known: {', '.join(available)}")
    example = available[arguments.example]
    intervention = arguments.intervention or example.intervention
    overlay = harness.intervention_overlay(arguments.example)

    compat = "" if arguments.baseline_compat.lower() in ("", "none") else arguments.baseline_compat
    compat_arguments = ["--baseline-compat", compat] if compat else []

    seeds = list(range(arguments.first_seed, arguments.first_seed + arguments.seeds))
    sides = (("baseline", "new") if arguments.sides == "both" else (arguments.sides,))

    workdir = arguments.workdir or (Path("/tmp") / "hgps-sweep" / arguments.example)
    workdir.mkdir(parents=True, exist_ok=True)
    arguments.out.mkdir(parents=True, exist_ok=True)

    # Pass one: run everything, keeping the result files and the head counts. The exclusion cannot
    # be applied until it is the union over the whole sweep, which is not known until the last run.
    files: dict[tuple[str, int], dict[str, Path]] = {}
    empty: set[tuple] = set()
    timings: dict[str, float] = {}
    retries: list[str] = []
    families: dict[str, dict] = {}
    failed: list[dict] = []
    dropped: set[int] = set()

    started = time.monotonic()
    for seed in seeds:
        for side in sides:
            if seed in dropped:
                continue
            is_baseline = side == "baseline"
            binary = arguments.baseline if is_baseline else arguments.new
            source = example.baseline_config if is_baseline else example.new_config

            folder = workdir / side / f"seed-{seed}"
            if folder.exists():
                shutil.rmtree(folder)
            folder.mkdir(parents=True)

            document = harness.derive_config(source, seed, folder, intervention,
                                             arguments.stop_time, is_baseline, overlay,
                                             arguments.size_fraction)
            config_path = folder.parent / f"config-seed-{seed}.json"
            harness.stage_example_files(source, config_path.parent)
            harness.write_derived_config(config_path, document)

            extra = (["-T", "1"] if is_baseline else ["--threads", "1", *compat_arguments])
            log = folder.parent / f"log-seed-{seed}.txt"
            try:
                timings[side] = timings.get(side, 0.0) + harness.run(
                    binary, config_path, extra, log,
                    attempts=3 if is_baseline else 1, retries=retries, output_folder=folder)
            except RuntimeError as failure:
                if not arguments.tolerate_failures:
                    raise
                # The seed is dropped from BOTH sides, so the two samples stay the same set of
                # runs and a difference between them cannot be a difference in which seeds each
                # side got. The reason is kept whole rather than summarised: it is the finding.
                dropped.add(seed)
                failed.append({"side": side, "seed": seed, "reason": str(failure).splitlines()[0],
                               "log": str(log)})
                print(f"    {arguments.example} seed {seed}: {side} FAILED and the seed is "
                      f"dropped from both sides — {str(failure).splitlines()[0]}", flush=True)
                continue

            written = harness.result_families(folder)
            files[(side, seed)] = written
            families[side] = {family: {"file": path.name,
                                       "empty_file": path.stat().st_size == 0}
                              for family, path in written.items()}
            empty |= harness.empty_bands(written[harness.MAIN_FAMILY])
        if seed % 10 == 0 or seed == seeds[-1]:
            done = seeds.index(seed) + 1
            rate = (time.monotonic() - started) / done
            print(f"    {arguments.example}: {done}/{len(seeds)} seeds, "
                  f"{rate:.1f}s each, {rate * (len(seeds) - done) / 60:.0f} min left", flush=True)

    # Pass two: reduce every run against the one exclusion, and write one file per side.
    kept = [seed for seed in seeds if seed not in dropped]
    for side in sides:
        rows = []
        for seed in kept:
            reduced = harness.reduce_families(files[(side, seed)], empty)
            rows.extend(harness.reduced_to_rows(reduced, seed))
        harness.write_reference(arguments.out / f"{side}.csv.gz", rows)
        print(f"    wrote {arguments.out / f'{side}.csv.gz'}", flush=True)

    (arguments.out / "manifest.json").write_text(json.dumps({
        "example": arguments.example,
        "sides": list(sides),
        "seeds": kept,
        "seeds_requested": seeds,
        "dropped_seeds": sorted(dropped),
        "failed_runs": failed,
        "intervention": intervention,
        "stop_time_override": arguments.stop_time,
        "size_fraction_override": arguments.size_fraction,
        "baseline_binary": str(arguments.baseline),
        "new_binary": str(arguments.new),
        "compat_flags": compat,
        "families": families,
        "excluded_bands": sorted(empty),
        "timings_seconds": timings,
        "retries": retries,
        "written_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }, indent=2) + "\n")

    if not arguments.keep_files:
        shutil.rmtree(workdir, ignore_errors=True)

    print(f"sweep: {arguments.example}, {len(kept)} of {len(seeds)} seeds kept, "
          f"{', '.join(sides)}, {len(empty)} excluded bands, {len(retries)} baseline retries, "
          f"{len(failed)} run(s) refused, {(time.monotonic() - started) / 60:.1f} min")
    for failure in failed:
        print(f"    seed {failure['seed']} ({failure['side']}): {failure['reason']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
