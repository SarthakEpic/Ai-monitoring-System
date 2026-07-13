import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "python"))

from conformal_runtime import SplitConformalBinary
from drift_monitor import DriftLevel, assess_drift
from ood_ensemble import assess_ood
from probability_calibration import select_calibrator
from risk_coverage_report import build_risk_coverage_report
from selective_risk_certificate import certify_selective_risk


class Phase2ReliabilityTests(unittest.TestCase):
    def test_calibration_uses_only_provided_calibration_data(self):
        probabilities = [0.9, 0.8, 0.7, 0.2, 0.1, 0.3]
        labels = [1, 1, 1, 0, 0, 0]
        calibrator = select_calibrator(probabilities, labels)
        self.assertLess(calibrator.result.calibration_log_loss, 1.0)
        self.assertGreater(calibrator.transform(0.9), calibrator.transform(0.1))

    def test_conformal_abstains_on_ambiguous_prediction(self):
        conformal = SplitConformalBinary.fit([0.9, 0.8, 0.2, 0.1], [1, 1, 0, 0])
        result = conformal.predict(0.5)
        self.assertTrue(result.abstain)

    def test_certificate_needs_error_and_coverage_bounds(self):
        passed = certify_selective_risk(0, 100, 100, 0.10, 0.80)
        self.assertTrue(passed.accepted)
        failed = certify_selective_risk(5, 10, 100, 0.10, 0.80)
        self.assertFalse(failed.accepted)

    def test_ood_and_drift_fail_closed(self):
        ood = assess_ood({"cpu": 100}, {"cpu": 10}, {"cpu": 5}, 0.2, 0.3, False)
        self.assertFalse(ood.accepted)
        drift = assess_drift({"cpu": 10}, {"cpu": 5}, {"cpu": 60}, 0.9, 0.4)
        self.assertEqual(drift.level, DriftLevel.CERTIFICATE_INVALID)

    def test_report_is_selective_and_auditable(self):
        report = build_risk_coverage_report([0.9, 0.8, 0.2, 0.1], [1, 1, 0, 0], [0.5, 0.8])
        self.assertEqual(len(report), 2)
        self.assertIn("coverage_lower_bound", report[0])


if __name__ == "__main__":
    unittest.main()
