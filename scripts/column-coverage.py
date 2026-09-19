#!/usr/bin/env python3
"""Every column of every output family, on both sides, and which of them are all zero.

This is the check that would have caught the 49.

For as long as this build has written income-stratified result files, 49 of their columns were
identically zero in every row while the baseline filled them — and nothing said so, because the
equivalence harness reduced the whole-population CSV and no test that ran a configuration could
reach the income series at all (docs/SUMMARY.md, docs/backlog.md item 2). The harness now compares
every family, which catches a column whose *values* disagree. It does not catch a column that is
zero on both sides for different reasons, and it is weakest exactly where the defect lived: a
variable that is legitimately absent for an example reduces to nothing on both sides and is
skipped.

So this script asks a blunter question, of the files themselves rather than of a reduction:

  * which columns does each output family have, on each side;
  * which of them are identically zero in every row of the file.

and fails when a column is **present in the baseline, present here, identically zero here and not
identically zero in the baseline**. That is the shape of the 49, and it is a shape no statistical
comparison can express, because "the baseline has numbers and we have nothing" is not a
disagreement about a distribution.

Two modes:

    scripts/column-coverage.py --refresh          # run both implementations, store the baseline's
    scripts/column-coverage.py                    # run this build, check against the stored one

`--refresh` needs the baseline binary (docs/build-notes.md). The check mode does not, which is what
lets it be a CI job: the baseline's inventory is stored beside the reduction it belongs to, keyed by
the same hash of the same derived config, so it cannot be matched to a configuration it did not come
from.

**One seed.** The inventory is taken at a single seed rather than the twenty the reduction uses.
A column identically zero across every row of a whole run — 4,884 rows on `KevinHall_FINCH`, 16,564
on the other two — is not a property that another seed changes; what varies with the seed is the
values, and those are what the harness compares. The seed is recorded in the stored file.
"""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

_spec = importlib.util.spec_from_file_location("eqrun", REPO / "tests/equivalence/run.py")
eqrun = importlib.util.module_from_spec(_spec)
sys.modules["eqrun"] = eqrun
_spec.loader.exec_module(eqrun)

# The examples the script covers, and the cohort fraction each is measured at. `HLM_India` ships a
# 1.24-million-person cohort and is compared at a hundredth of it for the same reason the harness
# compares it there (docs/equivalence.md); the fraction is part of the derived config and therefore
# part of the hash, so an inventory taken at one fraction cannot be matched to a config at another.
SIZE_FRACTION = {"HLM_India": 1e-5}

# One seed, and always the same one: the inventory is about which columns are filled at all.
INVENTORY_SEED = 1


def inventory(path: Path) -> dict:
    """One family's columns, and which of them are identically zero in every row."""
    if path.stat().st_size == 0:
        # The baseline opens `_IndividualIDTracking.csv` for every run whose config enables
        # tracking and writes nothing to it when no person passes the filter — not even a header.
        return {"empty_file": True, "rows": 0, "columns": [], "all_zero": []}

    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        columns = list(reader.fieldnames or [])
        non_zero: set[str] = set()
        rows = 0
        for row in reader:
            rows += 1
            for column in columns:
                if column in non_zero:
                    continue
                text = row[column]
                try:
                    if float(text) != 0.0:
                        non_zero.add(column)
                except ValueError:
                    # A non-numeric column — `source`, `gender_name` — is never "all zero".
                    non_zero.add(column)

    return {"empty_file": False, "rows": rows, "columns": columns,
            "all_zero": sorted(set(columns) - non_zero)}


def take_inventory(example: "eqrun.Example", label: str, binary: Path, source: Path,
                   is_baseline: bool, workdir: Path, compat: str) -> dict[str, dict]:
    """Runs one implementation once and inventories every CSV it wrote."""
    folder = workdir / example.name / label / f"seed-{INVENTORY_SEED}"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)

    overlay = eqrun.intervention_overlay(example.name)
    document = eqrun.derive_config(source, INVENTORY_SEED, folder, example.intervention, None,
                                   is_baseline, overlay, SIZE_FRACTION.get(example.name))
    config_path = folder.parent / f"config-seed-{INVENTORY_SEED}.json"
    eqrun.stage_example_files(source, config_path.parent)
    eqrun.write_derived_config(config_path, document)

    extra = (["-T", "1"] if is_baseline
             else ["--threads", "1"] + (["--baseline-compat", compat] if compat else []))
    elapsed = eqrun.run(binary, config_path, extra, folder.parent / f"log-seed-{INVENTORY_SEED}.txt",
                        attempts=3 if is_baseline else 1, output_folder=folder)
    print(f"    {example.name} · {label}: {elapsed:.1f}s", flush=True)

    return {name: inventory(path)
            for name, path in sorted(eqrun.result_families(folder).items())}


def stored_path(reference_dir: Path, example: str, config_hash: str) -> Path:
    return reference_dir / example / f"columns-{config_hash}.json"


def compare(example: str, baseline: dict[str, dict], mine: dict[str, dict]) -> list[str]:
    """Every failure, as a line of text. An empty list is a pass."""
    failures: list[str] = []

    families = sorted(set(baseline) | set(mine))
    for family in families:
        theirs = baseline.get(family)
        ours = mine.get(family)

        if theirs is not None and ours is None:
            excluded = eqrun.BASELINE_ONLY_FAMILIES.get(family)
            if excluded is not None and theirs.get("empty_file"):
                identifier, reason = excluded
                print(f"    {example} · {family}: baseline only, and empty — excluded ({identifier}: "
                      f"{reason})")
                continue
            if excluded is not None:
                failures.append(
                    f"{example} · {family}: excluded as {identifier} on the ground that the "
                    f"baseline's file is empty — but it has {theirs['rows']} row(s), so the "
                    f"exclusion no longer holds")
                continue
            failures.append(f"{example} · {family}: the baseline writes this family and this build "
                            f"does not")
            continue

        if ours is not None and theirs is None:
            failures.append(f"{example} · {family}: this build writes this family and the baseline "
                            f"does not")
            continue

        their_columns = set(theirs["columns"])
        our_columns = set(ours["columns"])
        for column in sorted(their_columns - our_columns):
            failures.append(f"{example} · {family} · {column}: a column the baseline writes and "
                            f"this build does not")
        for column in sorted(our_columns - their_columns):
            failures.append(f"{example} · {family} · {column}: a column this build writes and the "
                            f"baseline does not")

        their_zero = set(theirs["all_zero"])
        our_zero = set(ours["all_zero"])
        shared = their_columns & our_columns

        # The rule this script exists for.
        for column in sorted((our_zero & shared) - their_zero):
            failures.append(
                f"{example} · {family} · {column}: identically zero in all {ours['rows']} rows "
                f"here and filled in the baseline's file — a column this build does not compute")

        # And its mirror, which is not a failure but must be a *recorded* difference rather than a
        # surprise: the only reason to fill a column the baseline leaves empty is that the baseline
        # is defective and the harness knows it (docs/deviations.md B-22).
        for column in sorted((their_zero & shared) - our_zero):
            if column in eqrun.BASELINE_DOES_NOT_COMPUTE:
                identifier, _ = eqrun.BASELINE_DOES_NOT_COMPUTE[column]
                print(f"    {example} · {family} · {column}: zero in the baseline and filled here "
                      f"({identifier})")
                continue
            failures.append(
                f"{example} · {family} · {column}: identically zero in the baseline's file and "
                f"filled here, and it is not in the harness's BASELINE_DOES_NOT_COMPUTE — either "
                f"the baseline defect is undocumented or this build is writing something it "
                f"should not")

    return failures


def summarise(example: str, baseline: dict[str, dict], mine: dict[str, dict]) -> None:
    print(f"    {example}: {len(mine)} family(ies) here, {len(baseline)} in the baseline")
    for family in sorted(set(baseline) | set(mine)):
        theirs = baseline.get(family, {})
        ours = mine.get(family, {})
        columns = len(ours.get("columns", theirs.get("columns", [])))
        print(f"      {family:<24} {columns:>4} columns, "
              f"{len(ours.get('all_zero', [])):>3} all-zero here, "
              f"{len(theirs.get('all_zero', [])):>3} all-zero in the baseline")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--example", action="append", default=None,
                        help="which example to inventory (repeatable); default: all three that "
                             "can be run")
    parser.add_argument("--refresh", action="store_true",
                        help="run the baseline binary and overwrite the stored inventory")
    parser.add_argument("--baseline", type=Path,
                        default=Path("/tmp/hgps-build/baseline-release/src/HealthGPS.Console/"
                                     "HealthGPS.Console"))
    parser.add_argument("--new", type=Path, default=REPO / "out/build/release/src/healthgps")
    parser.add_argument("--workdir", type=Path, default=None)
    parser.add_argument("--reference-dir", type=Path, default=eqrun.REFERENCE_DIR)
    parser.add_argument("--baseline-compat", default="all",
                        help="the compatibility flags this build runs with, matching the harness's "
                             "default: the inventory is of the build the comparison grades")
    parser.add_argument("--json", type=Path, default=None,
                        help="also write the full inventory of both sides as JSON")
    arguments = parser.parse_args()

    available = eqrun.examples()
    names = arguments.example or list(available)
    for name in names:
        if name not in available:
            parser.error(f"unknown example '{name}'; known: {', '.join(available)}")

    compat = "" if arguments.baseline_compat.lower() in ("", "none") else arguments.baseline_compat
    workdir = arguments.workdir or (Path("/tmp") / "hgps-column-coverage")
    workdir.mkdir(parents=True, exist_ok=True)

    failures: list[str] = []
    collected: list[dict] = []

    for name in names:
        example = available[name]
        if not example.new_config.is_file():
            print(f"=== {name}: SKIPPED, {example.new_config} does not exist")
            continue

        print(f"=== {name}")
        overlay = eqrun.intervention_overlay(name)
        document = eqrun.derive_config(example.baseline_config, 0, Path("/results"),
                                       example.intervention, None, True, overlay,
                                       SIZE_FRACTION.get(name), absolute=False)
        document["running"].pop("seed", None)
        config_hash = eqrun.sha256_of(json.dumps(document, sort_keys=True))
        path = stored_path(arguments.reference_dir, name, config_hash)

        mine = take_inventory(example, "new", arguments.new, example.new_config, False, workdir,
                              compat)

        if arguments.refresh:
            baseline = take_inventory(example, "baseline", arguments.baseline,
                                      example.baseline_config, True, workdir, compat)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps({
                "example": name,
                "baseline_config_sha256": config_hash,
                "intervention": example.intervention,
                "size_fraction_override": SIZE_FRACTION.get(name),
                "seed": INVENTORY_SEED,
                "baseline_binary": str(arguments.baseline),
                "what": "every column of every CSV family the baseline wrote, and which of them "
                        "are identically zero in every row",
                "written_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "families": baseline,
            }, indent=2) + "\n")
            print(f"    wrote {path.relative_to(REPO)}")
        else:
            if not path.is_file():
                failures.append(f"{name}: no stored baseline inventory at {path}; run this script "
                                f"with --refresh")
                continue
            baseline = json.loads(path.read_text())["families"]

        summarise(name, baseline, mine)
        found = compare(name, baseline, mine)
        for line in found:
            print(f"    FAIL {line}")
        failures.extend(found)
        collected.append({"example": name, "baseline_config_sha256": config_hash,
                          "baseline": baseline, "new": mine, "failures": found})

    if arguments.json is not None:
        arguments.json.parent.mkdir(parents=True, exist_ok=True)
        arguments.json.write_text(json.dumps(collected, indent=2) + "\n")
        print(f"wrote {arguments.json}")

    print()
    if failures:
        print(f"column coverage: FAIL — {len(failures)} finding(s)")
        return 1
    print("column coverage: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
