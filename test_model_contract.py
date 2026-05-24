import json
import shutil
import unittest
from pathlib import Path

import joblib
import numpy as np

from model_features import FEATURE_NAMES, WINDOW, build_prediction_payload, predict_probability_from_runtime_file

TEST_ROOT = Path(__file__).resolve().parent / ".test_tmp"


class DummyModel:
    classes_ = np.array([0, 1, 2, 3], dtype=int)

    def predict_proba(self, features):
        if features.shape != (1, len(FEATURE_NAMES)):
            raise ValueError(f"expected {len(FEATURE_NAMES)} engineered features, got {features.shape}")
        return np.array([[0.10, 0.20, 0.60, 0.10]], dtype=float)


class RecoveryConfusedModel:
    classes_ = np.array([0, 1, 2, 3], dtype=int)

    def predict_proba(self, features):
        return np.array([[0.05, 0.50, 0.12, 0.33]], dtype=float)


class CriticalConfusedModel:
    classes_ = np.array([0, 1, 2, 3], dtype=int)

    def predict_proba(self, features):
        return np.array([[0.03, 0.44, 0.38, 0.15]], dtype=float)


class ModelContractTests(unittest.TestCase):
    def setUp(self):
        TEST_ROOT.mkdir(exist_ok=True)

    def tearDown(self):
        if TEST_ROOT.exists():
            shutil.rmtree(TEST_ROOT)

    def test_runtime_window_prediction_uses_reliability_payload(self):
        runtime_path = TEST_ROOT / "runtime_features.json"
        model_path = TEST_ROOT / "ai_model.joblib"

        payload = {
            "window": WINDOW,
            "cpu_threshold": 80,
            "mem_threshold": 85,
            "disk_threshold": 10,
            "cpu_history": [20, 25, 30, 35, 40, 42, 45, 50],
            "mem_history": [30, 31, 32, 33, 34, 35, 36, 37],
            "disk_history": [70, 69, 68, 67, 66, 65, 64, 63],
            "net_history": [1, 1, 2, 2, 2, 3, 3, 4],
            "process_history": [80, 80, 81, 82, 82, 83, 84, 84],
        }

        runtime_path.write_text(json.dumps(payload), encoding="utf-8")
        joblib.dump(DummyModel(), model_path)

        result = build_prediction_payload(str(runtime_path), str(model_path))
        self.assertEqual(result["contract"], "ai_reliability_v2")
        self.assertEqual(result["class"], "CRITICAL")
        self.assertAlmostEqual(result["confidence"], 60.0)
        self.assertIn("reason", result)
        self.assertGreater(result["risk"], 0.0)

        probability = predict_probability_from_runtime_file(str(runtime_path), str(model_path))
        self.assertAlmostEqual(probability, result["risk"])

    def test_runtime_window_requires_enough_samples(self):
        runtime_path = TEST_ROOT / "runtime_features.json"
        model_path = TEST_ROOT / "ai_model.joblib"

        payload = {
            "window": WINDOW,
            "disk_threshold": 10,
            "cpu_history": [20] * (WINDOW - 1),
            "mem_history": [30] * (WINDOW - 1),
            "disk_history": [70] * (WINDOW - 1),
        }

        runtime_path.write_text(json.dumps(payload), encoding="utf-8")
        joblib.dump(DummyModel(), model_path)

        with self.assertRaises(ValueError):
            predict_probability_from_runtime_file(str(runtime_path), str(model_path))

    def test_runtime_calibration_promotes_recovery_pattern(self):
        runtime_path = TEST_ROOT / "runtime_features.json"
        model_path = TEST_ROOT / "ai_model.joblib"

        payload = {
            "window": WINDOW,
            "cpu_threshold": 80,
            "mem_threshold": 85,
            "disk_threshold": 10,
            "cpu_history": [92, 88, 83, 76, 68, 60, 53, 48],
            "mem_history": [89, 86, 81, 76, 70, 64, 59, 55],
            "disk_history": [25, 25, 26, 27, 28, 30, 32, 34],
            "net_history": [3, 3, 2, 2, 2, 1, 1, 1],
            "process_history": [90, 90, 89, 88, 87, 86, 84, 83],
        }

        runtime_path.write_text(json.dumps(payload), encoding="utf-8")
        joblib.dump(RecoveryConfusedModel(), model_path)

        result = build_prediction_payload(str(runtime_path), str(model_path))
        self.assertEqual(result["class"], "RECOVERY")
        self.assertGreater(result["class_probabilities"]["RECOVERY"], result["class_probabilities"]["WARNING"])

    def test_runtime_calibration_promotes_active_critical_pressure(self):
        runtime_path = TEST_ROOT / "runtime_features.json"
        model_path = TEST_ROOT / "ai_model.joblib"

        payload = {
            "window": WINDOW,
            "cpu_threshold": 80,
            "mem_threshold": 85,
            "disk_threshold": 10,
            "cpu_history": [82, 85, 88, 91, 94, 97, 98, 99],
            "mem_history": [78, 80, 84, 87, 90, 93, 96, 97],
            "disk_history": [12, 11, 9, 7, 6, 5, 4, 3],
            "net_history": [2, 2, 3, 3, 4, 5, 5, 6],
            "process_history": [80, 82, 84, 88, 91, 95, 99, 103],
        }

        runtime_path.write_text(json.dumps(payload), encoding="utf-8")
        joblib.dump(CriticalConfusedModel(), model_path)

        result = build_prediction_payload(str(runtime_path), str(model_path))
        self.assertEqual(result["class"], "CRITICAL")
        self.assertGreater(result["class_probabilities"]["CRITICAL"], result["class_probabilities"]["WARNING"])


if __name__ == "__main__":
    unittest.main()
