import unittest

from python.episode_builder import EpisodeBuilderConfig, build_episodes


def frame(timestamp, *, session="s1", phase="ACTIVE", behavior="coding", app="ide", cpu=20.0, memory=50.0, disk=40.0):
    return {
        "frame_id": f"frame-{timestamp}",
        "monotonic_timestamp_ms": timestamp,
        "wall_timestamp_utc": f"2026-07-13T00:00:{timestamp // 1000:02d}Z",
        "device_id": "device-a",
        "session_id": session,
        "windows_build_family": "windows-11",
        "hardware": {"cpu_core_tier": "4", "ram_tier": "4gb", "storage_type": "ssd", "gpu_tier": "igpu", "power_mode": "balanced"},
        "collector_health": {"qoe": "HEALTHY"},
        "feature_source_versions": {"qoe": "v1"},
        "provenance": "MEASURED_QOE",
        "foreground": {"app_family": app, "behavior_class": behavior},
        "workload_phase": phase,
        "resources": {"cpu_percent": cpu, "memory_percent": memory, "disk_free_percent": disk},
    }


class EpisodeBuilderTests(unittest.TestCase):
    def test_builds_stable_baseline_and_keeps_frames_together(self):
        episodes = build_episodes([
            frame(1000, cpu=10.0), frame(2000, cpu=20.0), frame(3000, cpu=30.0),
        ])
        self.assertEqual(len(episodes), 1)
        record = episodes[0].record
        self.assertEqual(episodes[0].frame_count, 3)
        self.assertEqual(record["stable_baseline"]["cpu_median"], 20.0)
        self.assertTrue(record["data_quality"]["valid"])
        self.assertEqual(record["start_reason"], "first_frame")
        self.assertEqual(record["end_reason"], "input_exhausted")

    def test_workload_change_and_gap_create_explained_boundaries(self):
        episodes = build_episodes([
            frame(1000), frame(2000),
            frame(3000, phase="COMPILING"), frame(4000, phase="COMPILING"),
            frame(25000, phase="COMPILING"), frame(26000, phase="COMPILING"),
        ])
        self.assertEqual(len(episodes), 3)
        self.assertEqual(episodes[0].record["end_reason"], "workload_phase_changed")
        self.assertEqual(episodes[1].record["end_reason"], "telemetry_gap")
        self.assertFalse(episodes[0].record["data_quality"]["valid"])

    def test_invalid_collector_health_remains_visible(self):
        records = [frame(1000), frame(2000), frame(3000)]
        records[1]["collector_health"] = {"qoe": "UNAVAILABLE"}
        episode = build_episodes(records)[0].record
        self.assertFalse(episode["data_quality"]["valid"])
        self.assertIn("qoe", episode["data_quality"]["missing_or_unavailable_collectors"])

    def test_configurable_gap_preserves_a_continuous_episode(self):
        episodes = build_episodes(
            [frame(1000), frame(10000), frame(19000)],
            EpisodeBuilderConfig(maximum_frame_gap_ms=10000),
        )
        self.assertEqual(len(episodes), 1)


if __name__ == "__main__":
    unittest.main()
