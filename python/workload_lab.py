"""Bounded Aegis-99 Phase 4 workload lab.

Dry-run is the default. Execution is opt-in, capped, and writes only under a
caller supplied temporary directory. The tool records what it ran so real
hardware evidence remains distinguishable from synthetic lab activity.
"""
from __future__ import annotations

import argparse
import json
import os
import tempfile
import threading
import time
from pathlib import Path

SCENARIOS = {"cpu", "memory", "storage", "process", "hang", "network", "graphics", "power"}
UNSUPPORTED = {"network", "graphics", "power"}


def plan(scenario: str, seconds: int, workers: int, memory_mb: int, io_mb: int) -> dict[str, object]:
    if scenario not in SCENARIOS:
        raise ValueError(f"unsupported scenario: {scenario}")
    if not 1 <= seconds <= 60 or not 1 <= workers <= 4 or not 0 <= memory_mb <= 256 or not 0 <= io_mb <= 32:
        raise ValueError("requested workload exceeds bounded lab limits")
    return {"schema_version": 1, "scenario": scenario, "seconds": seconds, "workers": workers, "memory_mb": memory_mb, "io_mb": io_mb, "supported": scenario not in UNSUPPORTED, "execution": "dry_run"}


def _cpu_worker(deadline: float) -> None:
    value = 0
    while time.monotonic() < deadline:
        value = (value * 1103515245 + 12345) & 0x7FFFFFFF


def execute(spec: dict[str, object], root: Path) -> dict[str, object]:
    if not spec["supported"]:
        raise ValueError(f"{spec['scenario']} requires a platform-specific external harness and cannot be simulated as evidence")
    deadline = time.monotonic() + int(spec["seconds"])
    scenario = str(spec["scenario"])
    allocations: list[bytearray] = []
    created: list[str] = []
    try:
        if scenario == "cpu":
            threads = [threading.Thread(target=_cpu_worker, args=(deadline,)) for _ in range(int(spec["workers"]))]
            for thread in threads: thread.start()
            for thread in threads: thread.join()
        elif scenario == "memory":
            allocations = [bytearray(1024 * 1024) for _ in range(int(spec["memory_mb"]))]
            while time.monotonic() < deadline: time.sleep(0.02)
        elif scenario == "storage":
            root.mkdir(parents=True, exist_ok=True)
            target = root / "aegis99-workload.bin"
            chunk = bytes(1024 * 1024)
            with target.open("wb") as stream:
                for _ in range(int(spec["io_mb"])): stream.write(chunk)
                stream.flush(); os.fsync(stream.fileno())
            created.append(str(target))
            while time.monotonic() < deadline: time.sleep(0.02)
        elif scenario in {"process", "hang"}:
            # The lab records these as no-mutation lifecycle probes. A real process
            # storm or application hang requires a separate isolated VM harness.
            while time.monotonic() < deadline: time.sleep(0.02)
        return {**spec, "execution": "completed", "created_files": created}
    finally:
        allocations.clear()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=sorted(SCENARIOS), required=True)
    parser.add_argument("--seconds", type=int, default=5)
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--memory-mb", type=int, default=32)
    parser.add_argument("--io-mb", type=int, default=8)
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--temp-root", default=str(Path(tempfile.gettempdir()) / "aegis99-workload-lab"))
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        result = plan(args.scenario, args.seconds, args.workers, args.memory_mb, args.io_mb)
        if args.execute: result = execute(result, Path(args.temp_root))
    except ValueError as error:
        result = {"schema_version": 1, "execution": "rejected", "reason": str(error)}
    Path(args.output).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Workload lab: {result['execution']}")
    return 0 if result["execution"] in {"dry_run", "completed"} else 2

if __name__ == "__main__":
    raise SystemExit(main())
