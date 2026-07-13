import unittest

from python.dataset_schema_v3 import (
    SCHEMA_VERSION,
    ValidationError,
    validate_action_outcome,
    validate_outcome_label,
    validate_telemetry_frame,
    validate_workload_episode,
)


def base_record(provenance="MEASURED_QOE"):
    return {
        "schema_version": SCHEMA_VERSION,
        "monotonic_timestamp_ms": 1000,
        "wall_timestamp_utc": "2026-07-13T00:00:00Z",
        "device_id": "device-opaque-01",
        "session_id": "session-01",
        "episode_id": "episode-01",
        "windows_build_family": "windows-11-26200",
        "hardware": {
            "cpu_core_tier": "4-logical",
            "ram_tier": "4gb",
            "storage_type": "ssd",
            "gpu_tier": "integrated",
            "power_mode": "balanced",
        },
        "collector_health": {"qoe": "healthy"},
        "feature_source_versions": {"qoe": "v1"},
        "provenance": provenance,
    }


class DatasetContractTests(unittest.TestCase):
    def test_telemetry_requires_raw_and_normalized_measurements(self):
        record = base_record()
        record.update({
            "foreground": {"app_family": "ide", "behavior_class": "compilation"},
            "workload_phase": "ACTIVE_INTERACTION",
            "resources": {"cpu_percent": 25.0, "memory_percent": 60.0, "disk_free_percent": 30.0},
            "normalized_resources": {"cpu_robust_z": 0.2, "memory_robust_z": 0.3, "disk_robust_z": -0.1},
        })
        result = validate_telemetry_frame(record)
        self.assertEqual(result.record_type, "telemetry_frame")

        del record["normalized_resources"]
        with self.assertRaises(ValidationError):
            validate_telemetry_frame(record)

    def test_episode_requires_non_empty_interval_and_quality(self):
        record = base_record("CONTROLLED_LAB")
        record.update({
            "workload_class": "compilation",
            "start_reason": "foreground_change",
            "end_reason": "stable",
            "start_monotonic_ms": 100,
            "end_monotonic_ms": 400,
            "stable_baseline": {"memory_median": 60.0},
            "observation_interval_ms": 300,
            "outcome_interval_ms": 100,
            "data_quality": {"valid": True},
        })
        self.assertTrue(validate_workload_episode(record).certification_eligible)
        record["end_monotonic_ms"] = 100
        with self.assertRaises(ValidationError):
            validate_workload_episode(record)

    def test_certification_rejects_weak_labels(self):
        record = base_record("WEAK_HEURISTIC")
        record.update({
            "primary_outcome": "USER_IMPACT_EVENT",
            "label_confidence": 0.4,
            "qoe_evidence": {"source": "threshold"},
        })
        self.assertFalse(validate_outcome_label(record).certification_eligible)
        with self.assertRaises(ValidationError):
            validate_outcome_label(record, for_certification=True)

    def test_measured_action_outcome_is_certification_eligible(self):
        record = base_record("MEASURED_QOE")
        record.update({
            "transaction_id": "txn-01",
            "action_type": "eco_qos",
            "outcome": "VERIFIED_HELPFUL",
            "rollback": {"complete": True},
            "measured_effect": {"foreground_latency_delta_ms": -12.0},
        })
        self.assertTrue(validate_action_outcome(record, for_certification=True).certification_eligible)


if __name__ == "__main__":
    unittest.main()
