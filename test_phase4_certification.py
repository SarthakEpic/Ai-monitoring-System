import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "python"))

from certification_runner import CertificationError, build_report, validate_locked_episodes
from locked_split import create_locked_manifest, write_locked_manifest
from stress_lab import build_corpus


def episode(identifier: str, **changes: object) -> dict[str, object]:
    row = {
        "episode_id": identifier,
        "label_provenance": "MEASURED_QOE",
        "support_slice": "win11-8gb-ssd",
        "eligible": True,
        "accepted": True,
        "correct": True,
        "critical_event": True,
        "critical_detected": True,
        "committed_action": True,
        "severe_harm": False,
        "rollback_attempted": True,
        "rollback_succeeded": True,
        "protected_policy_violation": False,
    }
    row.update(changes)
    return row


class Phase4CertificationTests(unittest.TestCase):
    def test_locked_manifest_and_measured_provenance_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "locked.json"
            write_locked_manifest(path, create_locked_manifest(["one"]))
            with self.assertRaises(CertificationError):
                validate_locked_episodes([episode("one", label_provenance="WEAK_HEURISTIC")], path)
            with self.assertRaises(CertificationError):
                validate_locked_episodes([episode("different")], path)

    def test_small_perfect_set_remains_not_certified_by_exact_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            locked = root / "locked.json"
            rows = [episode("one")]
            write_locked_manifest(locked, create_locked_manifest(["one"]))
            report = build_report(rows, locked, {"frozen": True, "artifact_hashes": {"binary": "abc"}})
            self.assertEqual(report["status"], "NOT_CERTIFIED")
            self.assertIn("correct_outcome_lower_bound_failed", report["overall"]["failures"])

    def test_hard_case_corpus_is_deterministic(self):
        first = build_corpus("fixed")
        second = build_corpus("fixed")
        self.assertEqual(first, second)
        self.assertGreaterEqual(len(first["cases"]), 8)


if __name__ == "__main__":
    unittest.main()
