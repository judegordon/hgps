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
import gzip
import hashlib
import importlib.util
import json
import math
import random
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
        self.assertGreater(eqrun.distribution_p_value(base, new), 0.1)

    def test_a_rate_that_really_differs_fails(self):
        # All-or-nothing at sixty seeds, against the level Holm leaves for the smallest p-value in
        # a family of the ~45,000 tests one example produces: alpha / m, about 2.2e-7. The
        # threshold there is 22 of 60, which is the number `distribution_p_value`'s docstring and
        # docs/equivalence-method.md 5.3 quote. It was 17 of 60 while the exact test was judged
        # against a hand-counted 0.05/5000, and that is the price of one consistent family rather
        # than two levels that never had to agree.
        per_test = eqrun.FAMILY_WISE_ALPHA / 45000
        base = [0] * 60
        self.assertGreater(eqrun.distribution_p_value(base, [0] * 39 + [1] * 21), per_test)
        self.assertLess(eqrun.distribution_p_value(base, [0] * 38 + [1] * 22), per_test)

    def test_a_shifted_point_mass_costs_the_same_however_far_it_moved(self):
        # Both sides one value, the two values different: the only arrangement of the 2n
        # observations at least as extreme as this one is its mirror, so the p-value is exactly
        # 2/C(2n, n) whatever the gap between the values is. A 1% shift and a 5% shift are equally
        # impossible under the null, which is why `self_check.py`'s `mean_bmi` and `mean_energy`
        # report identical p-values.
        # Two distinct values are tested, so the exact 2/C(2n, n) is Bonferroni-doubled.
        for n in (6, 12, 20):
            self.assertAlmostEqual(4.0 / math.comb(2 * n, n),
                                   eqrun.distribution_p_value([0] * n, [1] * n))

    def test_how_many_seeds_a_shifted_point_mass_needs(self):
        # The arithmetic behind `self_check.POINT_MASS_NEEDS_SEEDS`, pinned here so that a change
        # to the family-wise alpha or to the exact test moves a test rather than a comment.
        # Against a run of ~350 tests: out of reach at six seeds, clear at twelve.
        family = 350
        six = eqrun.distribution_p_value([0] * 6, [1] * 6) * family
        twelve = eqrun.distribution_p_value([0] * 12, [1] * 12) * family
        self.assertGreater(six, eqrun.FAMILY_WISE_ALPHA)
        self.assertLess(twelve, eqrun.FAMILY_WISE_ALPHA / 10)

    def test_it_is_corrected_for_the_number_of_values_tested(self):
        # Three values means three tests, so the smallest p is multiplied by three.
        base = [0] * 10 + [1] * 5 + [2] * 5
        new = [0] * 5 + [1] * 10 + [2] * 5
        smallest = min(eqrun.fisher_exact_two_sided(10, 10, 5, 15),
                       eqrun.fisher_exact_two_sided(5, 15, 10, 10),
                       eqrun.fisher_exact_two_sided(5, 15, 5, 15))
        self.assertAlmostEqual(min(1.0, smallest * 3), eqrun.distribution_p_value(base, new))


def _series(values_by_seed, family=eqrun.MAIN_FAMILY):
    """{seed: {key: value}} for one series, so `compare` has something to chew on.

    The key carries the output family since the harness stopped comparing one file per run: every
    key is `(family, scenario, year, sex, variable)`.
    """
    key = (family, "baseline", 2020, "male", "mean_bmi")
    return {seed: {key: value} for seed, value in values_by_seed.items()}


def _compare(base_values, new_values):
    seeds = list(range(1, len(base_values) + 1))
    outcome = eqrun.Outcome(example="test", seeds=seeds)
    eqrun.compare(_series(dict(zip(seeds, base_values))),
                  _series(dict(zip(seeds, new_values))), seeds, outcome)
    return outcome


class WhichStatisticsAreComparedTest(unittest.TestCase):
    """A continuous series gets two tests; a lattice-valued one gets one exact test instead.

    Before the ninth run a continuous series got five numeric comparisons — the mean, the standard
    deviation and three quantiles — each against `4.5 x` an estimated standard error, and a lattice
    one got the mean and an exact test. The five became two when the rule gained a stated
    false-positive rate: the mean's comparison is Welch's t, the standard deviation's and the
    quantiles' is one robust test of spread, and a lattice series keeps only the exact test,
    because its mean is a function of the same counts the exact test already covers
    (docs/equivalence-method.md 4 and 5, ADR 0048)."""

    def test_a_continuous_series_gets_a_location_and_a_dispersion_test(self):
        base = [20.0 + 0.37 * i for i in range(20)]
        new = [20.0 + 0.37 * i + 0.01 for i in range(20)]
        statistics = {c.statistic for c in _compare(base, new).comparisons}
        self.assertEqual({"location", "dispersion"}, statistics)

    def test_a_lattice_series_gets_the_exact_distribution_test_and_nothing_else(self):
        base = [0.0] * 13 + [1.0] * 7
        new = [0.0] * 11 + [1.0] * 9
        statistics = {c.statistic for c in _compare(base, new).comparisons}
        self.assertEqual({"distribution"}, statistics)

    def test_a_point_mass_with_many_distinct_jumps_is_still_a_lattice(self):
        # Eleven distinct values, so the count rule does not fire — but one value covers more than
        # half the seeds, so the median is that value and is a step function all the same.
        base = [5.0] * 11 + [5.0 + i for i in range(1, 10)]
        new = [5.0] * 12 + [5.0 + i for i in range(1, 9)]
        statistics = {c.statistic for c in _compare(base, new).comparisons}
        self.assertEqual({"distribution"}, statistics)

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


class RegularizedIncompleteBetaTest(unittest.TestCase):
    """The one special function the new rule needs, against closed forms rather than against itself.

    It is the whole of the numerical risk in the change of rule: every p-value the location and
    dispersion tests produce is one call of this, at arguments far out in the tail where a wrong
    answer would be a wrong verdict rather than a wrong-looking number.
    """

    def test_a_uniform_case_is_the_identity(self):
        # I_x(1, 1) = x, because Beta(1, 1) is the uniform distribution.
        for x in (0.01, 0.25, 0.5, 0.9, 0.999):
            self.assertAlmostEqual(x, eqrun.regularized_incomplete_beta(1.0, 1.0, x), places=12)

    def test_the_arcsine_case(self):
        # I_x(1/2, 1/2) = (2/pi) arcsin(sqrt(x)) — the arcsine distribution, which is also the
        # Cauchy's t distribution, so this checks the exact branch df = 1 runs through.
        for x in (0.001, 0.1, 0.5, 0.75, 0.99):
            self.assertAlmostEqual(2.0 / math.pi * math.asin(math.sqrt(x)),
                                   eqrun.regularized_incomplete_beta(0.5, 0.5, x), places=12)

    def test_it_is_symmetric(self):
        # I_x(a, b) + I_(1-x)(b, a) = 1, which is the reflection the implementation takes on one
        # side of the argument and not the other — so this is a test of the seam.
        for a, b, x in ((19.0, 0.5, 0.004), (0.5, 19.0, 0.996), (3.0, 7.0, 0.31)):
            self.assertAlmostEqual(1.0, eqrun.regularized_incomplete_beta(a, b, x)
                                   + eqrun.regularized_incomplete_beta(b, a, 1.0 - x), places=12)

    def test_the_ends_are_exact(self):
        self.assertEqual(0.0, eqrun.regularized_incomplete_beta(2.0, 3.0, 0.0))
        self.assertEqual(1.0, eqrun.regularized_incomplete_beta(2.0, 3.0, 1.0))


class StudentTTest(unittest.TestCase):
    """The two-sided tail, against table values and against a closed form."""

    def test_no_difference_is_perfectly_ordinary(self):
        self.assertAlmostEqual(1.0, eqrun.student_t_two_sided(0.0, 38.0))

    def test_the_five_percent_point_of_twenty_degrees_of_freedom(self):
        # t(0.975, 20) = 2.085963, from any table.
        self.assertAlmostEqual(0.05, eqrun.student_t_two_sided(2.085963, 20.0), places=6)

    def test_one_degree_of_freedom_is_the_cauchy(self):
        for t in (0.5, 1.0, 4.0, 100.0):
            self.assertAlmostEqual(1.0 - 2.0 / math.pi * math.atan(t),
                                   eqrun.student_t_two_sided(t, 1.0), places=12)

    def test_the_far_tail_is_where_the_verdict_lives(self):
        # The level Holm leaves for the smallest p-value in a family of ~45,000 tests is about
        # 2.2e-7, and at 38 degrees of freedom that is t = 6.30. This is the number the whole
        # change of rule turns on: the old allowance asked for 4.5 standard errors and this asks
        # for 6.30 of the same estimated standard error, and the difference between them is exactly
        # the noise in the estimate that the old rule ignored.
        self.assertLess(eqrun.student_t_two_sided(6.31, 38.0), 2.2e-7)
        self.assertGreater(eqrun.student_t_two_sided(6.29, 38.0), 2.2e-7)
        self.assertAlmostEqual(6.24e-05, eqrun.student_t_two_sided(4.5, 38.0), places=7)


class WelchTest(unittest.TestCase):
    """The location test, including the two degenerate cases this model's output is full of."""

    def test_a_sample_against_itself_is_perfectly_ordinary(self):
        values = [20.0 + 0.37 * i for i in range(20)]
        _, _, p = eqrun.welch_t_test(values, values)
        self.assertAlmostEqual(1.0, p)

    def test_a_case_whose_t_and_degrees_of_freedom_can_be_done_by_hand(self):
        # Two samples of five, each with sample variance 2.5, shifted by 2. The squared standard
        # error is 2.5/5 + 2.5/5 = 1, so t = 2 exactly; Welch-Satterthwaite gives
        # 1 / (0.5^2/4 + 0.5^2/4) = 8 degrees of freedom, which is 2n - 2 as it must be when the
        # two variances and the two sample sizes agree. The tail at (2, 8) is 0.0805 from a table.
        t, df, p = eqrun.welch_t_test([1.0, 2.0, 3.0, 4.0, 5.0], [3.0, 4.0, 5.0, 6.0, 7.0])
        self.assertAlmostEqual(2.0, t, places=12)
        self.assertAlmostEqual(8.0, df, places=12)
        self.assertAlmostEqual(0.0805, p, places=4)

    def test_two_constants_that_agree_are_not_a_comparison(self):
        # A band mean calibration pins: one value in every seed, on both sides. There is nothing
        # to test and nothing wrong.
        _, _, p = eqrun.welch_t_test([25.541647] * 20, [25.541647] * 20)
        self.assertEqual(1.0, p)

    def test_two_constants_that_differ_are_certain(self):
        # The same, except that the two implementations disagree deterministically. Whether that
        # matters is then the printed-precision floor's question, not this function's.
        _, _, p = eqrun.welch_t_test([25.541647] * 20, [25.541648] * 20)
        self.assertEqual(0.0, p)

    def test_a_sample_too_small_to_have_a_variance_is_not_tested(self):
        self.assertEqual((0.0, 0.0, 1.0), eqrun.welch_t_test([1.0], [2.0]))

    def test_it_uses_the_distribution_the_estimated_error_really_has(self):
        # The point of the change, as one number. Two samples whose difference is exactly 4.5 of
        # their own estimated standard error: the old rule called that the threshold; the t
        # distribution on ~38 degrees of freedom calls it p = 5.8e-5, which Holm over a real
        # family does not come close to rejecting.
        left = list(_STANDARDISED)
        offset = 4.5 * math.sqrt(2.0 * statistics_variance(left) / 20)
        right = [value + offset for value in left]
        _, _, p = eqrun.welch_t_test(left, right)
        self.assertGreater(p, 1e-5)
        self.assertLess(p, 1e-3)


def statistics_variance(values):
    mean = sum(values) / len(values)
    return sum((value - mean) ** 2 for value in values) / (len(values) - 1)


# Twenty values with mean 0 and variance 1, so a test can build a sample with an exact offset in
# units of its own standard error without depending on a random generator.
_STANDARDISED = [(i - 9.5) / 5.916079783099616 for i in range(20)]


class HolmTest(unittest.TestCase):
    """The multiplicity correction, which is the only place a verdict is decided."""

    def test_one_test_is_its_own_adjustment(self):
        self.assertEqual([0.004], eqrun.holm_adjusted([0.004]))

    def test_the_smallest_is_multiplied_by_the_whole_family(self):
        adjusted = eqrun.holm_adjusted([0.9, 0.001, 0.5, 0.4])
        self.assertAlmostEqual(0.004, adjusted[1])

    def test_it_is_a_step_down_and_therefore_monotone(self):
        raw = [0.001, 0.02, 0.03, 0.7]
        adjusted = eqrun.holm_adjusted(raw)
        self.assertEqual(sorted(adjusted), adjusted)
        for one, other in zip(raw, adjusted):
            self.assertGreaterEqual(other, one)

    def test_a_later_test_inherits_an_earlier_one_that_could_not_be_rejected(self):
        # The step-down: the second smallest is (m-1) * p = 3 * 0.2 = 0.6, but the smallest is
        # already 4 * 0.2 = 0.8, and Holm cannot reject a larger p-value than one it kept.
        self.assertEqual([0.8, 0.8, 0.8, 0.8], eqrun.holm_adjusted([0.2, 0.2, 0.2, 0.2]))

    def test_nothing_is_ever_adjusted_past_certainty(self):
        self.assertEqual([1.0] * 3, eqrun.holm_adjusted([0.9, 0.95, 1.0]))

    def test_the_order_given_is_the_order_returned(self):
        self.assertEqual([1.0, 0.004, 1.0, 1.0], [round(value, 9) for value in
                                                  eqrun.holm_adjusted([0.9, 0.001, 0.5, 0.4])])


class TheRateIsWhatItSaysTest(unittest.TestCase):
    """The location test's nominal level is its actual level, on samples with a known distribution.

    This is the unit-test-scale version of `calibrate.py --mode null`, and it is here because the
    expensive version needs a build and twenty minutes: at this scale it cannot see the far tail
    Holm operates in, but it would catch a test whose level was wrong by a factor.

    The generator is seeded, so this is a fixed arithmetic fact rather than a flaky test.
    """

    def test_the_five_percent_level_rejects_about_five_percent(self):
        generator = random.Random(20260919)
        rejected = 0
        trials = 2000
        for _ in range(trials):
            left = [generator.gauss(0.0, 1.0) for _ in range(20)]
            right = [generator.gauss(0.0, 1.0) for _ in range(20)]
            _, _, p = eqrun.welch_t_test(left, right)
            rejected += p < 0.05
        # 100 expected, and the standard deviation of the count is sqrt(2000*0.05*0.95) = 9.7.
        self.assertGreater(rejected, 100 - 4 * 9.7)
        self.assertLess(rejected, 100 + 4 * 9.7)

    def test_a_heavy_tailed_sample_does_not_reject_more_often(self):
        # The model's output is not normal, so the level has to survive a sample that is not
        # either. A Student t on 3 degrees of freedom has no fourth moment at all.
        generator = random.Random(20260920)
        rejected = 0
        trials = 2000
        for _ in range(trials):
            left = [generator.gauss(0.0, 1.0) / math.sqrt(generator.gammavariate(1.5, 1.0) / 1.5)
                    for _ in range(20)]
            right = [generator.gauss(0.0, 1.0) / math.sqrt(generator.gammavariate(1.5, 1.0) / 1.5)
                     for _ in range(20)]
            _, _, p = eqrun.welch_t_test(left, right)
            rejected += p < 0.05
        self.assertLess(rejected, 100 + 4 * 9.7)


class ThePrintedPrecisionFloorTest(unittest.TestCase):
    """Nothing can fail on a difference the baseline's own output cannot express.

    Two rules say that, at the same precision and in that order, and which of them a given series
    meets is worth having written down:

      * `lattice_keys` buckets both samples at the baseline's six printed digits *before* the
        series is classified, so two figures the baseline cannot print apart are one value. A
        series whose whole spread is below that precision therefore has one bucket, is classified
        as a point mass, and its exact test has nothing to compare;
      * the floor in `compare` then waives any remaining test whose difference is below the same
        precision.

    Under the rule this replaced the second was load-bearing — it *was* the whole allowance for
    every pinned series. Under this one it is belt and braces, and the arithmetic says so: for a
    difference to be significant at the level Holm leaves while staying below the floor, the
    sample's own standard deviation has to be smaller than the floor, and such a sample is one
    bucket wide and has already gone to the exact test. It is kept because the guarantee is worth
    stating unconditionally, and because it can only ever remove failures, so the family-wise rate
    stays an upper bound.
    """

    def test_a_series_narrower_than_the_printed_precision_has_nothing_to_compare(self):
        # Both sides constant, differing in the twelfth digit: one bucket, so the exact test sees
        # one value and returns certainty.
        outcome = _compare([25.541647043865236] * 20, [25.541647043865240] * 20)
        self.assertEqual({"distribution"}, {c.statistic for c in outcome.comparisons})
        self.assertEqual([1.0], [c.p_value for c in outcome.comparisons])
        self.assertTrue(all(c.passed for c in outcome.comparisons))

    def test_a_difference_the_baseline_can_print_does_fail(self):
        # 1% of a pinned aggregate: the smallest difference anybody would call one, against the
        # tightest test the method has. This is the synthetic self-check's `mean_bmi` case.
        outcome = _compare([25.541647] * 20, [25.541647 * 1.01] * 20)
        self.assertFalse(all(c.passed for c in outcome.comparisons))

    def test_the_floor_waives_a_test_however_certain_it_is(self):
        # The guarantee itself, stated on one comparison rather than hunted for in a series: a
        # difference at or below the floor cannot fail even when the p-value is zero.
        key = (eqrun.MAIN_FAMILY, "baseline", 2020, "male", "mean_bmi")
        waived = eqrun.Comparison(key=key, statistic="location", baseline=25.5, new=25.5 + 1e-5,
                                  difference=1e-5, floor=2.55e-4, p_value=0.0, adjusted=0.0)
        self.assertTrue(waived.below_floor)
        self.assertTrue(waived.passed)

        just_over = eqrun.Comparison(key=key, statistic="location", baseline=25.5, new=25.6,
                                     difference=0.1, floor=2.55e-4, p_value=0.0, adjusted=0.0)
        self.assertFalse(just_over.below_floor)
        self.assertFalse(just_over.passed)

    def test_the_distribution_test_is_not_subject_to_the_floor(self):
        # Its statistic is a modal share, and two point masses at different values both have a
        # modal share of 1 — so a floor on that difference would waive every point-mass comparison
        # there is, including the 1% shift above.
        outcome = _compare([25.541647] * 20, [25.541647 * 1.01] * 20)
        self.assertTrue(all(c.floor is None for c in outcome.comparisons))
        self.assertFalse(any(c.below_floor for c in outcome.comparisons))


class OneFamilyOneRateTest(unittest.TestCase):
    """Every test in a run is corrected together, and one failure fails the run."""

    def test_holm_corrects_over_every_test_in_the_run(self):
        seeds = list(range(1, 21))
        base = {seed: {} for seed in seeds}
        mine = {seed: {} for seed in seeds}
        for year in range(2020, 2030):
            key = (eqrun.MAIN_FAMILY, "baseline", year, "male", "mean_bmi")
            for index, seed in enumerate(seeds):
                base[seed][key] = 20.0 + 0.37 * index
                mine[seed][key] = 20.0 + 0.37 * index + 0.01
        outcome = eqrun.Outcome(example="test", seeds=seeds)
        eqrun.compare(base, mine, seeds, outcome)

        # Ten years, two tests each: the family is twenty, and the smallest p-value is multiplied
        # by twenty rather than by one.
        self.assertEqual(20, len(outcome.comparisons))
        smallest = min(outcome.comparisons, key=lambda c: c.p_value)
        self.assertAlmostEqual(min(1.0, 20 * smallest.p_value), smallest.adjusted)

    def test_a_verdict_is_not_decided_before_the_family_is_known(self):
        comparison = eqrun.Comparison(key=(eqrun.MAIN_FAMILY, "baseline", 2020, "male", "x"),
                                      statistic="location", baseline=1.0, new=2.0,
                                      difference=1.0, floor=0.0, p_value=0.0)
        self.assertFalse(comparison.below_floor)
        self.assertIsNone(comparison.adjusted)
        self.assertTrue(comparison.passed, "an uncorrected comparison has no verdict to give")

    def test_there_is_no_failure_budget(self):
        # The eighth run spent a budget of three on one example. `report` has no parameter that
        # could hold one now, and one failure fails the run (docs/equivalence-method.md 6).
        seeds = list(range(1, 21))
        outcome = eqrun.Outcome(example="test", seeds=seeds)
        eqrun.compare(_series(dict(zip(seeds, [25.541647] * 20))),
                      _series(dict(zip(seeds, [25.541647 * 1.01] * 20))), seeds, outcome)
        self.assertEqual(1, sum(1 for c in outcome.comparisons if not c.passed))
        self.assertFalse(eqrun.report(outcome, verbose=False))


def _rate_series(case_counts, head_counts):
    """{seed: {rate key, count key}} for a rate whose numerator and denominator are both given.

    This is the shape the reduction actually produces for a disease rate: the value is
    `cases / head count` and the head count is beside it as its own variable.
    """
    # `prevalence_`, not `incidence_`: an incidence is undefined in the run's first year and these
    # samples are all one year, so the comparison would be skipped before the detector saw it.
    rate_key = (eqrun.MAIN_FAMILY, "baseline", 2020, "male", "prevalence_gout")
    count_key = (eqrun.MAIN_FAMILY, "baseline", 2020, "male", "count")
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
    return [c for c in outcome.comparisons if c.key[4] == variable]


class LatticeOnTheNumeratorTest(unittest.TestCase):
    """The detector asks its question of the case count, not of the rate, which was backlog item 11.

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
        self.assertEqual({"distribution"},
                         {c.statistic for c in _of(outcome, "prevalence_gout")})

    def test_the_same_series_read_as_a_rate_is_not_one(self):
        # The old behaviour, kept as a test so that what changed is written down: with no head
        # count beside it the detector can only look at the rate, and it sees a continuous series.
        cases = ([0] * 20 + [1] * 30 + [2] * 10)
        rates = [c / h for c, h in zip(cases, self.HEADS)]
        statistics = {c.statistic for c in _compare(rates, rates).comparisons}
        self.assertEqual({"location", "dispersion"}, statistics)

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
        self.assertEqual({"distribution"}, {c.statistic for c in compared})
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
        key = (eqrun.MAIN_FAMILY, "baseline", 2020, "male", "mean_bmi")
        count_key = (eqrun.MAIN_FAMILY, "baseline", 2020, "male", "count")
        seeds = list(range(1, 21))
        side = {seed: {key: values[seed - 1], count_key: float(heads[seed - 1])} for seed in seeds}
        outcome = eqrun.Outcome(example="test", seeds=seeds)
        eqrun.compare(side, side, seeds, outcome)
        self.assertEqual({"location", "dispersion"},
                         {c.statistic for c in _of(outcome, "mean_bmi")})

    def test_a_calibrated_mean_that_is_the_same_in_every_seed_is_still_a_point_mass(self):
        # The other direction: a band mean calibration pins takes one value in every seed, and the
        # cohort size is the same in every seed too, so its numerator is one value as well. It has
        # to stay on the exact-distribution path — the synthetic self-check's detection of a 1%
        # shift in `mean_bmi` runs through it (docs/equivalence-method.md 7.2).
        key = (eqrun.MAIN_FAMILY, "baseline", 2020, "male", "mean_bmi")
        count_key = (eqrun.MAIN_FAMILY, "baseline", 2020, "male", "count")
        seeds = list(range(1, 21))
        side = {seed: {key: 25.541647, count_key: 3146.0} for seed in seeds}
        outcome = eqrun.Outcome(example="test", seeds=seeds)
        eqrun.compare(side, side, seeds, outcome)
        self.assertEqual({"distribution"},
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

    HEADER = ["source", "run", "time", "gender_name", "index_id", "count", "deaths", "mean_bmi",
              "normal_weight", "over_weight", "obese_weight", "above_weight"]
    ROWS = [
        ["baseline", 1, 2020, "male", 30, 10, 1, 25.0, 6, 3, 1, 4],
        ["baseline", 1, 2020, "male", 31, 30, 2, 27.0, 12, 10, 8, 18],
        ["baseline", 1, 2020, "male", 32, 0, 0, 0.0, 0, 0, 0, 0],
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

    def test_the_weight_categories_are_head_counts_and_are_summed(self):
        """They were count-weighted until this run, which gave the average band's count.

        18 people of the 40 are above normal weight; the reduction used to report
        (10*4 + 30*18) / 40 = 14.5, which is neither a count nor a proportion. This is the
        defect this run closed and nothing else would catch: both
        implementations were reduced identically, so no comparison was ever wrong about it.
        """
        reduced = eqrun.reduce_result(self._write(self.ROWS))
        self.assertAlmostEqual(18.0, reduced[("baseline", 2020, "male", "normal_weight")])
        self.assertAlmostEqual(13.0, reduced[("baseline", 2020, "male", "over_weight")])
        self.assertAlmostEqual(9.0, reduced[("baseline", 2020, "male", "obese_weight")])
        self.assertAlmostEqual(22.0, reduced[("baseline", 2020, "male", "above_weight")])
        # And they still partition the population the way the result file does.
        self.assertAlmostEqual(reduced[("baseline", 2020, "male", "count")],
                               reduced[("baseline", 2020, "male", "normal_weight")]
                               + reduced[("baseline", 2020, "male", "over_weight")]
                               + reduced[("baseline", 2020, "male", "obese_weight")])

    def test_a_summed_variable_is_its_own_numerator_for_the_lattice_rule(self):
        """Which is why the four have to be in SUMMED_VARIABLES rather than merely summed.

        `numerator_series` reconstructs a count-weighted variable's numerator as value * count.
        For a head count the reduced value IS the numerator, and multiplying it by the head count
        again would classify a series of 18s as a series of 720s — a different lattice.
        """
        for variable in ("normal_weight", "over_weight", "obese_weight", "above_weight"):
            self.assertIn(variable, eqrun.SUMMED_VARIABLES)

    def test_an_excluded_band_is_left_out_of_both_the_weight_and_the_total(self):
        excluded = {eqrun.band_key("baseline", 2020, "male", 31)}
        reduced = eqrun.reduce_result(self._write(self.ROWS), excluded)
        self.assertAlmostEqual(10.0, reduced[("baseline", 2020, "male", "count")])
        self.assertAlmostEqual(25.0, reduced[("baseline", 2020, "male", "mean_bmi")])
        self.assertAlmostEqual(6.0, reduced[("baseline", 2020, "male", "normal_weight")])

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


def _named_series(variable, values_by_seed, year=2020, family=eqrun.MAIN_FAMILY):
    key = (family, "baseline", year, "male", variable)
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
        fixed = {seed: {(eqrun.MAIN_FAMILY, "intervention", year, "male", "mean_bmi"): value
                        for year, value in fixed_by_year.items()} for seed in seeds}
        compatible = {seed: {(eqrun.MAIN_FAMILY, "intervention", year, "male", "mean_bmi"): value
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
        fixed = {1: {(eqrun.MAIN_FAMILY, "intervention", 2020, "male", "mean_bmi"): 25.0,
                     (eqrun.MAIN_FAMILY, "intervention", 2020, "male", "mean_new_thing"): 1.0}}
        compatible = {1: {(eqrun.MAIN_FAMILY, "intervention", 2020, "male", "mean_bmi"): 24.0}}
        impact = eqrun.measure_impact(fixed, compatible, [1], "all")
        self.assertEqual(["mean_bmi"], [s.variable for s in impact.series])

    def test_nothing_in_the_impact_can_fail_a_run(self):
        # It is a measurement, not a gate (ADR 0041). `report_impact` returns nothing and
        # `report`'s verdict is computed before it is called.
        outcome = eqrun.Outcome(example="test", seeds=[1, 2, 3])
        fixed, compatible, seeds = self._reductions({2020: 99.0}, {2020: 1.0})
        outcome.impact = eqrun.measure_impact(fixed, compatible, seeds, "all")
        self.assertTrue(eqrun.report(outcome, verbose=False))


class OutputFamiliesTest(unittest.TestCase):
    """Every CSV a run writes is compared, and a family only one side has is a failure.

    Until this run the harness reduced the whole-population file and `find_result_csv` existed to
    *exclude* the others. Forty-five columns of every income-stratified file were identically zero
    here and filled in the baseline for as long as this build has written them, and the only
    comparison this project has did not look at them at all (docs/equivalence.md).
    """

    def test_a_derived_file_is_recognised_by_its_category_suffix(self):
        self.assertEqual("LowIncome", eqrun.family_of(Path("result_2026-01-02_LowIncome.csv")))
        self.assertEqual("UpperMiddleIncome",
                         eqrun.family_of(Path("r_2026-01-02_UpperMiddleIncome.csv")))
        self.assertEqual("IndividualIDTracking",
                         eqrun.family_of(Path("r_2026-01-02_IndividualIDTracking.csv")))

    def test_a_configured_output_name_ending_in_a_capital_is_not_a_family(self):
        # The rule that matched the *shape* of a CamelCase suffix would call this one "B". The
        # second fixture pack's configured output name is exactly this, so the rule that looks
        # right was wrong on a file this repository generates.
        self.assertEqual(eqrun.MAIN_FAMILY, eqrun.family_of(Path("synthland_2026-01-02_B.csv")))
        self.assertEqual(eqrun.MAIN_FAMILY, eqrun.family_of(Path("result.csv")))

    def test_every_csv_in_a_folder_is_found_and_named(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            for name in ("result_2026-01-02.csv", "result_2026-01-02_LowIncome.csv",
                         "result_2026-01-02_HighIncome.csv"):
                (folder / name).write_text("source,run,time,gender_name,index_id,count\n")
            (folder / "result_2026-01-02.json").write_text("{}")

            families = eqrun.result_families(folder)
            self.assertEqual({eqrun.MAIN_FAMILY, "LowIncome", "HighIncome"}, set(families))

    def test_two_files_claiming_the_whole_population_family_is_an_error(self):
        # The failure mode a new derived file with an unknown suffix produces, and it is loud on
        # purpose: silently treating it as a second main file is how a family goes unnoticed.
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "result_2026-01-02.csv").write_text("count\n")
            (folder / "result_2026-01-02_Whatever.csv").write_text("count\n")
            with self.assertRaises(RuntimeError):
                eqrun.result_families(folder)

    def test_the_family_is_part_of_every_key(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            header = "source,run,time,gender_name,index_id,count,mean_bmi\n"
            (folder / "r.csv").write_text(header + "baseline,1,2020,male,30,10,25.0\n")
            (folder / "r_LowIncome.csv").write_text(header + "baseline,1,2020,male,30,4,22.0\n")

            reduced = eqrun.reduce_families(eqrun.result_families(folder))
            self.assertAlmostEqual(25.0, reduced[("result", "baseline", 2020, "male", "mean_bmi")])
            self.assertAlmostEqual(22.0,
                                   reduced[("LowIncome", "baseline", 2020, "male", "mean_bmi")])
            self.assertAlmostEqual(4.0, reduced[("LowIncome", "baseline", 2020, "male", "count")])

    def test_a_stratified_rate_is_reconstructed_from_its_own_head_count(self):
        # The lattice detector rebuilds a rate's numerator as `value * count`. Reading the whole
        # population's head count for a stratum's rate would give it a number two or three times
        # too large and classify the series wrongly.
        seeds = list(range(1, 21))

        def side(stratum_cases):
            return {seed: {("LowIncome", "baseline", 2020, "male", "prevalence_gout"):
                               stratum_cases[seed - 1] / 400.0,
                           ("LowIncome", "baseline", 2020, "male", "count"): 400.0,
                           ("result", "baseline", 2020, "male", "count"): 3146.0}
                    for seed in seeds}

        outcome = eqrun.Outcome(example="test", seeds=seeds)
        cases = [0] * 13 + [1] * 7
        eqrun.compare(side(cases), side(cases), seeds, outcome)
        stratified = [c for c in outcome.comparisons
                      if c.key[0] == "LowIncome" and c.key[4] == "prevalence_gout"]
        self.assertEqual({"distribution"}, {c.statistic for c in stratified})

    def test_a_family_only_the_baseline_writes_is_a_failure(self):
        failures = eqrun.check_families({"result": {}, "MiddleIncome": {}}, {"result": {}})
        self.assertEqual(1, len(failures))
        self.assertIn("MiddleIncome", failures[0])

    def test_a_family_only_this_build_writes_is_a_failure(self):
        failures = eqrun.check_families({"result": {}}, {"result": {}, "MiddleIncome": {}})
        self.assertEqual(1, len(failures))
        self.assertIn("this build writes", failures[0])

    def test_the_recorded_baseline_only_family_is_excluded_while_its_file_is_empty(self):
        failures = eqrun.check_families(
            {"result": {}, "IndividualIDTracking": {"empty_file": True}}, {"result": {}})
        self.assertEqual([], failures)

    def test_the_exclusion_disarms_itself_when_the_baseline_fills_the_file(self):
        # The same discipline BASELINE_DOES_NOT_COMPUTE follows one level down: an exclusion whose
        # premise stops holding turns back into a failure and says why.
        failures = eqrun.check_families(
            {"result": {}, "IndividualIDTracking": {"empty_file": False}}, {"result": {}})
        self.assertEqual(1, len(failures))
        self.assertIn("no longer holds", failures[0])

    def test_a_family_failure_fails_the_run(self):
        outcome = eqrun.Outcome(example="test", seeds=[1, 2, 3])
        outcome.family_failures = ["MiddleIncome: the baseline writes this and we do not"]
        self.assertFalse(eqrun.report(outcome, verbose=False))

    def test_a_reference_without_a_family_column_is_refused_by_name(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "old.csv.gz"
            with gzip.open(path, "wt", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(["seed", "scenario", "year", "sex", "variable", "value"])
                writer.writerow([1, "baseline", 2020, "male", "mean_bmi", "25.0"])
            with self.assertRaises(SystemExit) as raised:
                eqrun.read_reference(path)
            self.assertIn("family", str(raised.exception))

    def test_a_reference_round_trips_through_the_family_keyed_format(self):
        reduced = {("result", "baseline", 2020, "male", "mean_bmi"): 25.5,
                   ("LowIncome", "baseline", 2020, "female", "count"): 12.0}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "reference.csv.gz"
            eqrun.write_reference(path, eqrun.reduced_to_rows(reduced, seed=7))
            self.assertEqual({7: reduced}, eqrun.read_reference(path))
