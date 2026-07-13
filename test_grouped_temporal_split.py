import tempfile
import unittest
from pathlib import Path

from python.grouped_temporal_split import (
    EpisodeDescriptor,
    LeakageError,
    assert_no_window_overlap,
    split_grouped_temporal,
)
from python.locked_split import (
    create_locked_manifest,
    load_locked_manifest,
    reject_locked_episode_use,
    write_locked_manifest,
)


def episode(index, *, device=None, workload="coding", app="ide"):
    start = index * 10000
    return EpisodeDescriptor(
        episode_id=f"episode-{index}",
        device_id=device or f"device-{index}",
        session_id=f"session-{index}",
        workload_class=workload,
        application_family=app,
        start_monotonic_ms=start,
        end_monotonic_ms=start + 1000,
    )


class GroupedTemporalSplitTests(unittest.TestCase):
    def test_episode_split_keeps_whole_episodes_in_one_role(self):
        result = split_grouped_temporal([episode(index) for index in range(10)], purge_gap_ms=200)
        assigned = set()
        for role in result.roles:
            ids = result.ids(role)
            self.assertFalse(assigned & ids)
            assigned |= ids
        self.assertEqual(len(assigned), 10)

    def test_device_disjoint_split_prevents_cross_role_device_overlap(self):
        episodes = [
            episode(0, device="a"), episode(1, device="a"),
            episode(2, device="b"), episode(3, device="b"),
            episode(4, device="c"), episode(5, device="c"),
            episode(6, device="d"), episode(7, device="d"),
        ]
        result = split_grouped_temporal(episodes, group_by="device")
        assigned_devices = {}
        for role, values in result.roles.items():
            for value in values:
                if value.device_id in assigned_devices:
                    self.assertEqual(assigned_devices[value.device_id], role)
                else:
                    assigned_devices[value.device_id] = role
                    continue
                assigned_devices[value.device_id] = role

    def test_window_overlap_is_rejected(self):
        with self.assertRaises(LeakageError):
            assert_no_window_overlap({
                "train": [{"episode_id": "same"}],
                "locked_test": [{"episode_id": "same"}],
            })

    def test_locked_manifest_detects_tampering_and_training_reuse(self):
        manifest = create_locked_manifest(["locked-1", "locked-2"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "locked.json"
            write_locked_manifest(path, manifest)
            loaded = load_locked_manifest(path)
            self.assertEqual(loaded.content_hash, manifest.content_hash)
            with self.assertRaises(LeakageError):
                reject_locked_episode_use([{"episode_id": "locked-1"}], loaded, role="train")
            path.write_text('{"schema_version":1,"purpose":"x","episode_ids":["locked-1"],"content_hash":"bad"}', encoding="utf-8")
            with self.assertRaises(LeakageError):
                load_locked_manifest(path)


if __name__ == "__main__":
    unittest.main()
