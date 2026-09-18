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
     reporting — or, for a head count, the sum over them (SUMMED_VARIABLES). Age bands that either
     implementation ever empties are left out of that reduction, on both sides: see "the
     emptying-band exclusion" below.
  3. Across the seeds, computes the mean, standard deviation and 5th, 50th and 95th percentiles of
     each of those series, for each implementation, and compares them.

The comparison is a hypothesis test, not a tolerance on a single number: two Monte Carlo
simulations with different random streams cannot agree exactly, and the question is whether they
agree to within what that noise allows. See docs/equivalence.md for the thresholds and why they
are what they are.

The baseline's reduced output is cached under tests/equivalence/reference/, keyed by the hash of
the config that produced it, so a later run compares against the same numbers without needing the
baseline binary.

The emptying-band exclusion
---------------------------

Immigration into an (age, sex) band clones somebody already in it. When the band is empty there is
nobody to clone, and both implementations skip it and fall short of the demographic projection the
cohort is otherwise pinned to — silently, and on different seeds, because which bands empty depends
on the draws. That is a defect in the baseline, recorded as B-21 in docs/deviations.md; this
implementation reproduces the rule and so inherits it.

The measured consequence, over three seeds of HLM_France: in the baseline scenario 197 of the
baseline's band counts and 220 of this build's fall short of the projection, and **every single one
of them is a band whose head count is exactly zero**. No band ever exceeds the projection. So the
bands where the two implementations can legitimately disagree are exactly the bands that empty.

The harness therefore excludes, from the reduction on **both** sides and for **every** seed, each
(scenario, year, sex, age) band that either implementation empties in any seed. The set is derived
from the runs rather than declared, is applied identically to both, and is recorded in the
reference manifest so a run against the stored reference uses the same one. For HLM_France at
three seeds it is 562 of 16,564 bands — 0.124% of the head count, all at ages 91 and above — and
with it the two implementations' baseline-scenario cohort totals agree **exactly**, in every year,
for both sexes, at every seed.

If a run finds an empty band outside the recorded set, the stored reduction is no longer the right
one and the harness says so and fails, rather than quietly widening the exclusion.

Compatibility flags, and the deviation-impact section
-----------------------------------------------------

This build fixes defects in the baseline, and 22 of those fixes change the numbers
(docs/deviations.md). Each one that does is switchable: `--baseline-compat NAME` makes the engine
reproduce the baseline's behaviour exactly, bug and all (ADR 0041).

**Every comparison here runs this build with `--baseline-compat all`.** So the comparison tests
everything *except* the deliberate deviations, and an out-of-tolerance cell means something is
wrong rather than something is different on purpose. Before this, a known deviation and an unknown
defect looked identical in the output and were told apart by a person reading the numbers — which
worked once, for one deviation, and does not survive a second.

The harness then runs this build **once more with the flags off** and reports the difference
between the two as a separate **deviation impact** section: per variable, per year, per scenario,
the size and the direction of what the fixes are worth. That section is *reported, not graded*. A
deviation has no right size, so a pass/fail threshold on it would be a number nobody could justify.

The extra pass is skipped when it would measure nothing. `--deviation-impact auto`, the default,
runs the first seed both ways and stops if the two results are byte-identical — which is the answer
for every example whose active intervention no recorded deviation touches, and is itself worth
printing. `always` and `never` override the probe.

Comparing one intervention at a time
------------------------------------

`--intervention NAME` activates a different policy in both implementations. Each choice is a
different scenario and therefore a different stored reference, because the reference is keyed by
the hash of the config that produced it, so the five extra policies do not disturb the reference
for `simple`.

    tests/equivalence/run.py --example HLM_France --seeds 20 --intervention fiscal

`KevinHall_FINCH` ships only `simple`, and its impact list is empty, so the definitions for the
other five come from `tests/equivalence/interventions/KevinHall_FINCH.json`. See
`intervention_overlay` for exactly what is in that file and what was changed.
"""

from __future__ import annotations

import argparse
import collections
import csv
import gzip
import hashlib
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field, replace
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

# A series is LATTICE-VALUED when either of the two conditions below holds. Its across-seed
# distribution is then a set of counts on a small set of values rather than anything like a
# normal, and no *quantile* of it can be compared numerically at all:
#
#   * a quantile of a lattice-valued sample is itself a lattice point, so the comparison's
#     resolution is one whole lattice step;
#   * the normal-theory allowance shrinks as 1/sqrt(n) while the lattice step does not, so the
#     comparison gets WORSE with more seeds — the opposite of what a test should do.
#
# The second point is not hypothetical. `incidence_esophaguscancer` at (intervention, 2025, male)
# is 0, one case or two cases, and the two implementations' counts over sixty seeds were
# {0: 26, 1: 28, 2: 6} and {0: 33, 1: 21, 2: 6} — distributions Fisher's exact test cannot tell
# apart, p = 0.27. But the zero share crosses one half between them, so their medians differ by a
# whole lattice step, and at sixty seeds the allowance is smaller than one step. The same
# comparison passed at twenty seeds, where the allowance was larger than a step. docs/equivalence.md
# has the derivation.
#
# For such a series the mean is compared as usual — it is not a lattice point and its allowance
# does shrink correctly — and the standard deviation and the three quantiles, all of which are
# functions of the same counts, are replaced by one exact test of those counts.

# **The detector looks at the numerator, not at the reduced value.** That is the correction this
# run made, and it is worth stating why rather than only what.
#
# The reduction turns a per-band figure into one population figure per (scenario, year, sex): counts
# are summed, and everything else is the count-weighted mean over the bands. So a disease rate comes
# out as `total cases / total head count`. The cases are a small integer; the head count differs
# from seed to seed. Dividing one by the other smears the lattice — a series that is a handful of
# case counts in disguise presents dozens of distinct *rates*, and both rules below, applied to the
# rate, miss it.
#
# That is not hypothetical either. The `HLM_India` comparison at 60 seeds produced four such series
# with 43 to 79 distinct values and a modal share of 0.12 to 0.40 — under both rules — while a third
# of their seeds were exactly zero. Six comparisons failed at 1.02x to 1.09x of their allowance, in
# both the `simple` and the `food_labelling` runs, which is what says they belong to the comparison
# and not to either implementation. docs/equivalence.md has the table.
#
# The numerator is recoverable without storing anything new: the reduction already carries `count`
# as a summed variable, and for a count-weighted variable the numerator is `value * count` for the
# same (scenario, year, sex) and seed. `numerator_series` does that, and `reduce_result` checks the
# identity it rests on, so a file where it did not hold would fail rather than be classified from a
# wrong number.
#
# **The bucketing is unchanged**: the numerator is bucketed at the baseline's printed precision, the
# same rule the reduced value got. Note what that means — if the head count were the same in every
# seed, multiplying both the values and the scale by it would leave every bucket exactly where it
# was, so this change does nothing at all except where the denominator moves. Which is the whole of
# the defect it fixes.

# The variables the reduction sums rather than count-weights. Their reduced value IS the numerator.
#
# **The four weight categories belong here, and did not until this run.** `normal_weight`,
# `over_weight`, `obese_weight` and `above_weight` are head counts: the analysis module increments
# one of them per person per band, in both implementations, and neither divides them by anything —
# `tests/sim/simulation_test.cpp` pins `normal + over + obese == count` for every row. Reducing a
# per-band head count by the count-weighted mean gave a population figure of 15.3 for `normal_weight`
# on `HLM_France` at (baseline, 2030, male), where the population figure is about 1,550: the average
# band's count rather than the population's total, which is a number with no meaning. The *shape* of
# the series still followed the underlying quantity, which is why nothing looked obviously wrong and
# why no comparison was wrong — both implementations were reduced identically — but the level was.
#
# Moving them here changes every stored reference, because a reference holds reduced values, so all
# four were regenerated against the baseline binary when this changed (docs/equivalence.md).
SUMMED_VARIABLES = {"count", "deaths", "emigrations",
                    "normal_weight", "over_weight", "obese_weight", "above_weight"}

# (1) At most this many distinct values, at the baseline's printed precision, in the two
#     implementations' samples pooled. A continuous quantity gives one distinct value per seed, so
#     this cannot catch one: six is a quarter of the smallest seed count the harness accepts.
LATTICE_MAX_DISTINCT_VALUES = 6

# (2) Or one value covering more than this share of one implementation's seeds. A series can have
#     many distinct values and still be a point mass with rare jumps — and when one value covers
#     more than half the seeds, the median IS that value, so it is a step function too.
DEGENERATE_MODAL_SHARE = 0.5

# The family-wide significance the distribution test uses, matching the 4.5 sigma the other
# tests use: a Bonferroni correction at alpha = 0.05 over the ~5,000 independent series.
DISTRIBUTION_TEST_ALPHA = 0.05 / 5000

# Variables the baseline does not actually compute, keyed to the deviation that records why.
#
# A column the baseline emits but never fills is not a disagreement about a number: there is no
# number on one side. Comparing it would fail for ever and say nothing. Excluding it is safe only
# while the premise holds, so the harness *checks* the premise — the baseline's series must be
# identically zero — and compares the variable normally if it is not. A baseline that starts
# filling the column therefore stops being excluded, and says so.
# Variables the baseline emits as a column and never fills. They are excluded from the comparison
# only while the baseline's series is identically zero, so the rule disarms itself if upstream ever
# starts computing one — and only while *this* build's series is not, because two identically zero
# series mean the deviation is not being applied here either, which is a regression the rule would
# otherwise hide behind its own message. See `compare`.
#
# These stay exclusions rather than becoming compatibility flags (ADR 0041). The distinction is
# whether both sides have a number that means something: B-24 is two implementations computing a
# real value and disagreeing on purpose, which is worth measuring; this is one implementation
# emitting a placeholder, and "compare zero against zero" is not a stronger test than not
# comparing. docs/equivalence-method.md records the review.
BASELINE_DOES_NOT_COMPUTE = {
    "std_income": ("B-22", "the baseline skips 'income' in the loop that accumulates squared "
                           "deviations, on the ground that the mapping loop handles it, and the "
                           "mapping loop skips it too; the column is always exactly zero"),
}

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

    # `data.source` is a URL in every shipped example and a relative directory in the synthetic
    # fixture pack. Only the second needs absolutising, and telling them apart matters: `fix` sees a
    # URL as a relative path — it has no leading slash — and would rewrite
    # `https://…/data.zip` into `<config dir>/https:/…/data.zip`, which changes the derived config,
    # changes its hash, and orphans the stored reference keyed by it. (It does not even fail loudly:
    # the source still ends in `.zip`, so the engine looks in its content-addressed cache first and
    # finds the already-extracted pack.)
    data = document.get("data", {})
    if isinstance(data.get("source"), str) and not data["source"].startswith(("http://", "https://")):
        fix(data, "source")


INTERVENTION_OVERLAYS = Path(__file__).resolve().parent / "interventions"


def intervention_overlay(example_name: str) -> dict:
    """The intervention definitions an example does not ship but the comparison needs.

    `KevinHall_FINCH` declares exactly one intervention, `simple`, and its impact list is
    **empty**, so activating it compares the baseline scenario against a copy of itself as far as
    the `interventions` block is concerned. (The two scenarios do differ, but through
    `policy_start_year` and the S1 policy-effect coefficients, which are the static linear model's
    own mechanism and nothing to do with this block.)

    `tests/equivalence/interventions/KevinHall_FINCH.json` is HLM_France's own five definitions,
    verbatim from the upstream example, with two substitutions and nothing else:

      * the active period becomes FINCH's own — 2025 onwards, which is what its `simple`
        declares — because HLM_France's runs to 2050 and the FINCH horizon ends in 2032;
      * the risk factor `Energy` becomes `EnergyIntake`, which is FINCH's name for it.

    What running them shows is narrower than it looks, and worth knowing before reading the result.
    In the whole baseline, `Scenario::apply` has ONE call site —
    `dynamic_hierarchical_linear_model.cpp:110` — so an intervention scenario reaches the HLM
    surface and nothing else, and on the FINCH surface all six are inert. This build has the same
    single call site. Running FINCH with `marketing` active gives output byte-identical to running
    it with `simple` active, in both implementations.

    So these runs check that both implementations agree the policies are inert here, for each
    policy separately — which is what would catch an implementation that wired `apply` into a
    model the baseline leaves alone. The policies' own rules are compared on HLM_France, where the
    definitions are upstream's own and the model does consult them.
    """
    path = INTERVENTION_OVERLAYS / f"{example_name}.json"
    return json.loads(path.read_text()) if path.is_file() else {}


def derive_config(source: Path, seed: int, output_folder: Path, intervention: str | None,
                  stop_time: int | None, is_baseline: bool, overlay: dict | None = None,
                  size_fraction: float | None = None, absolute: bool = True) -> dict:
    document = json.loads(source.read_text())

    # The baseline reads a seed array; config v2 requires a scalar.
    document["running"]["seed"] = [seed] if is_baseline else seed

    if stop_time is not None:
        document["running"]["stop_time"] = stop_time

    # `inputs.settings.size_fraction` is the share of the real population the cohort samples, and it is
    # the only knob that makes a country-scale example comparable in an afternoon rather than a week.
    # It goes into BOTH implementations' configs identically, so the comparison is exactly as valid a
    # test of the code as it is at full scale; what it is not is a test at full scale.
    # docs/equivalence.md says so where the HLM_India result is reported.
    if size_fraction is not None:
        document["inputs"]["settings"]["size_fraction"] = size_fraction

    if intervention is not None:
        types = document["running"]["interventions"].setdefault("types", {})
        if intervention not in types and overlay and intervention in overlay:
            types[intervention] = overlay[intervention]
        document["running"]["interventions"]["active_type_id"] = intervention

    document["output"]["folder"] = str(output_folder)
    # The baseline ignores output.file_name unless it contains a token (audit finding B-08), so
    # the result file is found by looking rather than by name.
    document["output"]["file_name"] = "result_{TIMESTAMP}.json"

    # `absolute=False` is for the hash that keys a stored reference, and nothing else. The
    # derived config a run actually uses needs absolute paths, because it does not live beside the
    # files it names — but those paths contain the checkout's location, and a reference keyed by
    # them can only ever be found on the machine that wrote it. The first CI run to reach this step
    # said so: it recomputed a different hash, found no reference, and went looking for a baseline
    # binary that CI deliberately does not build. Hashing the document before this step keys a
    # reference by the scenario, which is what it was always meant to identify.
    if absolute:
        absolutise(document, source.parent)
    return document


def stage_example_files(source_config: Path, into: Path) -> None:
    """Puts an upstream example's files next to a derived config, safely.

    The derived config names its model files by absolute path, but a *model* file names its own
    CSVs by a path relative to the config's directory — that is how the baseline resolves them, and
    it is right when the config sits in the example folder, which upstream it does. The derived
    config does not, so the files it needs are put beside it.

    **The source config is copied; everything else is symlinked.** That distinction is the whole
    point of this function. The upstream examples and this repository's converted ones are inputs,
    not scratch, and a symlink is a two-way door: writing `<into>/config.json` in a directory where
    `config.json` is a link does not create a file, it rewrites the example. So

      * the **config is a copy**, because it is the file a caller derives from and the obvious name
        for a derived config is the name of the one it came from;
      * the **data and model inputs are links** — CSVs, model JSONs, ZIPs — because they are read
        and never written, and `HLM_India`'s would be 42 MB a copy.

    `write_derived_config` is the other half, and it is load-bearing rather than belt-and-braces:
    `KevinHall_FINCH` ships *two* configs and the harness derives from `new_config.json`, so
    `config.json` — the name the original accident used — is still a link here. Copying the source
    config alone would not have stopped it on that example; refusing to write through a link does.

    This is a rule in the code rather than a warning in a docstring because the warning was not
    enough. An ad-hoc measurement script named its derived config `config.json`, wrote it into a
    directory staged the old way, and silently rewrote two of the converted examples; the runs then
    still worked, because the mangled `data.source` still ended in `.zip` and the engine found the
    already-extracted pack in its content-addressed cache without looking at the path.
    See docs/decisions/0039-scratch-directories-copy-what-they-may-write.md.

    Nothing here changes the derived config, and so nothing changes its hash or the stored reference
    keyed by it.
    """
    into.mkdir(parents=True, exist_ok=True)
    for entry in sorted(source_config.parent.iterdir()):
        if not entry.is_file():
            continue
        staged = into / entry.name
        if staged.is_symlink() or staged.exists():
            continue
        if entry.resolve() == source_config.resolve():
            shutil.copyfile(entry, staged)
        else:
            staged.symlink_to(entry.resolve())


def write_derived_config(path: Path, document: dict) -> None:
    """Writes a derived config, and never through a symlink.

    Every derived config in this harness goes through here. A symlink at `path` would mean writing
    into whatever it points at — which, in a directory staged by `stage_example_files`, is an
    upstream example file. `stage_example_files` already makes that impossible for a config by
    copying it rather than linking it; this makes it impossible for anything else too, and loudly
    rather than silently.
    """
    if path.is_symlink():
        raise RuntimeError(
            f"refusing to write the derived config {path} through a symlink to "
            f"{os.readlink(path)}: that would rewrite an input file rather than create a new one")
    path.write_text(json.dumps(document, indent=1))


def find_result_csv(folder: Path) -> Path:
    """The main result CSV: the whole-population one.

    A run also writes an income-stratified file per category and, when it is switched on, an
    individual-tracking file. Every one of those is the main file's name plus a suffix, so the
    main one is the file whose stem every other stem begins with — which needs no list of suffixes
    to keep in step with the writer.
    """
    candidates = sorted(folder.glob("*.csv"))
    if not candidates:
        raise RuntimeError(f"no result CSV in {folder}")

    # The derived files are the main name plus a CamelCase suffix — `_LowIncome`, `_Quintile3`,
    # `_IndividualIDTracking`. Matching on the shape of the suffix rather than on a list of them
    # keeps this in step with the writer, and works even when a derived file's own timestamp is a
    # second later than the main one's, which it sometimes is.
    derived = re.compile(r"_[A-Z][A-Za-z0-9]*$")
    whole = [p for p in candidates if not derived.search(p.stem)]

    if len(whole) != 1:
        raise RuntimeError(f"cannot tell which of the CSVs in {folder} is the whole-population "
                           f"one: {[p.name for p in candidates]}")
    return whole[0]


def run(binary: Path, config: Path, extra: list[str], log: Path,
        attempts: int = 1, retries: list[str] | None = None,
        output_folder: Path | None = None) -> float:
    """Runs one binary on one config, and returns how long it took.

    `attempts` above one is for the baseline only, and exists for a measured reason: on the FINCH
    example it exits on a signal about one run in forty-five — 4 of 180 measured runs, three
    `SIGTRAP` and one `SIGABRT` — and the *same* config and seed then succeeds. That is the concurrency defect the audit recorded (B-01, B-02) — two scenario threads
    and a repository populated lazily from inside a parallel loop — and it is not something a
    comparison against it can fix. A retry keeps a twenty-seed run from being lost to it; every
    retry is recorded and reported, so the flake is visible rather than smoothed away.
    """
    for attempt in range(1, attempts + 1):
        # A crashed attempt leaves its part-written result files behind, and the retry would then
        # add a second set beside them.
        if output_folder is not None:
            if output_folder.exists():
                shutil.rmtree(output_folder)
            output_folder.mkdir(parents=True)

        started = time.monotonic()
        with log.open("w") as stream:
            completed = subprocess.run([str(binary), "--config", str(config), *extra],
                                       stdout=stream, stderr=subprocess.STDOUT, check=False)
        elapsed = time.monotonic() - started

        if completed.returncode == 0:
            return elapsed

        tail = "".join(log.read_text(errors="replace").splitlines(keepends=True)[-25:])
        if attempt == attempts:
            raise RuntimeError(f"{binary.name} exited {completed.returncode} on attempt "
                               f"{attempt} of {attempts}\n{tail}")

        note = (f"{binary.name} exited {completed.returncode} on {config.name}; "
                f"retrying (attempt {attempt + 1} of {attempts})")
        print(f"    {note}", flush=True)
        if retries is not None:
            retries.append(note)

    raise RuntimeError("unreachable")


# --- reducing a result file --------------------------------------------------------------------


KEY_COLUMNS = ("source", "run", "time", "gender_name", "index_id", "count")


Band = tuple  # (scenario, year, sex, age)


def empty_bands(path: Path) -> set[Band]:
    """The (scenario, year, sex, age) bands whose head count is zero.

    These are the bands immigration cannot refill, and so the only bands where the two
    implementations disagree about the cohort — see "the emptying-band exclusion" in the module
    docstring and docs/equivalence.md.
    """
    found: set[Band] = set()
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            if float(row["count"]) == 0.0:
                found.add((row["source"].lower(), int(row["time"]), row["gender_name"].lower(),
                           int(row["index_id"])))
    return found


def band_key(scenario: str, year: int, sex: str, age: int) -> Band:
    return (scenario, year, sex, age)


def reduce_result(path: Path, excluded: set[Band] | None = None
                  ) -> dict[tuple[str, int, str, str], float]:
    """One value per (scenario, year, sex, variable), as the count-weighted mean over ages.

    Every variable in the file is a per-age-band figure. `SUMMED_VARIABLES` — `count`, `deaths`,
    `emigrations` and the four weight categories — are head counts, so the population figure is
    their sum; everything else is a mean or a proportion over the band's members, so the population
    figure is the count-weighted mean. Reducing this way is what makes the two implementations
    comparable at all: their age bands hold different people.

    `excluded` names the age bands to leave out, on both sides and for every seed. It is the set of
    bands that either implementation empties in any seed: the bands where immigration falls short
    of the projection, and so the only bands whose head counts the two implementations disagree
    about. Excluding them on one side alone would bias the comparison, so the caller passes the
    union and this function applies it to whichever file it is given.
    """
    excluded = excluded or set()
    totals: dict[tuple[str, int, str, str], float] = {}
    weights: dict[tuple[str, int, str, str], float] = {}

    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        variables = [name for name in reader.fieldnames or [] if name not in KEY_COLUMNS]
        summed = SUMMED_VARIABLES

        for row in reader:
            source = row["source"].lower()
            year = int(row["time"])
            sex = row["gender_name"].lower()
            count = float(row["count"])

            if band_key(source, year, sex, int(row["index_id"])) in excluded:
                continue

            # Before the `count <= 0` guard below: a band with nobody alive in it can still have
            # deaths and emigrations to contribute, and a summed variable has no denominator to
            # care about either way.
            for variable in sorted(summed):
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

    reduced = {key: totals[key] / weights[key] for key in totals}

    # The lattice detector reconstructs a count-weighted variable's numerator as `value * count`
    # (see LATTICE_MAX_DISTINCT_VALUES above). That is only right if the weight this function
    # divided by is the same head count it summed into `count`, which holds when every band with
    # people in it reports every variable. Checked rather than assumed: a file where it did not
    # hold would give the detector a wrong number and nothing would say so.
    for key, weight in weights.items():
        scenario, year, sex, variable = key
        if variable in summed:
            continue
        head_count = totals.get((scenario, year, sex, "count"))
        if head_count is None:
            continue
        if abs(weight - head_count) > 1e-6 * max(abs(head_count), 1.0):
            raise SystemExit(
                f"{path}: {key} was weighted by {weight} but the head count for that "
                f"(scenario, year, sex) is {head_count}. The reduction's numerator cannot be "
                f"reconstructed, so the lattice detector would classify on a wrong number.")

    return reduced


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

    # Set for the distribution test, which is a p-value against a threshold rather than a
    # difference against an allowance. `allowed` then holds the threshold and `difference` the
    # p-value, so the two kinds of comparison still report and aggregate the same way.
    p_value: float | None = None

    @property
    def passed(self) -> bool:
        if self.p_value is not None:
            return self.p_value >= self.allowed
        return abs(self.difference) <= self.allowed

    @property
    def ratio_of_allowed(self) -> float:
        if self.p_value is not None:
            # 1.0 is exactly the threshold, so this orders the same way as the others do.
            return self.allowed / self.p_value if self.p_value > 0 else math.inf
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
    excluded_bands: int = 0

    # variable -> (deviation id, reason), for the columns the baseline emits but never fills.
    uncomputed: dict[str, tuple[str, str]] = field(default_factory=dict)

    # Baseline runs that exited on a signal and were retried.
    retries: list[str] = field(default_factory=list)

    # The compatibility flags this build ran the comparison with, as passed on its command line.
    compat_flags: str = ""

    # The deviation-impact pass: what the flags are worth, reported rather than graded.
    # None when the pass did not run; see DeviationImpact.
    impact: "DeviationImpact | None" = None


def lattice_keys(values: list[float], scale: float) -> list[int]:
    """The values bucketed at the baseline's printed precision.

    Two figures the baseline's own output cannot tell apart must not count as different values.
    The baseline prints six significant digits, so `0.00029274` and `0.000292741` are one value
    and not two — and counting them as two is enough to hide a lattice series from the test below.
    """
    step = PRINTED_PRECISION_FLOOR * max(scale, 1e-12)
    return [round(value / step) for value in values]


def numerator_series(values: list[float], counts: list[float]) -> list[float]:
    """The numerator a count-weighted series was reduced from, per seed.

    `value` is `sum over bands of count_b * value_b` divided by `sum over bands of count_b`, so
    multiplying it back by the head count recovers the numerator. For a disease rate that numerator
    is a case count, which is the quantity the lattice rule is about; for a mean it is a total,
    which is continuous and will not be mistaken for a lattice.

    It is returned unbucketed on purpose. The caller buckets it with `lattice_keys` at the *same*
    printed-precision rule the reduced value gets, which is what makes this a change of the quantity
    asked about and not a change of the threshold. Rounding to a whole event instead — which the
    first version of this did — is a finer bucket than printed precision as soon as the numerator is
    large: a calibrated mean's numerator is a total of 80,354, and one unit in that is 1.2e-5
    relative, just above the 1e-5 floor. It made every calibrated mean on `HLM_India` fail. The
    re-score of the stored references is what found it.
    """
    return [value * count for value, count in zip(values, counts)]


def modal_share(keys: list[int]) -> float:
    """The share of the seeds that take the series' single commonest value."""
    if not keys:
        return 0.0
    return collections.Counter(keys).most_common(1)[0][1] / len(keys)


def distribution_p_value(base_keys: list[int], new_keys: list[int]) -> float:
    """How likely two lattice-valued samples this different are, if they came from one source.

    One Fisher exact test per distinct value — "this value against every other" — over the two
    implementations' counts, Bonferroni-corrected for the number of values tested. That tests the
    whole shape of the discrete distribution rather than one summary of it, and it is exact, so it
    holds at the counts these series actually have: a handful of events in twenty or sixty seeds,
    where every normal approximation is worthless.

    It is a real test rather than a waiver, but it is a blunt one at twenty seeds, and the exact
    numbers are worth knowing. Against the family-wide alpha of 1e-5, for a two-valued series:

      * at n = 20, a baseline that never leaves one value fails once this build leaves it in 14 of
        20 seeds — but 10 of 20 against 20 of 20 does NOT fail, because no arrangement of forty
        observations is unlikely enough at that alpha;
      * at n = 60, the same all-or-nothing case fails at 17 of 60, and 30 of 60 against 54 of 60
        fails as well.

    So a rare-event rate is barely testable at twenty seeds and properly testable at sixty. That is
    a second reason for the 60-seed confirmation, beyond the one docs/equivalence.md gives for the
    standard deviation.
    """
    values = sorted(set(base_keys) | set(new_keys))
    if len(values) < 2:
        return 1.0

    base_n, new_n = len(base_keys), len(new_keys)
    base_counts = collections.Counter(base_keys)
    new_counts = collections.Counter(new_keys)

    smallest = 1.0
    for value in values:
        a = base_counts[value]
        c = new_counts[value]
        smallest = min(smallest, fisher_exact_two_sided(a, base_n - a, c, new_n - c))
    return min(1.0, smallest * len(values))


def fisher_exact_two_sided(a: int, b: int, c: int, d: int) -> float:
    """The two-sided p-value of Fisher's exact test on the 2x2 table [[a, b], [c, d]].

    Written out rather than taken from scipy, because the harness has no third-party dependency
    and this is twenty lines. It sums the hypergeometric probability of every table with the same
    margins whose probability is no greater than the observed one — the standard two-sided
    definition.
    """
    total = a + b + c + d
    if total == 0:
        return 1.0

    row1, col1 = a + b, a + c

    def probability(k: int) -> float:
        return (math.comb(row1, k) * math.comb(total - row1, col1 - k)) / math.comb(total, col1)

    lower = max(0, col1 - (total - row1))
    upper = min(row1, col1)
    observed = probability(a)
    # A relative slack, because the probabilities are floating point and the observed table must
    # always be counted as no greater than itself.
    return min(1.0, sum(probability(k) for k in range(lower, upper + 1)
                        if probability(k) <= observed * (1.0 + 1e-9)))


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

        if variable in BASELINE_DOES_NOT_COMPUTE and all(v == 0.0 for v in base_values):
            identifier, reason = BASELINE_DOES_NOT_COMPUTE[variable]
            if all(v == 0.0 for v in new_values):
                # The exclusion says "the baseline emits this column and never fills it, and we
                # do". If *this* build's series is identically zero too, that second half is no
                # longer true, and the rule as written would hide the regression completely: it
                # would print "the baseline does not compute it" and skip, which is exactly what
                # it prints when everything is fine. Found by reviewing the exclusions against
                # ADR 0041, not by it failing.
                outcome.missing.append(
                    f"{key}: excluded as {identifier} because the baseline never fills it — but "
                    f"this build's series is identically zero as well, which is the deviation "
                    f"not holding rather than the deviation being applied")
                continue
            outcome.uncomputed.setdefault(variable, (identifier, reason))
            outcome.skipped.append(f"{key}: the baseline does not compute it ({identifier})")
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

        # Is this series lattice-valued? See LATTICE_MAX_DISTINCT_VALUES above for what that means,
        # why no quantile of such a series can be compared numerically, and why the question is
        # asked of the numerator rather than of the reduced value.
        base_counts = [baseline[seed].get((scenario, year, sex, "count")) for seed in seeds]
        new_counts = [new[seed].get((scenario, year, sex, "count")) for seed in seeds]
        on_numerator = (
            variable not in SUMMED_VARIABLES
            and all(count is not None and count > 0.0 for count in base_counts + new_counts))

        if on_numerator:
            base_numerators = numerator_series(base_values, base_counts)
            new_numerators = numerator_series(new_values, new_counts)
            numerator_scale = max(abs(value) for value in base_numerators + new_numerators) or 1.0
            base_keys = lattice_keys(base_numerators, numerator_scale)
            new_keys = lattice_keys(new_numerators, numerator_scale)
        else:
            # A summed variable's reduced value is its own numerator, and a head count of zero
            # leaves nothing to reconstruct. Both implementations' samples are bucketed on one
            # common scale, so that "distinct value" means the same thing on both sides.
            common_scale = max(abs(value) for value in base_values + new_values) or 1.0
            base_keys = lattice_keys(base_values, common_scale)
            new_keys = lattice_keys(new_values, common_scale)
        lattice = (len(set(base_keys) | set(new_keys)) <= LATTICE_MAX_DISTINCT_VALUES or
                   max(modal_share(base_keys), modal_share(new_keys)) > DEGENERATE_MODAL_SHARE)

        for name, se_factor in STATISTICS.items():
            if lattice and name != "mean":
                continue
            allowed = SIGMA_LIMIT * se_factor * math.sqrt(pooled_variance / n) + floor
            outcome.comparisons.append(
                Comparison(key, name, getattr(base, name), getattr(mine, name),
                           allowed=allowed,
                           difference=getattr(mine, name) - getattr(base, name)))

        if lattice:
            # The mean above, and the whole discrete distribution here. Between them they cover
            # everything the four dropped statistics were measuring, and they measure it with a
            # test that holds at these counts.
            probability = distribution_p_value(base_keys, new_keys)
            base_mode = collections.Counter(base_keys).most_common(1)[0][1] / n
            new_mode = collections.Counter(new_keys).most_common(1)[0][1] / n
            outcome.comparisons.append(
                Comparison(key, "distribution", base_mode, new_mode,
                           allowed=DISTRIBUTION_TEST_ALPHA, difference=probability,
                           p_value=probability))
            continue

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
        "HLM_India": Example(
            name="HLM_India",
            baseline_config=UPSTREAM_EXAMPLES / "HLM_India" / "config.json",
            new_config=REPO / "examples" / "HLM_India" / "config.json",
            # Its own: unlike HLM_France, this example ships an active intervention, and its dynamic
            # model is EBHLM, so the policy really is applied.
            intervention="food_labelling",
        ),
    }


# --- the deviation-impact measurement -----------------------------------------------------------


@dataclass
class ImpactSeries:
    """What one deviation set is worth for one (scenario, sex, variable), year by year."""

    scenario: str
    sex: str
    variable: str

    # year -> mean over seeds of (fixed - baseline-compatible). Positive means the fix raises the
    # value; the sign is part of the finding, so it is never taken away.
    by_year: dict[int, float] = field(default_factory=dict)

    # The same, relative to the baseline-compatible value, for a reader who wants a percentage.
    relative_by_year: dict[int, float] = field(default_factory=dict)

    @property
    def largest(self) -> float:
        """The year whose difference is largest in magnitude, signed."""
        if not self.by_year:
            return 0.0
        return max(self.by_year.values(), key=abs)

    @property
    def largest_year(self) -> int | None:
        if not self.by_year:
            return None
        return max(self.by_year, key=lambda year: abs(self.by_year[year]))


@dataclass
class DeviationImpact:
    """The difference the compatibility flags make, measured rather than argued.

    This is not a pass/fail result and nothing here can fail a run. It exists because a deviation
    that is only ever described in prose gets quoted for three runs after it stopped being true,
    and because "these two disagree" and "these two disagree by exactly what this fix is worth" are
    different statements (ADR 0041).
    """

    flags: str
    seeds: list[int]
    # Every series, including the ones that are identically zero; the report filters.
    series: list[ImpactSeries] = field(default_factory=list)
    # Series where the two runs agree to the last printed digit everywhere.
    unchanged: int = 0
    # Bands the flags-off run empties that the comparison's exclusion does not cover. Reported,
    # because it would make a difference figure for that band mean something slightly different.
    extra_empty_bands: int = 0
    timing_seconds: float = 0.0
    note: str = ""


def measure_impact(fixed: dict[int, dict], compatible: dict[int, dict], seeds: list[int],
                   flags: str) -> DeviationImpact:
    """`fixed` minus `compatible`, averaged over seeds, per (scenario, sex, variable, year).

    Both dictionaries are reductions of this build's own output on the same seeds and the same
    excluded bands, differing only in whether the compatibility flags were on. So the difference is
    the deviations and nothing else — no Monte Carlo noise, because the seeds are the same and the
    number of random draws per person per year is unchanged by a flag.
    """
    impact = DeviationImpact(flags=flags, seeds=list(seeds))

    keys = set()
    for seed in seeds:
        keys |= set(fixed.get(seed, {})) & set(compatible.get(seed, {}))

    grouped: dict[tuple[str, str, str], ImpactSeries] = {}
    for scenario, year, sex, variable in sorted(keys):
        differences = []
        compatible_values = []
        for seed in seeds:
            left = fixed.get(seed, {}).get((scenario, year, sex, variable))
            right = compatible.get(seed, {}).get((scenario, year, sex, variable))
            if left is None or right is None:
                continue
            differences.append(left - right)
            compatible_values.append(right)
        if not differences:
            continue

        mean_difference = statistics.fmean(differences)
        mean_compatible = statistics.fmean(compatible_values)

        # The baseline prints six significant digits, so a difference below that floor is not a
        # difference either implementation could report. Counting it as one would fill the section
        # with noise from the last bits of a double.
        scale = max(abs(mean_compatible), PRINTED_PRECISION_FLOOR)
        if abs(mean_difference) <= scale * PRINTED_PRECISION_FLOOR:
            impact.unchanged += 1
            continue

        series = grouped.setdefault((scenario, sex, variable),
                                    ImpactSeries(scenario=scenario, sex=sex, variable=variable))
        series.by_year[year] = mean_difference
        series.relative_by_year[year] = (mean_difference / mean_compatible
                                          if mean_compatible else math.inf)

    impact.series = sorted(grouped.values(), key=lambda s: -abs(s.largest))
    return impact


# --- the report ---------------------------------------------------------------------------------


def variable_of(comparison: Comparison) -> str:
    return comparison.key[3]


def report(outcome: Outcome, verbose: bool, max_failures: int) -> bool:
    failures = [c for c in outcome.comparisons if not c.passed]
    total = len(outcome.comparisons)

    print()
    print(f"=== {outcome.example}: {len(outcome.seeds)} seeds, {total} comparisons")
    for label, seconds in sorted(outcome.timings.items()):
        print(f"    {label}: {seconds:.1f}s")
    for label, digest in sorted(outcome.config_hashes.items()):
        print(f"    {label} config sha256: {digest}")
    print(f"    {outcome.excluded_bands} age band(s) excluded on both sides: the bands either "
          f"implementation empties, which immigration cannot refill (docs/equivalence.md)")
    if outcome.compat_flags:
        print(f"    compared with --baseline-compat {outcome.compat_flags}: this build reproduces "
              f"the baseline's deliberate deviations, so the comparison tests everything but them "
              f"(ADR 0041)")
    if outcome.retries:
        print(f"    {len(outcome.retries)} baseline run(s) exited on a signal and were retried "
              f"(audit B-01/B-02; docs/equivalence.md):")
        for line in outcome.retries:
            print(f"      {line}")

    for variable, (identifier, reason) in sorted(outcome.uncomputed.items()):
        print(f"    {variable}: not compared — the baseline emits the column and never fills it "
              f"({identifier}: {reason})")

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
            if worst.p_value is not None:
                print(f"      {variable}: {len(group)} comparison(s), "
                      f"years {years[0]}-{years[-1]}, worst {worst.statistic} "
                      f"baseline={worst.baseline:.3g} new={worst.new:.3g} "
                      f"p={worst.p_value:.3g} against {worst.allowed:.3g}")
            else:
                print(f"      {variable}: {len(group)} comparison(s), "
                      f"years {years[0]}-{years[-1]}, worst {worst.statistic} "
                      f"baseline={worst.baseline:.6g} new={worst.new:.6g} = "
                      f"{worst.ratio_of_allowed:.1f}x the allowance")

    # The largest excursions that still passed, so a systematic shift hiding inside a wide
    # allowance is visible rather than silent.
    if verbose:
        passed = sorted((c for c in outcome.comparisons if c.passed),
                        key=lambda c: -c.ratio_of_allowed)[:10]
        print("    largest differences that passed:")
        for comparison in passed:
            print(f"      {comparison.key} {comparison.statistic}: "
                  f"{comparison.ratio_of_allowed:.2f}x the allowance")

    report_impact(outcome, verbose)

    if outcome.missing:
        return False

    if len(failures) <= max_failures:
        if failures:
            print(f"    within the {max_failures} accepted: see docs/equivalence.md for what "
                  f"they are and why")
        return True

    return False


def impact_as_json(impact: "DeviationImpact | None") -> dict | None:
    if impact is None:
        return None
    return {
        "flags": impact.flags,
        "seeds": impact.seeds,
        "note": impact.note,
        "elapsed_seconds": impact.timing_seconds,
        "series_unchanged_to_printed_precision": impact.unchanged,
        "extra_empty_bands": impact.extra_empty_bands,
        "series": [
            {
                "scenario": series.scenario,
                "sex": series.sex,
                "variable": series.variable,
                "largest_difference": series.largest,
                "largest_difference_year": series.largest_year,
                "largest_relative": series.relative_by_year.get(series.largest_year),
                "by_year": {str(year): value for year, value in sorted(series.by_year.items())},
                "relative_by_year": {str(year): value
                                     for year, value in sorted(series.relative_by_year.items())},
            }
            for series in impact.series
        ],
    }


def report_impact(outcome: Outcome, verbose: bool) -> None:
    """The deviation-impact section. Nothing here can fail a run (ADR 0041)."""
    impact = outcome.impact
    if impact is None:
        return

    print()
    print(f"--- deviation impact: what --baseline-compat {impact.flags} is worth on "
          f"{outcome.example}")
    print(f"    this build's own output with the flags off, minus the same with them on, "
          f"averaged over {len(impact.seeds)} seeds. Reported, not graded.")
    if impact.timing_seconds:
        print(f"    the extra pass took {impact.timing_seconds:.1f}s")
    if impact.note:
        print(f"    {impact.note}")
    if impact.extra_empty_bands:
        print(f"    {impact.extra_empty_bands} age band(s) the flags-off run empties and the "
              f"comparison's exclusion does not cover; their cells are reduced with the "
              f"comparison's exclusion all the same, so the two sides stay comparable")

    if not impact.series:
        print(f"    no difference anywhere: all {impact.unchanged} series agree to the "
              f"baseline's printed precision. No recorded deviation reaches this run.")
        return

    print(f"    {len(impact.series)} series differ; {impact.unchanged} agree to the printed "
          f"precision")
    shown = impact.series if verbose else impact.series[:12]
    for series in shown:
        years = sorted(series.by_year)
        largest_year = series.largest_year
        relative = series.relative_by_year.get(largest_year, 0.0)
        print(f"      {series.scenario}/{series.sex}/{series.variable}: "
              f"largest {series.largest:+.6g} ({relative:+.3%}) in {largest_year}, "
              f"over {years[0]}-{years[-1]}")
        # The year-by-year curve is the evidence: a deviation with a shape nobody expected is
        # worth more than its worst year.
        if verbose:
            trace = ", ".join(f"{year}:{series.by_year[year]:+.4g}" for year in years)
            print(f"        {trace}")
    if len(impact.series) > len(shown):
        print(f"      … and {len(impact.series) - len(shown)} more; --verbose for all of them, "
              f"or read the JSON")


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
        "excluded_age_bands": outcome.excluded_bands,
        "not_computed_by_the_baseline": {v: {"deviation": d, "reason": r}
                                          for v, (d, r) in outcome.uncomputed.items()},
        "baseline_retries": outcome.retries,
        "skipped": len(outcome.skipped),
        "series_reported_by_one_side_only": outcome.missing,
        "sigma_limit": SIGMA_LIMIT,
        "printed_precision_floor": PRINTED_PRECISION_FLOOR,
        "baseline_compat_flags": outcome.compat_flags,
        "deviation_impact": impact_as_json(outcome.impact),
        "groups": sorted(by_group.values(),
                         key=lambda g: (-g["failed"], -g["worst"]["ratio"])),
    }


def run_impact_pass(*, example: Example, seeds: list[int], workdir: Path, name: str,
                    binary: Path, overlay: dict, excluded: set[Band], compat_flags: str,
                    compat_arguments: list[str], fixed_reference: dict[int, dict],
                    result_files: dict[tuple[str, int], Path], stop_time: int | None,
                    size_fraction: float | None, mode: str) -> DeviationImpact | None:
    """Runs this build again with the compatibility flags off, and measures the difference.

    `fixed_reference` is the reduction of the comparison's runs — the ones *with* the flags on. The
    naming is deliberately the other way round from what it looks like: with the flags on this
    build reproduces the baseline, so the comparison's runs are the *compatible* ones and this pass
    produces the *fixed* ones. See ADR 0041.
    """
    started = time.monotonic()
    impact_reduced: dict[int, dict] = {}
    extra_empty: set[Band] = set()
    note = ""

    order = list(seeds)
    for position, seed in enumerate(order):
        folder = workdir / name / "no-compat" / f"seed-{seed}"
        if folder.exists():
            shutil.rmtree(folder)
        folder.mkdir(parents=True)

        document = derive_config(example.new_config, seed, folder, example.intervention,
                                 stop_time, False, overlay, size_fraction)
        config_path = folder.parent / f"config-seed-{seed}.json"
        stage_example_files(example.new_config, config_path.parent)
        write_derived_config(config_path, document)

        run(binary, config_path, ["--threads", "1"],
            folder.parent / f"log-seed-{seed}.txt", output_folder=folder)

        result = find_result_csv(folder)
        extra_empty |= empty_bands(result) - excluded
        impact_reduced[seed] = reduce_result(result, excluded)

        # The probe: if the first seed's two result files are byte-identical, no recorded
        # deviation reaches this run and the remaining seeds would measure nothing. Comparing the
        # files rather than the reductions makes that a stronger statement — the reduction could
        # hide a difference the exclusion removed.
        if mode == "auto" and position == 0:
            comparison_result = result_files.get(("new", seed))
            if (comparison_result is not None
                    and comparison_result.read_bytes() == result.read_bytes()):
                impact = DeviationImpact(flags=compat_flags, seeds=[seed])
                impact.note = (f"stopped after seed {seed}: this build's output is byte-identical "
                               f"with the flags on and off, so no recorded deviation reaches this "
                               f"example with '{example.intervention}' active. "
                               f"--deviation-impact always runs the rest anyway.")
                impact.timing_seconds = time.monotonic() - started
                impact.unchanged = len(fixed_reference.get(seed, {}))
                return impact
        print(f"    {name} seed {seed}: deviation-impact pass done", flush=True)

    impact = measure_impact(impact_reduced, fixed_reference, seeds, compat_flags)
    impact.extra_empty_bands = len(extra_empty)
    impact.timing_seconds = time.monotonic() - started
    impact.note = note
    return impact


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
    parser.add_argument("--size-fraction", type=float, default=None,
                        help="override inputs.settings.size_fraction in both, which is the share of "
                             "the real population the cohort samples. It is what makes a "
                             "country-scale example comparable in an afternoon: HLM_India ships "
                             "0.001, which is 1.24 million people and forty-two minutes a run. Both "
                             "implementations get the same value, so the comparison is as valid a "
                             "test of the code as it is at full scale — it is simply not a test at "
                             "full scale. It is part of the derived config, so it is part of the "
                             "hash, so a reduced-cohort run cannot be compared against a full-scale "
                             "stored reference by accident.")
    parser.add_argument("--intervention", default=None,
                        help="override which intervention is active in both implementations. The "
                             "default is the example's own, which is `simple`. Each choice is a "
                             "different scenario and therefore a different stored reference, "
                             "because the reference is keyed by the config that produced it.")
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
    parser.add_argument("--max-failures", type=int, default=0,
                        help="how many out-of-tolerance comparisons to accept before failing. "
                             "The default is none. docs/equivalence.md records the residual this "
                             "implementation has and what causes it, and scripts/check.sh passes "
                             "that number — so a regression beyond it fails, and the known "
                             "difference does not leave a permanently red check nobody reads.")
    parser.add_argument("--reference-dir", type=Path, default=REFERENCE_DIR,
                        help="where the baseline's reduced output is cached (default: "
                             "tests/equivalence/reference). A run with many more seeds than the "
                             "checked-in reference should point this somewhere outside the "
                             "repository rather than commit tens of megabytes.")
    parser.add_argument("--json", type=Path, default=None,
                        help="also write the full outcome as JSON, so docs/equivalence.md can "
                             "quote exact numbers rather than round ones")
    parser.add_argument("--baseline-compat", default="all",
                        help="the compatibility flags this build runs the comparison with "
                             "(default: all). With them on it reproduces the baseline's "
                             "deliberate deviations, so the comparison tests everything except "
                             "them and an out-of-tolerance cell means something is wrong. "
                             "`--baseline-compat none` compares the fixed behaviour instead, "
                             "which is what every run before ADR 0041 did.")
    parser.add_argument("--deviation-impact", choices=("auto", "always", "never"), default="auto",
                        help="whether to run this build a second time with the compatibility "
                             "flags off and report what they are worth, per variable per year. "
                             "`auto` (the default) probes the first seed and stops if the two "
                             "runs agree byte for byte, which is the answer whenever no recorded "
                             "deviation reaches the example's active intervention. The section is "
                             "reported and never graded (ADR 0041).")
    parser.add_argument("--verbose", action="store_true")
    arguments = parser.parse_args()

    compat_flags = "" if arguments.baseline_compat.lower() in ("", "none") \
        else arguments.baseline_compat
    compat_arguments = ["--baseline-compat", compat_flags] if compat_flags else []
    if arguments.deviation_impact != "never" and not compat_flags:
        parser.error("--deviation-impact needs compatibility flags to measure; it is the "
                     "difference between a run with them and a run without")

    available = examples()
    names = arguments.example or list(available)
    for name in names:
        if name not in available:
            parser.error(f"unknown example '{name}'; known: {', '.join(available)}")
    if arguments.intervention is not None:
        for name in names:
            available[name] = replace(available[name], intervention=arguments.intervention)

    seeds = list(range(arguments.first_seed, arguments.first_seed + arguments.seeds))
    if len(seeds) < 3:
        parser.error("at least 3 seeds are needed for a distribution to mean anything")

    workdir = arguments.workdir or (Path(os.environ.get("TMPDIR", "/tmp")) / "hgps-equivalence")
    workdir.mkdir(parents=True, exist_ok=True)

    all_passed = True
    collected: list[Outcome] = []

    for name in names:
        example = available[name]
        outcome = Outcome(example=name, seeds=seeds, compat_flags=compat_flags)

        if not example.new_config.is_file():
            print(f"=== {name}: SKIPPED, {example.new_config} does not exist")
            continue

        # The config hash is over the derived config with the seed removed, so it identifies the
        # scenario rather than one run of it, and a stored reference can be matched to it.
        overlay = intervention_overlay(name)

        def hash_of(source: Path, is_baseline: bool) -> str:
            # Deliberately not absolutised: see derive_config. The seed is removed so the hash
            # identifies the scenario rather than one run of it.
            document = derive_config(source, 0, Path("/results"), example.intervention,
                                     arguments.stop_time, is_baseline, overlay,
                                     arguments.size_fraction, absolute=False)
            document["running"].pop("seed", None)
            return sha256_of(json.dumps(document, sort_keys=True))

        outcome.config_hashes["baseline"] = hash_of(example.baseline_config, True)
        outcome.config_hashes["new"] = hash_of(example.new_config, False)

        reference_path = (arguments.reference_dir / name /
                          f"{outcome.config_hashes['baseline']}.csv.gz")
        manifest_path = reference_path.with_suffix("").with_suffix(".json")

        baseline_reduced: dict[int, dict] = {}
        new_reduced: dict[int, dict] = {}
        stored_manifest: dict = {}

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
                stored_manifest = (json.loads(manifest_path.read_text())
                                   if manifest_path.is_file() else {})

        # Two passes, because the exclusion has to be the union over both implementations and every
        # seed before any file can be reduced with it. Pass one runs and reads only the head
        # counts; pass two reduces. The result files stay in the working directory between them.
        result_files: dict[tuple[str, int], Path] = {}
        baseline_empty: set[Band] = set()
        new_empty: set[Band] = set()

        for seed in seeds:
            for label, binary, source, is_baseline in (
                    ("baseline", arguments.baseline, example.baseline_config, True),
                    ("new", arguments.new, example.new_config, False)):
                if label == "baseline" and use_reference:
                    continue

                folder = workdir / name / label / f"seed-{seed}"
                if folder.exists():
                    shutil.rmtree(folder)
                folder.mkdir(parents=True)

                document = derive_config(source, seed, folder, example.intervention,
                                         arguments.stop_time, is_baseline, overlay,
                                         arguments.size_fraction)
                config_path = folder.parent / f"config-seed-{seed}.json"
                stage_example_files(source, config_path.parent)
                write_derived_config(config_path, document)

                extra = (["-T", "1"] if label == "baseline"
                         else ["--threads", "1", *compat_arguments])
                elapsed = run(binary, config_path, extra,
                              folder.parent / f"log-seed-{seed}.txt",
                              attempts=3 if is_baseline else 1,
                              retries=outcome.retries, output_folder=folder)
                outcome.timings[label] = outcome.timings.get(label, 0.0) + elapsed

                result = find_result_csv(folder)
                result_files[(label, seed)] = result
                (baseline_empty if is_baseline else new_empty).update(empty_bands(result))
            print(f"    {name} seed {seed}: done", flush=True)

        if use_reference:
            baseline_empty = {tuple(band) for band in stored_manifest.get("baseline_empty_bands",
                                                                          [])}

        excluded = baseline_empty | new_empty
        outcome.excluded_bands = len(excluded)

        if use_reference:
            recorded = {tuple(band) for band in stored_manifest.get("excluded_bands", [])}
            if excluded != recorded:
                extra_now = sorted(excluded - recorded)[:10]
                gone = sorted(recorded - excluded)[:10]
                print(f"=== {name}: the emptying-band exclusion has changed since the stored "
                      f"reference was written, so the stored reduction is no longer the right "
                      f"one.")
                print(f"    recorded {len(recorded)} bands, this run needs {len(excluded)}")
                if extra_now:
                    print(f"    newly empty, e.g.: {extra_now}")
                if gone:
                    print(f"    no longer empty, e.g.: {gone}")
                print(f"    re-run with --refresh-reference; see docs/equivalence.md")
                all_passed = False
                continue

        for (label, seed), result in result_files.items():
            target = baseline_reduced if label == "baseline" else new_reduced
            target[seed] = reduce_result(result, excluded)

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
                "size_fraction_override": arguments.size_fraction,
                "baseline_binary": str(arguments.baseline),
                "reduction": "count-weighted mean over age bands; head counts (count, deaths, "
                             "emigrations and the four weight categories) summed; the age bands "
                             "listed in excluded_bands are left out on both sides — see "
                             "docs/equivalence.md",
                "baseline_empty_bands": sorted(baseline_empty),
                "excluded_bands": sorted(excluded),
                "written_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            }, indent=2) + "\n")
            try:
                shown = reference_path.relative_to(REPO)
            except ValueError:
                shown = reference_path
            print(f"    wrote reference {shown}")

        compare(baseline_reduced, new_reduced, seeds, outcome)

        # The deviation-impact pass. This build again, same seeds, same excluded bands, with the
        # compatibility flags off — so the difference is the deviations and nothing else. It can
        # fail nothing; it is a measurement (ADR 0041).
        if arguments.deviation_impact != "never":
            outcome.impact = run_impact_pass(
                example=example, seeds=seeds, workdir=workdir, name=name,
                binary=arguments.new, overlay=overlay, excluded=excluded,
                compat_flags=compat_flags, compat_arguments=compat_arguments,
                fixed_reference=new_reduced, result_files=result_files,
                stop_time=arguments.stop_time, size_fraction=arguments.size_fraction,
                mode=arguments.deviation_impact)

        all_passed &= report(outcome, arguments.verbose, arguments.max_failures)
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
