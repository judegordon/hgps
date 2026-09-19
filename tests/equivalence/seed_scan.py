#!/usr/bin/env python3
"""Many seeds of one implementation, asking only "did it finish, and is what it wrote sane?".

`sweep.py` runs many seeds and stores a *reduction* so a study can score the same runs many ways.
This asks a smaller question and so keeps nothing: for each seed, did the run finish, and does any
output file contain a non-finite or physically impossible number? That is the question behind
docs/backlog.md item 2 — how often does the energy balance run away, here and upstream — and
answering it needs no reduction at all, which is what makes five hundred seeds affordable.

The two differences from `sweep.py` matter and are deliberate:

  * **The two sides are scanned independently.** `sweep.py --tolerate-failures` drops a failing
    seed from *both* sides, because its samples have to stay the same set of runs. A census of
    which seeds each implementation refuses must not do that: "the baseline completes seed 80" is
    exactly the fact being established.
  * **A run that finishes is still read.** The baseline throws below the configured minimum weight
    and only *warns* above the maximum, so a runaway of the other sign leaves it exiting zero with
    an absurd number in the results file. A census that counted only exit codes would miss it,
    and missing it is how this class of defect ships.

    # the census behind docs/findings/seed-80.md
    python3 tests/equivalence/seed_scan.py --example KevinHall_FINCH --seeds 500 --side new \
        --out /tmp/hgps-scan/new.jsonl --workers 4
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import shutil
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from threading import Lock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run as harness  # noqa: E402  (the path has to be set first)

REPO = Path(__file__).resolve().parents[2]

# Columns whose value is a body weight in kilograms, checked against the configured maximum. A
# band *mean* above the per-person ceiling is absurd by construction: no average of values the
# engine considers describable can exceed the largest describable value.
WEIGHT_COLUMNS = ("mean_weight", "std_weight")

# Nothing any of these files legitimately carries is this large. Head counts are the biggest
# honest numbers here and the largest cohort this project runs is 1.24 million.
ABSURD_MAGNITUDE = 1e12


def scan_file(path: Path, weight_ceiling: float) -> list[dict]:
    """Every non-finite or physically impossible cell in one result file, with where it is."""
    findings: list[dict] = []
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        for row_number, row in enumerate(reader, start=2):
            for column, text in row.items():
                if column is None or text is None or text == "":
                    continue
                try:
                    value = float(text)
                except ValueError:
                    continue  # a label column
                if not math.isfinite(value):
                    kind = "non_finite"
                elif column in WEIGHT_COLUMNS and value > weight_ceiling:
                    kind = "weight_above_ceiling"
                elif abs(value) > ABSURD_MAGNITUDE:
                    kind = "absurd_magnitude"
                else:
                    continue
                findings.append({"file": path.name, "row": row_number, "column": column,
                                 "value": text,
                                 "at": f"{row.get('source', '?')} {row.get('time', '?')} "
                                       f"{row.get('gender_name', '?')} "
                                       f"age {row.get('index_id', '?')}",
                                 "kind": kind})
                if len(findings) >= 50:
                    return findings
    return findings


# What each implementation says when a body goes above the configured maximum without the run
# stopping. The baseline prints a line to stdout and carries on; this build counts a metric and
# carries on (`KevinHallModel::validate_weight`). Either way the run exits zero, so the *log* is
# the only place the event is visible to a census — which is the whole reason this function
# exists: a runaway of the upward sign leaves no trace in the exit code at all.
WEIGHT_WARNINGS = ("[WEIGHT RANGE WARNING]", "WeightAboveConfiguredMaximum")


def scan_log(path: Path) -> dict:
    """How many times the run said a weight was above the configured maximum, and the worst one."""
    if not path.exists():
        return {"weight_warnings": 0}
    count = 0
    worst = ""
    for line in path.read_text(errors="replace").splitlines():
        if not any(marker in line for marker in WEIGHT_WARNINGS):
            continue
        count += 1
        if not worst:
            worst = line.strip()[:400]
    return {"weight_warnings": count, "weight_warning": worst} if count else {
        "weight_warnings": 0}


def scan_metrics(folder: Path) -> dict:
    """This build's own count of the same event, which is a metric rather than a printed line.

    `validate_weight` counts `WeightAboveConfiguredMaximum` into the run metrics, and the metrics
    go into the results JSON rather than to stdout — so the two implementations record the same
    event in two different places and a census has to read both.
    """
    found: dict[str, float] = {}
    for path in sorted(folder.glob("*.json")):
        if path.name.endswith("_manifest.json"):
            continue
        try:
            document = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        for entry in document.get("results", []):
            for name, value in (entry.get("metrics") or {}).items():
                if "Weight" in name:
                    found[name] = max(found.get(name, 0.0), float(value))
    return {"weight_metrics": found} if found else {}


def peak_weight(path: Path) -> float:
    """The largest band mean weight in a result file — the precursor, when there is one."""
    peak = 0.0
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            text = row.get("mean_weight")
            if not text:
                continue
            try:
                value = float(text)
            except ValueError:
                continue
            if math.isfinite(value) and value > peak:
                peak = value
    return peak


def one_seed(*, seed: int, side: str, example: harness.Example, intervention: str | None,
             overlay: dict | None, binary: Path, compat: list[str], workdir: Path,
             stop_time: int | None, size_fraction: float | None,
             weight_ceiling: float, keep_files: bool, attempts: int) -> dict:
    is_baseline = side == "baseline"
    source = example.baseline_config if is_baseline else example.new_config

    folder = workdir / f"seed-{seed}" / "out"
    if folder.parent.exists():
        shutil.rmtree(folder.parent)
    folder.mkdir(parents=True)

    document = harness.derive_config(source, seed, folder, intervention, stop_time, is_baseline,
                                     overlay, size_fraction)
    # Not `config.json`: an example folder can contain a file of that name, which
    # `stage_example_files` links here, and `write_derived_config` refuses to write
    # through a link (ADR 0039). `KevinHall_FINCH` is exactly that case.
    config_path = folder.parent / f"config-seed-{seed}.json"
    harness.stage_example_files(source, config_path.parent)
    harness.write_derived_config(config_path, document)

    # The baseline is retried and this build is not, and the asymmetry is the measurement rather
    # than a favour. The baseline exits on a signal about one run in forty-five on this example —
    # the concurrency defect the audit recorded as B-01 and B-02, which `-T 1` does not reach
    # because the two scenarios still run in two threads — and counting that as "the baseline
    # refuses this seed" would fill a census of *deterministic* refusals with a defect that is
    # already recorded. So a baseline seed is tried three times and only a seed that fails all
    # three is refused; every retry is counted and reported, so the flake stays visible. This
    # build has no such flake and is given one attempt, so a refusal here is deterministic by
    # construction — which is what makes "seed 80 fails here and not there" a fact about the
    # model rather than about luck.
    extra = ["-T", "1"] if is_baseline else ["--threads", "1", *compat]
    log = folder.parent / "log.txt"
    record: dict = {"seed": seed, "side": side}
    retries: list[str] = []
    started = time.monotonic()
    try:
        record["seconds"] = round(harness.run(binary, config_path, extra, log,
                                              attempts=attempts, retries=retries,
                                              output_folder=folder), 2)
        record["completed"] = True
    except RuntimeError as failure:
        record["seconds"] = round(time.monotonic() - started, 2)
        record["completed"] = False
        # The last line of the log is the engine's own message; the RuntimeError's first line is
        # only the exit code. Both are kept: which one is informative differs by implementation.
        lines = [line for line in log.read_text(errors="replace").splitlines() if line.strip()]
        record["exit"] = str(failure).splitlines()[0]
        record["reason"] = lines[-1][:400] if lines else ""

    findings: list[dict] = []
    peak = 0.0
    if record["completed"]:
        try:
            written = harness.result_families(folder)
        except RuntimeError as problem:
            record["completed"] = False
            record["reason"] = f"no result file: {problem}"
            written = {}
        for path in written.values():
            findings.extend(scan_file(path, weight_ceiling))
        if harness.MAIN_FAMILY in written:
            peak = peak_weight(written[harness.MAIN_FAMILY])

    record.update(scan_log(log))
    if record["completed"]:
        record.update(scan_metrics(folder))
    record["retries"] = len(retries)
    record["findings"] = findings
    record["peak_mean_weight"] = round(peak, 4)
    record["clean"] = bool(record["completed"] and not findings)

    if not keep_files:
        shutil.rmtree(folder.parent, ignore_errors=True)
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--example", required=True)
    parser.add_argument("--seeds", type=int, default=500)
    parser.add_argument("--first-seed", type=int, default=1)
    parser.add_argument("--side", choices=("new", "baseline"), required=True)
    parser.add_argument("--out", type=Path, required=True, help="a JSONL file, one line per seed")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--stop-time", type=int, default=None)
    parser.add_argument("--size-fraction", type=float, default=None)
    parser.add_argument("--intervention", default=None)
    parser.add_argument("--weight-ceiling", type=float, default=210.0,
                        help="the configured maximum for 'Weight' in the example; a band mean "
                             "above it is impossible rather than extreme")
    parser.add_argument("--baseline", type=Path,
                        default=Path("/tmp/hgps-build/baseline-release/src/HealthGPS.Console/"
                                     "HealthGPS.Console"))
    parser.add_argument("--new", type=Path, default=REPO / "out/build/release/src/healthgps")
    parser.add_argument("--baseline-compat", default="all")
    parser.add_argument("--workdir", type=Path, default=None)
    parser.add_argument("--keep-files", action="store_true")
    parser.add_argument("--attempts", type=int, default=None,
                        help="tries per seed before it counts as refused. The default is 3 for "
                             "the baseline and 1 for this build; see `one_seed`.")
    arguments = parser.parse_args()

    available = harness.examples()
    if arguments.example not in available:
        parser.error(f"unknown example '{arguments.example}'; known: {', '.join(available)}")
    example = available[arguments.example]
    intervention = arguments.intervention or example.intervention
    overlay = harness.intervention_overlay(arguments.example)

    compat = ("" if arguments.baseline_compat.lower() in ("", "none")
              else arguments.baseline_compat)
    compat_arguments = ["--baseline-compat", compat] if compat else []
    binary = arguments.baseline if arguments.side == "baseline" else arguments.new
    if arguments.attempts is None:
        arguments.attempts = 3 if arguments.side == "baseline" else 1

    seeds = list(range(arguments.first_seed, arguments.first_seed + arguments.seeds))
    root = arguments.workdir or (Path("/tmp") / "hgps-scan" / arguments.example /
                                 arguments.side)
    root.mkdir(parents=True, exist_ok=True)
    arguments.out.parent.mkdir(parents=True, exist_ok=True)

    # One worker directory each, so two seeds never stage into the same folder.
    lock = Lock()
    done = [0]
    started = time.monotonic()
    stream = arguments.out.open("w")

    def work(seed: int) -> dict:
        record = one_seed(seed=seed, side=arguments.side, example=example,
                          intervention=intervention, overlay=overlay, binary=binary,
                          compat=compat_arguments,
                          workdir=root / f"worker-{seed % arguments.workers}",
                          stop_time=arguments.stop_time,
                          size_fraction=arguments.size_fraction,
                          weight_ceiling=arguments.weight_ceiling,
                          keep_files=arguments.keep_files,
                          attempts=arguments.attempts)
        with lock:
            stream.write(json.dumps(record) + "\n")
            stream.flush()
            done[0] += 1
            if record.get("weight_metrics"):
                print(f"    {arguments.side} seed {seed}: "
                      f"{record['weight_metrics']}, run still exited zero", flush=True)
            if record.get("weight_warnings"):
                print(f"    {arguments.side} seed {seed}: {record['weight_warnings']} weight(s) "
                      f"above the configured maximum, run still exited zero — "
                      f"{record.get('weight_warning', '')[:200]}", flush=True)
            if not record["clean"]:
                why = record.get("reason", "") or record["findings"][0]["kind"]
                print(f"    {arguments.side} seed {seed}: NOT CLEAN — {why[:200]}", flush=True)
            if done[0] % 25 == 0 or done[0] == len(seeds):
                rate = (time.monotonic() - started) / done[0]
                print(f"    {arguments.example} {arguments.side}: {done[0]}/{len(seeds)}, "
                      f"{rate:.1f}s each, {rate * (len(seeds) - done[0]) / 60:.0f} min left",
                      flush=True)
        return record

    with ThreadPoolExecutor(max_workers=arguments.workers) as pool:
        records = list(pool.map(work, seeds))
    stream.close()

    refused = [r["seed"] for r in records if not r["completed"]]
    flaked = sum(r.get("retries", 0) for r in records)
    dirty = [r["seed"] for r in records if r["completed"] and r["findings"]]
    warned = [r["seed"] for r in records
              if r.get("weight_warnings") or r.get("weight_metrics")]
    print(f"seed_scan: {arguments.example} {arguments.side}, {len(seeds)} seeds, "
          f"{len(refused)} refused, {len(dirty)} completed with a non-finite or impossible "
          f"value, {flaked} retried attempt(s), "
          f"{(time.monotonic() - started) / 60:.1f} min")
    if refused:
        print(f"    refused: {refused}")
    if dirty:
        print(f"    unsane output: {dirty}")
    if warned:
        print(f"    above the configured maximum without failing: {warned}")
    if not arguments.keep_files:
        shutil.rmtree(root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
