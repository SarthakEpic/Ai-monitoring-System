import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "python"))

from selective_risk_certificate import certify_selective_risk, exact_binomial_bounds


class ExactBinomialCertificateTests(unittest.TestCase):
    def test_zero_errors_has_exact_upper_bound(self):
        _, upper = exact_binomial_bounds(0, 100)
        self.assertAlmostEqual(upper, 0.029513, places=5)

    def test_bounds_are_one_sided_and_ordered(self):
        lower, upper = exact_binomial_bounds(97, 100)
        self.assertGreater(lower, 0.90)
        self.assertLess(upper, 1.0)
        self.assertLess(lower, upper)

    def test_invalid_counts_fail_closed(self):
        certificate = certify_selective_risk(11, 10, 10, 0.01, 0.70)
        self.assertFalse(certificate.accepted)
        self.assertEqual(certificate.error_upper_bound, 1.0)


if __name__ == "__main__":
    unittest.main()
