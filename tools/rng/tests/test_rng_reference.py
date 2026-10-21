"""Tests for tools/rng/rng_reference.py and gen_rng_kat.py (plan A04).

Run from the repository root: python -m unittest discover -s tools/rng/tests -v
"""

import io
import math
import statistics
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import gen_rng_kat as gen  # noqa: E402
import rng_reference as r  # noqa: E402


def ulp_distance(a: float, b: float) -> int:
    """Distance in units in the last place between two finite doubles of the same sign."""
    return abs(r.to_bits(a) - r.to_bits(b))


class Hashing(unittest.TestCase):
    def test_splitmix64_published_sequence(self):
        # SplitMix64 seeded with 0: state advances by the golden gamma, output = mix64(state).
        expected = [0xE220A8397B1DCDAF, 0x6E789E6AA1B965F4, 0x06C45D188009454F]
        got = [r.mix64(k * r.GOLDEN_GAMMA) for k in (1, 2, 3)]
        self.assertEqual(got, expected)

    def test_fnv1a_published_values(self):
        self.assertEqual(r.hash_id(""), 0xCBF29CE484222325)
        self.assertEqual(r.hash_id("a"), 0xAF63DC4C8601EC8C)

    def test_keys_differ_by_every_argument(self):
        base = r.stage_key(1, "c01_allocate", 0)
        self.assertNotEqual(base, r.stage_key(2, "c01_allocate", 0))
        self.assertNotEqual(base, r.stage_key(1, "c02_seed", 0))
        self.assertNotEqual(base, r.stage_key(1, "c01_allocate", 1))
        self.assertNotEqual(r.bits64(base, 0, 1), r.bits64(base, 1, 0))

    def test_unit_intervals(self):
        self.assertEqual(r.to_unit01(0), 0.0)
        self.assertLess(r.to_unit01(r.MASK64), 1.0)
        self.assertGreater(r.to_open01(0), 0.0)
        self.assertLess(r.to_open01(r.MASK64), 1.0)

    def test_stream_equals_stateless_draws(self):
        key = r.stage_key(7, "test", 0)
        s = r.RngStream(key, 42)
        self.assertEqual([s.uniform01() for _ in range(5)],
                         [r.uniform01(key, 42, k) for k in range(5)])


class DetMath(unittest.TestCase):
    def test_exp_within_one_ulp_of_math(self):
        key = r.stage_key(0, "test.exp", 0)
        for i in range(20000):
            x = r.uniform(key, i, 0, -740.0, 709.0)
            self.assertLessEqual(ulp_distance(r.det_exp(x), math.exp(x)), 1, x)

    def test_log_within_one_ulp_of_math(self):
        key = r.stage_key(0, "test.log", 0)
        for i in range(20000):
            x = math.exp(r.uniform(key, i, 0, -744.0, 709.0))
            self.assertLessEqual(ulp_distance(r.det_log(x), math.log(x)), 1, x)

    def test_special_values(self):
        self.assertEqual(r.det_exp(0.0), 1.0)
        self.assertEqual(r.det_exp(math.inf), math.inf)
        self.assertEqual(r.det_exp(-math.inf), 0.0)
        self.assertEqual(r.det_exp(710.0), math.inf)
        self.assertEqual(r.det_exp(-746.0), 0.0)
        self.assertTrue(math.isnan(r.det_exp(math.nan)))
        self.assertEqual(r.det_log(1.0), 0.0)
        self.assertEqual(r.det_log(0.0), -math.inf)
        self.assertEqual(r.det_log(math.inf), math.inf)
        self.assertTrue(math.isnan(r.det_log(-1.0)))
        self.assertTrue(math.isnan(r.det_log(math.nan)))

    def test_log_factorial_matches_lgamma(self):
        for k in list(range(30)) + [100, 1000, 10 ** 6, 10 ** 12]:
            want = math.lgamma(k + 1)
            self.assertLessEqual(abs(r.log_factorial(k) - want), 1e-13 * max(1.0, want), k)


class Distributions(unittest.TestCase):
    def test_inverse_normal_cdf_matches_statistics(self):
        nd = statistics.NormalDist()
        key = r.stage_key(0, "test.inverse_normal_cdf", 0)
        for i in range(20000):
            p = r.uniform_open01(key, i, 0)
            want = nd.inv_cdf(p)
            self.assertLessEqual(abs(r.inverse_normal_cdf(p) - want), 1e-14 * max(1.0, abs(want)))

    def test_poisson_mean(self):
        key = r.stage_key(0, "test.poisson", 0)
        for lam in (0.5, 4.0, 30.0, 1000.0):
            n = 4000
            mean = sum(r.poisson(r.RngStream(key, i), lam) for i in range(n)) / n
            self.assertLess(abs(mean - lam), 5.0 * math.sqrt(lam / n), lam)

    def test_weighted_bins_skip_zero_weights(self):
        bins = r.WeightedBins([0.0, 1.0, 0.0, 2.0, 0.0])
        self.assertEqual(bins.sample(0.0), 1)
        self.assertEqual(bins.sample(1.0 / 3.0), 3)
        self.assertEqual(bins.sample(1.0 - 2.0 ** -53), 3)


class Generator(unittest.TestCase):
    def test_committed_file_is_current(self):
        self.assertEqual(gen.main(["--check"]), 0)

    def test_check_detects_a_stale_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "rng_kat.inc"
            self.assertEqual(gen.main(["--out", str(out)]), 0)
            self.assertEqual(gen.main(["--out", str(out), "--check"]), 0)
            out.write_text("stale", encoding="utf-8")
            with redirect_stderr(io.StringIO()):
                self.assertEqual(gen.main(["--out", str(out), "--check"]), 1)


if __name__ == "__main__":
    unittest.main()
