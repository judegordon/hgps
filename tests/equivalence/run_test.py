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
import importlib.util
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


if __name__ == "__main__":
    unittest.main()
