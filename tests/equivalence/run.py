#!/usr/bin/env python3
"""Statistical equivalence between the baseline and this implementation.

This is the single entry point for the validation strategy chosen in
docs/decisions/0006-validation-strategy.md: bit-exact reproduction of the baseline is not a
requirement, so equivalence means running both implementations over many seeds and comparing the
distribution of every output variable, per year, per scenario and per sex.

    tests/equivalence/run.py --example HLM_France --seeds 20

What it does, for each example and each seed:

  1. Writes a derived config for each implementation into the working directory. The baseline gets
     the upstream v1 config it was written for; this build gets the converted v2 config from
     examples/. Both get the same seed, the same output folder, absolute input paths and the same
     active intervention, so the only difference is the implementation.
  2. Runs both, and reduces each result file to one value per (scenario, year, sex, variable) by
     taking the count-weighted mean over the age bands — the population figure the variable is
     reporting.
  3. Across the seeds, computes the mean, standard deviation and 5th, 50th and 95th percentiles of
     each of those series, for each implementation, and compares them.

The comparison is a hypothesis test, not a tolerance on a single number: two Monte Carlo
simulations with different random streams cannot agree exactly, and the question is whether they
agree to within what that noise allows. See docs/equivalence.md for the thresholds and why they
are what they are.

The baseline's reduced output is cached under tests/equivalence/reference/, keyed by the hash of
the config that produced it, so a later run compares against the same numbers without needing the
baseline binary.
"""

from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
UPSTREAM_EXAMPLES = REPO.parent / "hgps_main_examples"
REFERENCE_DIR = HERE / "reference"

# The statistics compared, and the asymptotic standard error of each as a multiple of
# sigma/sqrt(n) for a normal sample. For a quantile q the standard error is
# sqrt(q(1-q)/n) / phi(z_q); the median gives 1.2533 and the 5th and 95th percentiles 2.1133.
# docs/equivalence.md derives these.
STATISTICS = {
    "mean": 1.0,
    "p50": 1.2533,
    "p5": 2.1133,
    "p95": 2.1133,
}

# How many standard errors of the difference are allowed. A single comparison at 3 sigma would
# fail about eleven times by chance over the ~4,000 comparisons one example produces, so the
# threshold is set for the whole family: Bonferroni at alpha = 0.05 over 5,000 comparisons needs
# z = 4.4. docs/equivalence.md.
SIGMA_LIMIT = 4.5

# No comparison can be tighter than the precision of the numbers being compared. The baseline
# writes its CSV with six significant digits, so each of its band figures carries a relative
# rounding error of up to 4e-6, and the difference of two such figures up to 8e-6. This floor is
# added to every allowance, and for a variable that is constant across seeds it *is* the
# allowance — which turns that case into "equal to the precision the baseline prints".
PRINTED_PRECISION_FLOOR = 1e-5

# Variables whose value is meaningless in the first simulated year, so the year is skipped for
# them rather than compared. Nothing else is excluded.
FIRST_YEAR_UNDEFINED = ("deaths", "emigrations", "incidence_", "mean_yll", "std_yll", "mean_yld",
                        "std_yld", "mean_daly", "std_daly")


# --- running the two implementations -----------------------------------------------------------


def sha256_of(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def absolutise(document: dict, base: Path) -> None:
    """Rewrites the config's relative input paths as absolute ones.

    The derived configs live in the working directory rather than beside the files they name, so
    every input path has to be resolved before it moves. Only inputs: where results go is set
    separately, below.
    """

    def fix(parent, key):
        if isinstance(parent, dict) and isinstance(parent.get(key), str):
            value = parent[key]
            if not value.startswith("${") and not Path(value).is_absolute():
                parent[key] = str((base / value).resolve())

    fix(document.get("inputs", {}).get("dataset", {}), "name")

    models = document.get("modelling", {}).get("risk_factor_models", {})
    for key in list(models):
        fix(models, key)

    adjustments = document.get("modelling", {}).get("baseline_adjustments", {})
    for key in list(adjustments.get("file_names", {})):
        fix(adjustments["file_names"], key)
    for stratum in adjustments.get("income_stratum_factors_mean", {}).get("strata", []):
        for key in list(stratum):
            fix(stratum, key)

    two_stage = document.get("project_requirements", {}).get("two_stage", {})
    fix(two_stage, "logistic_file")


def derive_config(source: Path, seed: int, output_folder: Path, intervention: str | None,
                  stop_time: int | None, is_baseline: bool) -> dict:
    document = json.loads(source.read_text())

    # The baseline reads a seed array; config v2 requires a scalar.
    document["running"]["seed"] = [seed] if is_baseline else seed

    if stop_time is not None:
        document["running"]["stop_time"] = stop_time

    if intervention is not None:
        document["running"]["interventions"]["active_type_id"] = intervention

    document["output"]["folder"] = str(output_folder)
    # The baseline ignores output.file_name unless it contains a token (audit finding B-08), so
    # the result file is found by looking rather than by name.
    document["output"]["file_name"] = "result_{TIMESTAMP}.json"

    absolutise(document, source.parent)
    return document


def find_result_csv(folder: Path) -> Path:
    """The main result CSV: the one without an income-stratum suffix."""
    candidates = [p for p in folder.glob("*.csv")
                  if not any(p.stem.endswith(s) for s in ("_LowIncome", "_MiddleIncome",
                                                          "_HighIncome", "_Quintile1",
                                                          "_Quintile2", "_Quintile3",
                                                          "_Quintile4", "_Quintile5"))]
    if len(candidates) != 1:
        raise RuntimeError(f"expected one result CSV in {folder}, found {len(candidates)}: "
                           f"{[p.name for p in candidates]}")
    return candidates[0]


def run(binary: Path, config: Path, extra: list[str], log: Path) -> float:
    started = time.monotonic()
    with log.open("w") as stream:
        completed = subprocess.run([str(binary), "--config", str(config), *extra],
                                   stdout=stream, stderr=subprocess.STDOUT, check=False)
    elapsed = time.monotonic() - started
    if completed.returncode != 0:
        tail = "".join(log.read_text(errors="replace").splitlines(keepends=True)[-25:])
        raise RuntimeError(f"{binary.name} exited {completed.returncode}\n{tail}")
    return elapsed


# --- reducing a result file --------------------------------------------------------------------


KEY_COLUMNS = ("source", "run", "time", "gender_name", "index_id", "count")


def reduce_result(path: Path) -> dict[tuple[str, int, str, str], float]:
    """One value per (scenario, year, sex, variable), as the count-weighted mean over ages.

    Every variable in the file is a per-age-band figure. `count`, `deaths` and `emigrations` are
    counts, so the population figure is their sum; everything else is a mean or a proportion over
    the band's members, so the population figure is the count-weighted mean. Reducing this way is
    what makes the two implementations comparable at all: their age bands hold different people.
    """
    totals: dict[tuple[str, int, str, str], float] = {}
    weights: dict[tuple[str, int, str, str], float] = {}

    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        variables = [name for name in reader.fieldnames or [] if name not in KEY_COLUMNS]
        summed = {"count", "deaths", "emigrations"}

        for row in reader:
            source = row["source"].lower()
            year = int(row["time"])
            sex = row["gender_name"].lower()
            count = float(row["count"])

            for variable in ("count", "deaths", "emigrations"):
                if variable in row and row[variable] != "":
                    key = (source, year, sex, variable)
                    totals[key] = totals.get(key, 0.0) + float(row[variable])
                    weights[key] = 1.0

            if count <= 0.0:
                continue

            for variable in variables:
                if variable in summed:
                    continue
                text = row[variable]
                if text == "":
                    continue
                key = (source, year, sex, variable)
                totals[key] = totals.get(key, 0.0) + count * float(text)
                weights[key] = weights.get(key, 0.0) + count

    return {key: totals[key] / weights[key] for key in totals}


def reduced_to_rows(reduced: dict[tuple[str, int, str, str], float], seed: int) -> list[list]:
    return [[seed, key[0], key[1], key[2], key[3], repr(value)]
            for key, value in sorted(reduced.items())]


REFERENCE_HEADER = ["seed", "scenario", "year", "sex", "variable", "value"]


def write_reference(path: Path, rows: list[list]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with gzip.open(path, "wt", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(REFERENCE_HEADER)
        writer.writerows(rows)


def read_reference(path: Path) -> dict[int, dict[tuple[str, int, str, str], float]]:
    by_seed: dict[int, dict[tuple[str, int, str, str], float]] = {}
    with gzip.open(path, "rt", newline="") as stream:
        for row in csv.DictReader(stream):
            seed = int(row["seed"])
            key = (row["scenario"], int(row["year"]), row["sex"], row["variable"])
            by_seed.setdefault(seed, {})[key] = float(row["value"])
    return by_seed


# --- comparing ---------------------------------------------------------------------------------


def quantile(values: list[float], q: float) -> float:
    """The type-7 quantile, so a reader can reproduce it in R or numpy without surprises."""
    if not values:
        return math.nan
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * q
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[int(position)]
    return ordered[lower] + (position - lower) * (ordered[upper] - ordered[lower])


@dataclass
class Summary:
    mean: float
    sd: float
    p5: float
    p50: float
    p95: float
    n: int

    @staticmethod
    def of(values: list[float]) -> "Summary":
        return Summary(mean=statistics.fmean(values),
                       sd=statistics.stdev(values) if len(values) > 1 else 0.0,
                       p5=quantile(values, 0.05),
                       p50=quantile(values, 0.50),
                       p95=quantile(values, 0.95),
                       n=len(values))


@dataclass
class Comparison:
    key: tuple[str, int, str, str]
    statistic: str
    baseline: float
    new: float
    allowed: float
    difference: float

    @property
    def passed(self) -> bool:
        return abs(self.difference) <= self.allowed

    @property
    def ratio_of_allowed(self) -> float:
        return abs(self.difference) / self.allowed if self.allowed > 0 else math.inf


@dataclass
class Outcome:
    example: str
    seeds: list[int]
    comparisons: list[Comparison] = field(default_factory=list)
    skipped: list[str] = field(default_factory=list)
    missing: list[str] = field(default_factory=list)
    timings: dict[str, float] = field(default_factory=dict)
    config_hashes: dict[str, str] = field(default_factory=dict)


def is_first_year_undefined(variable: str) -> bool:
    return any(variable.startswith(prefix) or variable == prefix
               for prefix in FIRST_YEAR_UNDEFINED)


def compare(baseline: dict[int, dict], new: dict[int, dict], seeds: list[int],
            outcome: Outcome) -> None:
    baseline_keys = {key for seed in seeds for key in baseline[seed]}
    new_keys = {key for seed in seeds for key in new[seed]}

    for key in sorted(baseline_keys - new_keys):
        outcome.missing.append(f"only the baseline reports {key}")
    for key in sorted(new_keys - baseline_keys):
        outcome.missing.append(f"only this build reports {key}")

    first_year = min(year for _, year, _, _ in baseline_keys) if baseline_keys else 0

    for key in sorted(baseline_keys & new_keys):
        scenario, year, sex, variable = key

        if year == first_year and is_first_year_undefined(variable):
            outcome.skipped.append(f"{key}: not defined in the first simulated year")
            continue

        base_values = [baseline[seed][key] for seed in seeds if key in baseline[seed]]
        new_values = [new[seed][key] for seed in seeds if key in new[seed]]
        if len(base_values) != len(seeds) or len(new_values) != len(seeds):
            outcome.skipped.append(f"{key}: not present for every seed")
            continue

        base = Summary.of(base_values)
        mine = Summary.of(new_values)

        n = len(seeds)
        pooled_variance = base.sd ** 2 + mine.sd ** 2

        # The floor is what the baseline's printed precision allows, and it is what makes a
        # variable that is constant across seeds — an incidence that never fires, a calibrated
        # band mean that does not depend on the seed — compare as equality rather than as a test
        # against zero noise.
        scale = max(abs(base.mean), abs(mine.mean), base.sd, mine.sd, 1e-12)
        floor = PRINTED_PRECISION_FLOOR * scale

        for name, se_factor in STATISTICS.items():
            allowed = SIGMA_LIMIT * se_factor * math.sqrt(pooled_variance / n) + floor
            outcome.comparisons.append(
                Comparison(key, name, getattr(base, name), getattr(mine, name),
                           allowed=allowed,
                           difference=getattr(mine, name) - getattr(base, name)))

        # The standard deviations. The standard error of a sample standard deviation is
        # s / sqrt(2(n-1)), so this is the same k-sigma rule as the others; writing it as a
        # difference rather than a ratio is what lets a baseline standard deviation of exactly
        # zero be compared at all.
        allowed = SIGMA_LIMIT * math.sqrt(pooled_variance / (2.0 * (n - 1))) + floor
        outcome.comparisons.append(Comparison(key, "sd", base.sd, mine.sd, allowed=allowed,
                                              difference=mine.sd - base.sd))


# --- the example definitions --------------------------------------------------------------------


@dataclass
class Example:
    name: str
    # The upstream config the baseline was written for.
    baseline_config: Path
    # The converted config v2 in examples/.
    new_config: Path
    # Which intervention to activate in both, so the intervention scenario is compared too.
    intervention: str | None


def examples() -> dict[str, Example]:
    return {
        "HLM_France": Example(
            name="HLM_France",
            baseline_config=UPSTREAM_EXAMPLES / "HLM_France" / "config.json",
            new_config=REPO / "examples" / "HLM_France" / "config.json",
            # HLM_France ships active_type_id null, which would compare one scenario. `simple`
            # lowers BMI by 1.0 from 2022, so activating it in both compares the intervention
            # path as well — the only intervention this build implements.
            intervention="simple",
        ),
        "KevinHall_FINCH": Example(
            name="KevinHall_FINCH",
            baseline_config=UPSTREAM_EXAMPLES / "KevinHall_FINCH" / "new_config.json",
            new_config=REPO / "examples" / "KevinHall_FINCH" / "config.json",
            intervention="simple",
        ),
    }


# --- the report ---------------------------------------------------------------------------------


def variable_of(comparison: Comparison) -> str:
    return comparison.key[3]


def report(outcome: Outcome, verbose: bool) -> bool:
    failures = [c for c in outcome.comparisons if not c.passed]
    total = len(outcome.comparisons)

    print()
    print(f"=== {outcome.example}: {len(outcome.seeds)} seeds, {total} comparisons")
    for label, seconds in sorted(outcome.timings.items()):
        print(f"    {label}: {seconds:.1f}s")
    for label, digest in sorted(outcome.config_hashes.items()):
        print(f"    {label} config sha256: {digest}")

    if outcome.missing:
        print(f"    {len(outcome.missing)} series reported by only one implementation:")
        for line in outcome.missing[:20]:
            print(f"      {line}")

    if outcome.skipped and verbose:
        print(f"    {len(outcome.skipped)} comparisons skipped")

    if not failures:
        print(f"    all {total} comparisons within tolerance")
    else:
        by_variable: dict[str, list[Comparison]] = {}
        for failure in failures:
            by_variable.setdefault(variable_of(failure), []).append(failure)

        print(f"    {len(failures)} of {total} comparisons out of tolerance, "
              f"in {len(by_variable)} variable(s):")
        for variable in sorted(by_variable, key=lambda v: -len(by_variable[v])):
            group = by_variable[variable]
            worst = max(group, key=lambda c: c.ratio_of_allowed)
            years = sorted({c.key[1] for c in group})
            print(f"      {variable}: {len(group)} comparison(s), years {years[0]}-{years[-1]}, "
                  f"worst {worst.statistic} baseline={worst.baseline:.6g} "
                  f"new={worst.new:.6g} = {worst.ratio_of_allowed:.1f}x the allowance")

    # The largest excursions that still passed, so a systematic shift hiding inside a wide
    # allowance is visible rather than silent.
    if verbose:
        passed = sorted((c for c in outcome.comparisons if c.passed),
                        key=lambda c: -c.ratio_of_allowed)[:10]
        print("    largest differences that passed:")
        for comparison in passed:
            print(f"      {comparison.key} {comparison.statistic}: "
                  f"{comparison.ratio_of_allowed:.2f}x the allowance")

    return not failures and not outcome.missing


def as_json(outcome: Outcome) -> dict:
    """The outcome in full, keyed so a reader can find any single comparison again."""
    by_group: dict[str, dict] = {}
    for comparison in outcome.comparisons:
        group = by_group.setdefault(f"{variable_of(comparison)}|{comparison.statistic}",
                                    {"variable": variable_of(comparison),
                                     "statistic": comparison.statistic,
                                     "compared": 0, "failed": 0, "worst": None})
        group["compared"] += 1
        if not comparison.passed:
            group["failed"] += 1
        if group["worst"] is None or comparison.ratio_of_allowed > group["worst"]["ratio"]:
            group["worst"] = {
                "scenario": comparison.key[0], "year": comparison.key[1],
                "sex": comparison.key[2], "baseline": comparison.baseline,
                "new": comparison.new, "allowed": comparison.allowed,
                "difference": comparison.difference,
                "ratio": comparison.ratio_of_allowed,
            }

    return {
        "example": outcome.example,
        "seeds": outcome.seeds,
        "config_hashes": outcome.config_hashes,
        "timings_seconds": outcome.timings,
        "comparisons": len(outcome.comparisons),
        "failures": sum(1 for c in outcome.comparisons if not c.passed),
        "skipped": len(outcome.skipped),
        "series_reported_by_one_side_only": outcome.missing,
        "sigma_limit": SIGMA_LIMIT,
        "printed_precision_floor": PRINTED_PRECISION_FLOOR,
        "groups": sorted(by_group.values(),
                         key=lambda g: (-g["failed"], -g["worst"]["ratio"])),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--example", action="append", default=None,
                        help="which example to compare (repeatable); default: every one that "
                             "this build can run")
    parser.add_argument("--seeds", type=int, default=20,
                        help="how many seeds to run (default 20)")
    parser.add_argument("--first-seed", type=int, default=1,
                        help="the first seed; seeds are first-seed .. first-seed+seeds-1, so a "
                             "run is reproducible from these two numbers alone")
    parser.add_argument("--stop-time", type=int, default=None,
                        help="override running.stop_time in both, for a quicker check")
    parser.add_argument("--baseline", type=Path,
                        default=Path("/tmp/hgps-build/baseline-release/src/HealthGPS.Console/"
                                     "HealthGPS.Console"),
                        help="the baseline binary; see docs/build-notes.md")
    parser.add_argument("--new", type=Path, default=REPO / "out/build/release/src/healthgps",
                        help="this build's binary")
    parser.add_argument("--workdir", type=Path, default=None,
                        help="where to put derived configs and result files "
                             "(default: a temporary directory that is kept)")
    parser.add_argument("--use-reference", action="store_true",
                        help="do not run the baseline; compare against the stored reference for "
                             "this config and these seeds")
    parser.add_argument("--refresh-reference", action="store_true",
                        help="run the baseline and overwrite the stored reference")
    parser.add_argument("--reference-dir", type=Path, default=REFERENCE_DIR,
                        help="where the baseline's reduced output is cached (default: "
                             "tests/equivalence/reference). A run with many more seeds than the "
                             "checked-in reference should point this somewhere outside the "
                             "repository rather than commit tens of megabytes.")
    parser.add_argument("--json", type=Path, default=None,
                        help="also write the full outcome as JSON, so docs/equivalence.md can "
                             "quote exact numbers rather than round ones")
    parser.add_argument("--verbose", action="store_true")
    arguments = parser.parse_args()

    available = examples()
    names = arguments.example or list(available)
    for name in names:
        if name not in available:
            parser.error(f"unknown example '{name}'; known: {', '.join(available)}")

    seeds = list(range(arguments.first_seed, arguments.first_seed + arguments.seeds))
    if len(seeds) < 3:
        parser.error("at least 3 seeds are needed for a distribution to mean anything")

    workdir = arguments.workdir or (Path(os.environ.get("TMPDIR", "/tmp")) / "hgps-equivalence")
    workdir.mkdir(parents=True, exist_ok=True)

    all_passed = True
    collected: list[Outcome] = []

    for name in names:
        example = available[name]
        outcome = Outcome(example=name, seeds=seeds)

        if not example.new_config.is_file():
            print(f"=== {name}: SKIPPED, {example.new_config} does not exist")
            continue

        # The config hash is over the derived config with the seed removed, so it identifies the
        # scenario rather than one run of it, and a stored reference can be matched to it.
        def hash_of(source: Path, is_baseline: bool) -> str:
            document = derive_config(source, 0, Path("/results"), example.intervention,
                                     arguments.stop_time, is_baseline)
            document["running"].pop("seed", None)
            return sha256_of(json.dumps(document, sort_keys=True))

        outcome.config_hashes["baseline"] = hash_of(example.baseline_config, True)
        outcome.config_hashes["new"] = hash_of(example.new_config, False)

        reference_path = (arguments.reference_dir / name /
                          f"{outcome.config_hashes['baseline']}.csv.gz")
        manifest_path = reference_path.with_suffix("").with_suffix(".json")

        baseline_reduced: dict[int, dict] = {}
        new_reduced: dict[int, dict] = {}

        use_reference = arguments.use_reference and reference_path.is_file()
        if arguments.use_reference and not reference_path.is_file():
            print(f"=== {name}: no stored reference at {reference_path}; running the baseline")
            use_reference = False

        if use_reference:
            stored = read_reference(reference_path)
            missing_seeds = [seed for seed in seeds if seed not in stored]
            if missing_seeds:
                print(f"=== {name}: the stored reference has no seed(s) {missing_seeds}; "
                      f"running the baseline")
                use_reference = False
            else:
                baseline_reduced = {seed: stored[seed] for seed in seeds}
                outcome.timings["baseline (stored reference)"] = 0.0

        for seed in seeds:
            for label, binary, source, is_baseline, target in (
                    ("baseline", arguments.baseline, example.baseline_config, True,
                     baseline_reduced),
                    ("new", arguments.new, example.new_config, False, new_reduced)):
                if label == "baseline" and use_reference:
                    continue

                folder = workdir / name / label / f"seed-{seed}"
                if folder.exists():
                    shutil.rmtree(folder)
                folder.mkdir(parents=True)

                document = derive_config(source, seed, folder, example.intervention,
                                         arguments.stop_time, is_baseline)
                config_path = folder.parent / f"config-seed-{seed}.json"
                config_path.write_text(json.dumps(document, indent=1))

                extra = ["-T", "1"] if label == "baseline" else ["--threads", "1"]
                elapsed = run(binary, config_path, extra, folder.parent / f"log-seed-{seed}.txt")
                outcome.timings[label] = outcome.timings.get(label, 0.0) + elapsed

                target[seed] = reduce_result(find_result_csv(folder))
            print(f"    {name} seed {seed}: done", flush=True)

        if not use_reference and (arguments.refresh_reference or not reference_path.is_file()):
            rows = [row for seed in seeds for row in reduced_to_rows(baseline_reduced[seed], seed)]
            write_reference(reference_path, rows)
            manifest_path.write_text(json.dumps({
                "example": name,
                "seeds": seeds,
                "baseline_config_sha256": outcome.config_hashes["baseline"],
                "new_config_sha256": outcome.config_hashes["new"],
                "intervention": example.intervention,
                "stop_time_override": arguments.stop_time,
                "baseline_binary": str(arguments.baseline),
                "reduction": "count-weighted mean over age bands; counts summed",
                "written_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            }, indent=2) + "\n")
            try:
                shown = reference_path.relative_to(REPO)
            except ValueError:
                shown = reference_path
            print(f"    wrote reference {shown}")

        compare(baseline_reduced, new_reduced, seeds, outcome)
        all_passed &= report(outcome, arguments.verbose)
        collected.append(outcome)

    if arguments.json is not None:
        arguments.json.parent.mkdir(parents=True, exist_ok=True)
        arguments.json.write_text(json.dumps(
            [as_json(outcome) for outcome in collected], indent=2) + "\n")
        print(f"wrote {arguments.json}")

    print()
    print("equivalence: PASS" if all_passed else "equivalence: FAIL")
    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
