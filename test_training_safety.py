import unittest

from train_model import _build_readiness_report


class LegacyTrainingSafetyTests(unittest.TestCase):
    def test_legacy_pipeline_cannot_become_production_candidate(self):
        report = _build_readiness_report(
            samples=2000,
            class_distribution={"NORMAL": 500, "WARNING": 500, "CRITICAL": 500, "RECOVERY": 500},
            report_dict={
                "NORMAL": {"recall": 1.0},
                "WARNING": {"recall": 1.0},
                "CRITICAL": {"recall": 1.0},
                "RECOVERY": {"recall": 1.0},
            },
        )
        self.assertEqual(report["status"], "research_only")
        self.assertTrue(any("v3 episode pipeline" in blocker for blocker in report["blockers"]))


if __name__ == "__main__":
    unittest.main()
