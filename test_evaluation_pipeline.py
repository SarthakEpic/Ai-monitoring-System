import unittest

from python.baselines import deterministic_resource_rule, ewma_probability, persistence
from python.dataset_schema_v3 import ValidationError
from python.evaluation_metrics import evaluate_binary_episode_predictions, risk_coverage_curve
from python.label_validation import validate_outcome_labels
from python.slice_report import evaluate_slices


def label(episode_id, outcome, provenance="MEASURED_QOE"):
    return {
        "schema_version": 3,
        "monotonic_timestamp_ms": 1,
        "wall_timestamp_utc": "2026-07-13T00:00:00Z",
        "device_id": "device-a",
        "session_id": "session-a",
        "episode_id": episode_id,
        "windows_build_family": "WINDOWS_11",
        "hardware": {"ram_tier": "4GB"},
        "collector_health": {"qoe": "HEALTHY"},
        "feature_source_versions": {"qoe": "v3"},
        "provenance": provenance,
        "primary_outcome": outcome,
        "label_confidence": 0.9,
        "qoe_evidence": {"message_loop_response": "observed"},
    }


class EvaluationPipelineTests(unittest.TestCase):
    def test_weak_labels_cannot_enter_certification(self):
        with self.assertRaises(ValidationError):
            validate_outcome_labels([label("e1", "USER_IMPACT_EVENT", "WEAK_HEURISTIC")], for_certification=True)

    def test_contradictory_episode_labels_are_rejected(self):
        with self.assertRaises(ValidationError):
            validate_outcome_labels([
                label("e1", "USER_IMPACT_EVENT"),
                label("e1", "NO_USER_IMPACT_EVENT"),
            ])

    def test_binary_metrics_report_calibration_and_bounds(self):
        report = evaluate_binary_episode_predictions([0, 0, 1, 1], [0.05, 0.2, 0.8, 0.95])
        self.assertEqual((report.true_negative, report.false_positive, report.false_negative, report.true_positive), (2, 0, 0, 2))
        self.assertGreater(report.balanced_accuracy, 0.99)
        self.assertGreater(report.critical_recall_wilson_lower, 0.30)
        self.assertEqual(len(risk_coverage_curve([0.05, 0.2, 0.8, 0.95], [0, 0, 1, 1])), 10)

    def test_baselines_are_explicit_research_comparisons(self):
        prediction = deterministic_resource_rule({"resources": {"cpu_percent": 95, "memory_percent": 95, "disk_free_percent": 1}})
        self.assertEqual(prediction.label, 1)
        self.assertEqual(persistence(None, 0.2).label, 0)
        self.assertAlmostEqual(ewma_probability([0.0, 1.0], 0.5), 0.5)

    def test_slice_report_keeps_device_and_workload_results_separate(self):
        report = evaluate_slices([
            {"device_id": "d1", "workload_class": "IDE", "application_family": "EDITOR", "label": 0, "probability": 0.1},
            {"device_id": "d2", "workload_class": "GAME", "application_family": "GAME", "label": 1, "probability": 0.9},
        ])
        self.assertEqual(set(report["device_id"]), {"d1", "d2"})
        self.assertEqual(set(report["workload_class"]), {"IDE", "GAME"})


if __name__ == "__main__":
    unittest.main()
