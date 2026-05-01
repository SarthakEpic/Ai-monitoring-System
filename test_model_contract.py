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


if __name__ == "__main__":
    unittest.main()
