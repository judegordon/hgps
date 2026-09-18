#!/usr/bin/env python3
"""Tests for the equivalence harness itself.

The harness decides whether this implementation agrees with the baseline, so a mistake in it is
invisible in exactly the way that matters: it says PASS. Two of its rules have already been wrong
once each —

  * the standard deviation and tail percentiles of a point-mass series were compared under normal
    theory, which produced seven failures that were artefacts of the test;
  * the median of a lattice-valued series was compared the same way, which produced eighteen more
    at sixty seeds, and would have produced more the more seeds were run.

Both were found by a whole run of the real thing taking twenty minutes. These tests check the same
rules in milliseconds, against numbers worked out by hand or taken from a textbook.

Run them with `python3 -m unittest discover -s tests/equivalence` or through CTest, which runs
this file as the test `EquivalenceHarness`.
"""

from __future__ import annotations

import csv
import hashlib
import importlib.util
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent

_spec = importlib.util.spec_from_file_location("eqrun", HERE / "run.py")
eqrun = importlib.util.module_from_spec(_spec)
sys.modules["eqrun"] = eqrun
_spec.loader.exec_module(eqrun)


class FisherExactTest(unittest.TestCase):
    """The 2x2 exact test, against values that can be checked away from this code."""

    def test_the_lady_tasting_tea(self):
        # Fisher's own example: 3 of 4 guessed right in each row. R's fisher.test gives 0.4857.
        self.assertAlmostEqual(0.4857142857, eqrun.fisher_exact_two_sided(3, 1, 1, 3), places=9)

    def test_a_table_with_no_association_is_certain(self):
        self.assertAlmostEqual(1.0, eqrun.fisher_exact_two_sided(5, 5, 5, 5))

    def test_complete_separation_is_the_least_likely_table(self):
        # Every arrangement is [[10,0],[0,10]] or its mirror: 2 / C(20,10).
        expected = 2.0 / math.comb(20, 10)
        self.assertAlmostEqual(expected, eqrun.fisher_exact_two_sided(10, 0, 0, 10), places=12)

    def test_it_is_symmetric_in_the_two_rows(self):
        self.assertAlmostEqual(eqrun.fisher_exact_two_sided(7, 3, 2, 8),
                               eqrun.fisher_exact_two_sided(2, 8, 7, 3))

    def test_an_empty_table_is_certain(self):
        self.assertAlmostEqual(1.0, eqrun.fisher_exact_two_sided(0, 0, 0, 0))


class QuantileTest(unittest.TestCase):
    """Type 7, which is what R's default and numpy's default both give."""

    def test_the_median_of_an_odd_sample(self):
        self.assertAlmostEqual(3.0, eqrun.quantile([5, 1, 3, 2, 4], 0.5))

    def test_the_median_of_an_even_sample_interpolates(self):
        self.assertAlmostEqual(2.5, eqrun.quantile([1, 2, 3, 4], 0.5))

    def test_a_tail_quantile_interpolates(self):
        # numpy.quantile(range(11), 0.05) == 0.5
        self.assertAlmostEqual(0.5, eqrun.quantile(list(range(11)), 0.05))
        self.assertAlmostEqual(9.5, eqrun.quantile(list(range(11)), 0.95))

    def test_one_value_is_its_own_quantile(self):
        self.assertAlmostEqual(7.0, eqrun.quantile([7.0], 0.05))


class LatticeKeyTest(unittest.TestCase):
    """Bucketing at the baseline's printed precision."""

    def test_values_the_baseline_cannot_tell_apart_are_one_value(self):
        # Six significant digits: these two differ in the tenth.
        values = [0.00029274, 0.000292741, 0.0]
        keys = eqrun.lattice_keys(values, max(values))
        self.assertEqual(keys[0], keys[1])
        self.assertNotEqual(keys[0], keys[2])

    def test_values_the_baseline_does_print_apart_stay_apart(self):
        values = [1.0, 1.001]
        keys = eqrun.lattice_keys(values, max(values))
        self.assertNotEqual(keys[0], keys[1])

    def test_a_zero_scale_does_not_divide_by_zero(self):
        self.assertEqual([0, 0], eqrun.lattice_keys([0.0, 0.0], 0.0))


class DistributionTest(unittest.TestCase):
    """The exact test that replaces the quantiles for a lattice-valued series."""

    def test_identical_counts_are_perfectly_compatible(self):
        self.assertAlmostEqual(1.0, eqrun.distribution_p_value([0] * 13 + [1] * 7,
                                                               [0] * 13 + [1] * 7))

    def test_one_value_on_both_sides_is_not_a_comparison(self):
        self.assertEqual(1.0, eqrun.distribution_p_value([4] * 20, [4] * 20))

    def test_the_eighteen_sixty_seed_medians_that_failed_are_compatible(self):
        # incidence_esophaguscancer at (intervention, 2025, male): the counts that produced a
        # whole-lattice-step difference in the median. The distributions are not distinguishable.
        base = [0] * 26 + [1] * 28 + [2] * 6
        new = [0] * 33 + [1] * 21 + [2] * 6
        self.assertGreater(eqrun.distribution_p_value(base, new),
                           eqrun.DISTRIBUTION_TEST_ALPHA * 1000)

    def test_a_rate_that_really_differs_fails(self):
        # All-or-nothing at sixty seeds: the calibration in run.py says 17 of 60 is the threshold.
        base = [0] * 60
        self.assertGreater(eqrun.distribution_p_value(base, [0] * 44 + [1] * 16),
                           eqrun.DISTRIBUTION_TEST_ALPHA)
        self.assertLess(eqrun.distribution_p_value(base, [0] * 43 + [1] * 17),
                        eqrun.DISTRIBUTION_TEST_ALPHA)

    def test_it_is_corrected_for_the_number_of_values_tested(self):
        # Three values means three tests, so the smallest p is multiplied by three.
        base = [0] * 10 + [1] * 5 + [2] * 5
        new = [0] * 5 + [1] * 10 + [2] * 5
        smallest = min(eqrun.fisher_exact_two_sided(10, 10, 5, 15),
                       eqrun.fisher_exact_two_sided(5, 15, 10, 10),
                       eqrun.fisher_exact_two_sided(5, 15, 5, 15))
        self.assertAlmostEqual(min(1.0, smallest * 3), eqrun.distribution_p_value(base, new))


def _series(values_by_seed):
    """{seed: {key: value}} for one series, so `compare` has something to chew on."""
    key = ("baseline", 2020, "male", "mean_bmi")
    return {seed: {key: value} for seed, value in values_by_seed.items()}


def _compare(base_values, new_values):
    seeds = list(range(1, len(base_values) + 1))
    outcome = eqrun.Outcome(example="test", seeds=seeds)
    eqrun.compare(_series(dict(zip(seeds, base_values))),
                  _series(dict(zip(seeds, new_values))), seeds, outcome)
    return outcome


class WhichStatisticsAreComparedTest(unittest.TestCase):
    """A lattice series is compared by its mean and its distribution; a continuous one by five."""

    def test_a_continuous_series_gets_all_five_statistics(self):
        base = [20.0 + 0.37 * i for i in range(20)]
        new = [20.0 + 0.37 * i + 0.01 for i in range(20)]
        statistics = {c.statistic for c in _compare(base, new).comparisons}
        self.assertEqual({"mean", "sd", "p5", "p50", "p95"}, statistics)

    def test_a_lattice_series_gets_the_mean_and_the_distribution(self):
        base = [0.0] * 13 + [1.0] * 7
        new = [0.0] * 11 + [1.0] * 9
        statistics = {c.statistic for c in _compare(base, new).comparisons}
        self.assertEqual({"mean", "distribution"}, statistics)

    def test_a_point_mass_with_many_distinct_jumps_is_still_a_lattice(self):
        # Eleven distinct values, so the count rule does not fire — but one value covers more than
        # half the seeds, so the median is that value and is a step function all the same.
        base = [5.0] * 11 + [5.0 + i for i in range(1, 10)]
        new = [5.0] * 12 + [5.0 + i for i in range(1, 9)]
        statistics = {c.statistic for c in _compare(base, new).comparisons}
        self.assertEqual({"mean", "distribution"}, statistics)

    def test_the_failing_sixty_seed_medians_now_pass(self):
        base = [0.0] * 26 + [0.00029274] * 28 + [0.00058548] * 6
        new = [0.0] * 33 + [0.00029274] * 21 + [0.00058548] * 6
        outcome = _compare(base, new)
        self.assertTrue(all(c.passed for c in outcome.comparisons),
                        [f"{c.statistic}: {c.baseline} vs {c.new}"
                         for c in outcome.comparisons if not c.passed])

    def test_a_difference_that_is_real_still_fails(self):
        # Same shape, but this build's rate is nothing like the baseline's.
        base = [0.0] * 60
        new = [0.0] * 20 + [0.00029274] * 40
        outcome = _compare(base, new)
        self.assertFalse(all(c.passed for c in outcome.comparisons))


def _rate_series(case_counts, head_counts):
    """{seed: {rate key, count key}} for a rate whose numerator and denominator are both given.

    This is the shape the reduction actually produces for a disease rate: the value is
    `cases / head count` and the head count is beside it as its own variable.
    """
    # `prevalence_`, not `incidence_`: an incidence is undefined in the run's first year and these
    # samples are all one year, so the comparison would be skipped before the detector saw it.
    rate_key = ("baseline", 2020, "male", "prevalence_gout")
    count_key = ("baseline", 2020, "male", "count")
    return {seed: {rate_key: cases / head, count_key: float(head)}
            for seed, (cases, head) in enumerate(zip(case_counts, head_counts), start=1)}


def _compare_rates(base_cases, base_heads, new_cases, new_heads):
    seeds = list(range(1, len(base_cases) + 1))
    outcome = eqrun.Outcome(example="test", seeds=seeds)
    eqrun.compare(_rate_series(base_cases, base_heads), _rate_series(new_cases, new_heads),
                  seeds, outcome)
    return outcome


def _of(outcome, variable):
    """The comparisons of one variable — the head count is beside it and is compared too."""
    return [c for c in outcome.comparisons if c.key[3] == variable]


class LatticeOnTheNumeratorTest(unittest.TestCase):
    """The detector asks its question of the case count, not of the rate (docs/backlog.md item 11).

    The four `HLM_India` series this fixes had 43 to 79 distinct *rates* and a third of their seeds
    exactly zero: a handful of case counts divided by a head count that moves seed to seed.
    """

    # A head count that differs in every seed, which is what smears the lattice.
    HEADS = [12406 + i for i in range(60)]

    def test_a_rate_whose_numerator_is_a_small_count_is_a_lattice(self):
        # Twenty of sixty seeds have no cases, the rest have one or two — and because the
        # denominator moves, the *rate* takes a different value in almost every seed.
        cases = ([0] * 20 + [1] * 30 + [2] * 10)
        rates = {c / h for c, h in zip(cases, self.HEADS)}
        self.assertGreater(len(rates), eqrun.LATTICE_MAX_DISTINCT_VALUES,
                           "the rate must look continuous, or this tests nothing")

        outcome = _compare_rates(cases, self.HEADS, cases, self.HEADS)
        self.assertEqual({"mean", "distribution"},
                         {c.statistic for c in _of(outcome, "prevalence_gout")})

    def test_the_same_series_read_as_a_rate_is_not_one(self):
        # The old behaviour, kept as a test so that what changed is written down: with no head
        # count beside it the detector can only look at the rate, and it sees a continuous series.
        cases = ([0] * 20 + [1] * 30 + [2] * 10)
        rates = [c / h for c, h in zip(cases, self.HEADS)]
        statistics = {c.statistic for c in _compare(rates, rates).comparisons}
        self.assertEqual({"mean", "sd", "p5", "p50", "p95"}, statistics)

    def test_the_six_india_residuals_are_the_shape_this_fixes(self):
        # `incidence_gout` at (baseline, 2019, female) failed in both 60-seed runs at 1.02x to
        # 1.09x of its allowance, on the 95th percentile. Written here as a prevalence for the
        # reason above; the shape of the numbers is what the test is about. With the classification taken from the
        # numerator there is no percentile comparison to fail, and the distribution test — which
        # does hold at these counts — passes.
        base_cases = [0] * 7 + [1] * 33 + [2] * 15 + [3] * 5
        new_cases = [0] * 5 + [1] * 35 + [2] * 14 + [3] * 6
        new_heads = [12400 + 2 * i for i in range(60)]

        outcome = _compare_rates(base_cases, self.HEADS, new_cases, new_heads)
        compared = _of(outcome, "prevalence_gout")
        self.assertEqual({"mean", "distribution"}, {c.statistic for c in compared})
        self.assertTrue(all(c.passed for c in compared),
                        [f"{c.statistic}: {c.baseline} vs {c.new}"
                         for c in compared if not c.passed])

    def test_a_real_difference_in_the_counts_still_fails(self):
        # The rule must not be a waiver: the same denominators, a case count that really differs.
        base_cases = [0] * 60
        new_cases = [0] * 15 + [1] * 45
        outcome = _compare_rates(base_cases, self.HEADS, new_cases, self.HEADS)
        self.assertFalse(all(c.passed for c in _of(outcome, "prevalence_gout")))

    def test_a_continuous_mean_is_not_turned_into_a_lattice_by_its_numerator(self):
        # A mean's numerator is a total rather than a count, so multiplying it back by the head
        # count leaves it continuous. It must still get all five statistics.
        heads = [3146 + i for i in range(20)]
        values = [25.0 + 0.037 * i for i in range(20)]
        key = ("baseline", 2020, "male", "mean_bmi")
        count_key = ("baseline", 2020, "male", "count")
        seeds = list(range(1, 21))
        side = {seed: {key: values[seed - 1], count_key: float(heads[seed - 1])} for seed in seeds}
        outcome = eqrun.Outcome(example="test", seeds=seeds)
        eqrun.compare(side, side, seeds, outcome)
        self.assertEqual({"mean", "sd", "p5", "p50", "p95"},
                         {c.statistic for c in _of(outcome, "mean_bmi")})

    def test_a_calibrated_mean_that_is_the_same_in_every_seed_is_still_a_point_mass(self):
        # The other direction: a band mean calibration pins takes one value in every seed, and the
        # cohort size is the same in every seed too, so its numerator is one value as well. It has
        # to stay on the exact-distribution path — the synthetic self-check's detection of a 1%
        # shift in `mean_bmi` runs through it (docs/equivalence-method.md 7.2).
        key = ("baseline", 2020, "male", "mean_bmi")
        count_key = ("baseline", 2020, "male", "count")
        seeds = list(range(1, 21))
        side = {seed: {key: 25.541647, count_key: 3146.0} for seed in seeds}
        outcome = eqrun.Outcome(example="test", seeds=seeds)
        eqrun.compare(side, side, seeds, outcome)
        self.assertEqual({"mean", "distribution"},
                         {c.statistic for c in _of(outcome, "mean_bmi")})

    def test_the_numerator_is_the_value_times_the_head_count(self):
        # Unbucketed: the caller buckets it at the baseline's printed precision, the same rule the
        # reduced value gets. The baseline prints six significant digits, so a rate read back and
        # multiplied by the head count is a few parts per million away from the count it came from,
        # and that bucketing is what absorbs the difference.
        numerators = eqrun.numerator_series([0.000967274, 0.00096728, 0.001047],
                                            [12406.0, 12406.0, 12406.0])
        self.assertEqual([12, 12, 13], [round(value) for value in numerators])

    def test_a_constant_head_count_leaves_every_bucket_where_it_was(self):
        # The property that says this is a change of the quantity asked about and not of the
        # threshold: multiplying the values and the scale by the same number moves nothing.
        rates = [0.000967274, 0.00096728, 0.001047, 0.0]
        heads = [12406.0] * 4
        on_rates = eqrun.lattice_keys(rates, max(rates))
        numerators = eqrun.numerator_series(rates, heads)
        on_numerators = eqrun.lattice_keys(numerators, max(numerators))
        self.assertEqual(on_rates, on_numerators)

    def test_a_calibrated_mean_is_not_split_by_its_own_size(self):
        # The first version of this rounded the numerator to a whole event, which is a finer bucket
        # than printed precision once the numerator is large. A band mean of 25.541647 over 3,146
        # people is a numerator of 80,354, and one unit in that is 1.2e-5 relative — just above the
        # 1e-5 floor — so two runs agreeing to the precision the baseline prints were split apart.
        # Every calibrated mean on `HLM_India` failed. This pins the fix.
        base = [25.541647043865236] * 20
        new = [25.541647043865240] * 20
        heads = [3146.0] * 20
        base_keys = eqrun.lattice_keys(eqrun.numerator_series(base, heads),
                                       max(eqrun.numerator_series(base, heads)))
        new_keys = eqrun.lattice_keys(eqrun.numerator_series(new, heads),
                                      max(eqrun.numerator_series(new, heads)))
        self.assertEqual(set(base_keys), set(new_keys))


class ReductionTest(unittest.TestCase):
    """The count-weighted reduction over age bands, and what it leaves out."""

    HEADER = ["source", "run", "time", "gender_name", "index_id", "count", "deaths", "mean_bmi"]
    ROWS = [
        ["baseline", 1, 2020, "male", 30, 10, 1, 25.0],
        ["baseline", 1, 2020, "male", 31, 30, 2, 27.0],
        ["baseline", 1, 2020, "male", 32, 0, 0, 0.0],
    ]

    def _write(self, rows):
        directory = Path(tempfile.mkdtemp())
        path = directory / "result.csv"
        with path.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(self.HEADER)
            writer.writerows(rows)
        return path

    def test_counts_are_summed_and_everything_else_is_count_weighted(self):
        reduced = eqrun.reduce_result(self._write(self.ROWS))
        self.assertAlmostEqual(40.0, reduced[("baseline", 2020, "male", "count")])
        self.assertAlmostEqual(3.0, reduced[("baseline", 2020, "male", "deaths")])
        # (10*25 + 30*27) / 40 — the empty band contributes nothing to either side of it.
        self.assertAlmostEqual(26.5, reduced[("baseline", 2020, "male", "mean_bmi")])

    def test_an_excluded_band_is_left_out_of_both_the_weight_and_the_total(self):
        excluded = {eqrun.band_key("baseline", 2020, "male", 31)}
        reduced = eqrun.reduce_result(self._write(self.ROWS), excluded)
        self.assertAlmostEqual(10.0, reduced[("baseline", 2020, "male", "count")])
        self.assertAlmostEqual(25.0, reduced[("baseline", 2020, "male", "mean_bmi")])

    def test_an_empty_band_is_reported_as_one(self):
        bands = eqrun.empty_bands(self._write(self.ROWS))
        self.assertIn(eqrun.band_key("baseline", 2020, "male", 32), bands)
        self.assertNotIn(eqrun.band_key("baseline", 2020, "male", 30), bands)


class ResultFileChoiceTest(unittest.TestCase):
    """Picking the whole-population CSV out of a folder of income-stratified ones."""

    def test_the_stratified_files_are_not_the_one(self):
        directory = Path(tempfile.mkdtemp())
        for name in ("result_2026-01-01_10-00-00.csv",
                     "result_2026-01-01_10-00-00_HighIncome.csv",
                     "result_2026-01-01_10-00-00_LowIncome.csv",
                     "result_2026-01-01_10-00-00_IndividualIDTracking.csv"):
            (directory / name).write_text("source\n")
        self.assertEqual("result_2026-01-01_10-00-00.csv",
                         eqrun.find_result_csv(directory).name)


class StagingAnExampleTest(unittest.TestCase):
    """That a working directory cannot write back into the example it was staged from.

    This is the only test here that exists because of an accident rather than a rule: an ad-hoc
    script named its derived config `config.json`, wrote it into a staged directory where every file
    including `config.json` was a symlink, and rewrote two of the converted examples in place. The
    fix is that the config is copied and the rest is linked
    (docs/decisions/0039-scratch-directories-copy-what-they-may-write.md); these assert it, because
    the property is invisible while it holds.
    """

    def setUp(self):
        self.example = Path(tempfile.mkdtemp()) / "example"
        self.example.mkdir()
        self.config = self.example / "config.json"
        self.config.write_text(json.dumps({"running": {"seed": 1}}, indent=1) + "\n")
        self.data = self.example / "Country.DataFile.csv"
        self.data.write_text("Age,Male,Female\n0,1,1\n")
        self.model = self.example / "static_model.json"
        self.model.write_text(json.dumps({"RiskFactorModels": {}}) + "\n")
        self.into = Path(tempfile.mkdtemp()) / "work"

    @staticmethod
    def _sha256(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def test_writing_the_derived_config_does_not_touch_the_source_example(self):
        # The exact accident: a derived config named after the one it came from.
        before = self._sha256(self.config)
        eqrun.stage_example_files(self.config, self.into)
        eqrun.write_derived_config(self.into / "config.json",
                                   {"running": {"seed": 99}, "derived": True})

        self.assertEqual(before, self._sha256(self.config))
        self.assertEqual({"running": {"seed": 1}},
                         json.loads(self.config.read_text()))
        self.assertTrue(json.loads((self.into / "config.json").read_text())["derived"])

    def test_the_config_is_a_copy_and_the_inputs_are_links(self):
        eqrun.stage_example_files(self.config, self.into)

        self.assertFalse((self.into / "config.json").is_symlink())
        # The data and the model file are read, never written, and one of them is 42 MB on the
        # India example — so those stay links.
        self.assertTrue((self.into / "Country.DataFile.csv").is_symlink())
        self.assertTrue((self.into / "static_model.json").is_symlink())
        self.assertEqual(self.data.read_text(),
                         (self.into / "Country.DataFile.csv").read_text())

    def test_writing_through_a_symlink_is_refused(self):
        # The general case, for a name this staging did not anticipate: refuse, rather than write
        # into whatever the link points at.
        eqrun.stage_example_files(self.config, self.into)
        before = self._sha256(self.model)

        with self.assertRaises(RuntimeError) as raised:
            eqrun.write_derived_config(self.into / "static_model.json", {"derived": True})

        self.assertIn("symlink", str(raised.exception))
        self.assertEqual(before, self._sha256(self.model))

    def test_staging_twice_leaves_the_first_staging_alone(self):
        # The harness stages once per (example, side) and then writes one config per seed, so this
        # runs on every seed after the first.
        eqrun.stage_example_files(self.config, self.into)
        eqrun.write_derived_config(self.into / "config.json", {"derived": True})
        eqrun.stage_example_files(self.config, self.into)

        self.assertTrue(json.loads((self.into / "config.json").read_text())["derived"])
        self.assertEqual({"running": {"seed": 1}}, json.loads(self.config.read_text()))


if __name__ == "__main__":
    unittest.main()


def _named_series(variable, values_by_seed, year=2020):
    key = ("baseline", year, "male", variable)
    return {seed: {key: value} for seed, value in values_by_seed.items()}


class BaselineDoesNotComputeTest(unittest.TestCase):
    """The exclusion for a column the baseline emits and never fills (B-22).

    The rule has two halves — "the baseline never fills it" and "we do" — and until this run it
    only checked the first. Found by reviewing the exclusions against ADR 0041.
    """

    def _run(self, base_values, new_values):
        seeds = list(range(1, len(base_values) + 1))
        outcome = eqrun.Outcome(example="test", seeds=seeds)
        eqrun.compare(_named_series("std_income", dict(zip(seeds, base_values))),
                      _named_series("std_income", dict(zip(seeds, new_values))), seeds, outcome)
        return outcome

    def test_a_column_the_baseline_never_fills_is_excluded_and_named(self):
        outcome = self._run([0.0] * 20, [12.5 + 0.1 * i for i in range(20)])
        self.assertEqual([], outcome.comparisons)
        self.assertIn("std_income", outcome.uncomputed)
        self.assertEqual("B-22", outcome.uncomputed["std_income"][0])
        self.assertEqual([], outcome.missing)

    def test_both_sides_identically_zero_is_reported_rather_than_excluded(self):
        # The regression the old rule would have hidden behind its own message: this build has
        # stopped computing the column too, and "the baseline does not compute it" is what the
        # harness printed either way.
        outcome = self._run([0.0] * 20, [0.0] * 20)
        self.assertEqual([], outcome.comparisons)
        self.assertEqual(1, len(outcome.missing))
        self.assertIn("identically zero as well", outcome.missing[0])
        self.assertNotIn("std_income", outcome.uncomputed)

    def test_the_rule_disarms_itself_if_the_baseline_starts_computing_it(self):
        outcome = self._run([12.4 + 0.1 * i for i in range(20)],
                            [12.5 + 0.1 * i for i in range(20)])
        self.assertNotEqual([], outcome.comparisons)
        self.assertEqual({}, outcome.uncomputed)


class DeviationImpactTest(unittest.TestCase):
    """The deviation-impact measurement: fixed minus baseline-compatible, per series (ADR 0041)."""

    @staticmethod
    def _reductions(fixed_by_year, compatible_by_year, seeds=(1, 2, 3)):
        fixed = {seed: {("intervention", year, "male", "mean_bmi"): value
                        for year, value in fixed_by_year.items()} for seed in seeds}
        compatible = {seed: {("intervention", year, "male", "mean_bmi"): value
                             for year, value in compatible_by_year.items()} for seed in seeds}
        return fixed, compatible, list(seeds)

    def test_two_identical_runs_report_no_difference(self):
        fixed, compatible, seeds = self._reductions({2020: 25.0, 2021: 25.5},
                                                    {2020: 25.0, 2021: 25.5})
        impact = eqrun.measure_impact(fixed, compatible, seeds, "all")
        self.assertEqual([], impact.series)
        self.assertEqual(2, impact.unchanged)

    def test_the_difference_keeps_its_sign_and_finds_its_largest_year(self):
        # B-24's shape: nothing in the policy's first year, then a difference that grows while the
        # coverage window is open. The sign says which way the fix moves the number.
        fixed, compatible, seeds = self._reductions(
            {2022: 25.0, 2023: 25.0007, 2024: 25.0045, 2025: 25.0124},
            {2022: 25.0, 2023: 25.0, 2024: 25.0, 2025: 25.0})
        impact = eqrun.measure_impact(fixed, compatible, seeds, "B-24")

        self.assertEqual(1, len(impact.series))
        series = impact.series[0]
        self.assertEqual("mean_bmi", series.variable)
        self.assertEqual(2025, series.largest_year)
        self.assertAlmostEqual(0.0124, series.largest, places=9)
        self.assertGreater(series.largest, 0.0)
        # The first year is below the printed-precision floor, so it is not counted as a
        # difference — which is the right answer: the defect needs a previous failed draw.
        self.assertNotIn(2022, series.by_year)

    def test_a_difference_below_the_printed_precision_is_not_a_difference(self):
        # The baseline writes six significant digits, so a difference in the last bits of a double
        # is not something either implementation could report.
        fixed, compatible, seeds = self._reductions({2020: 25.000000001}, {2020: 25.0})
        impact = eqrun.measure_impact(fixed, compatible, seeds, "all")
        self.assertEqual([], impact.series)
        self.assertEqual(1, impact.unchanged)

    def test_the_relative_figure_is_against_the_baseline_compatible_value(self):
        fixed, compatible, seeds = self._reductions({2020: 22.0}, {2020: 20.0})
        impact = eqrun.measure_impact(fixed, compatible, seeds, "all")
        series = impact.series[0]
        self.assertAlmostEqual(2.0, series.largest, places=9)
        self.assertAlmostEqual(0.1, series.relative_by_year[2020], places=9)

    def test_a_series_only_one_side_has_is_left_out_rather_than_guessed(self):
        fixed = {1: {("intervention", 2020, "male", "mean_bmi"): 25.0,
                     ("intervention", 2020, "male", "mean_new_thing"): 1.0}}
        compatible = {1: {("intervention", 2020, "male", "mean_bmi"): 24.0}}
        impact = eqrun.measure_impact(fixed, compatible, [1], "all")
        self.assertEqual(["mean_bmi"], [s.variable for s in impact.series])

    def test_nothing_in_the_impact_can_fail_a_run(self):
        # It is a measurement, not a gate (ADR 0041). `report_impact` returns nothing and
        # `report`'s verdict is computed before it is called.
        outcome = eqrun.Outcome(example="test", seeds=[1, 2, 3])
        fixed, compatible, seeds = self._reductions({2020: 99.0}, {2020: 1.0})
        outcome.impact = eqrun.measure_impact(fixed, compatible, seeds, "all")
        self.assertTrue(eqrun.report(outcome, verbose=False, max_failures=0))
