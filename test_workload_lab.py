import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "python"))
from workload_lab import execute, plan


class WorkloadLabTests(unittest.TestCase):
    def test_dry_run_is_bounded(self):
        result = plan("cpu", 5, 2, 32, 8)
        self.assertEqual(result["execution"], "dry_run")
        self.assertTrue(result["supported"])

    def test_unsupported_workload_cannot_be_presented_as_execution(self):
        with self.assertRaises(ValueError):
            execute(plan("graphics", 1, 1, 0, 0), Path(tempfile.gettempdir()))

    def test_storage_lab_writes_only_its_root(self):
        with tempfile.TemporaryDirectory() as directory:
            result = execute(plan("storage", 1, 1, 0, 1), Path(directory))
            self.assertEqual(result["execution"], "completed")
            self.assertTrue(Path(result["created_files"][0]).is_file())


if __name__ == "__main__":
    unittest.main()
