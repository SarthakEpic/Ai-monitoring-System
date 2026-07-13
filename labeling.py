from __future__ import annotations

from dataclasses import dataclass
from typing import Dict

import numpy as np


LABELS = ["NORMAL", "WARNING", "CRITICAL", "RECOVERY"]
LABEL_TO_ID: Dict[str, int] = {label: idx for idx, label in enumerate(LABELS)}
ID_TO_LABEL: Dict[int, str] = {idx: label for label, idx in LABEL_TO_ID.items()}


@dataclass(frozen=True)
class LabelThresholds:
    cpu: int = 80
    mem: int = 85
    disk: int = 10


def pressure_score(cpu: float, mem: float, disk: float, thresholds: LabelThresholds) -> float:
    cpu_score = min(100.0, max(0.0, (cpu / max(1, thresholds.cpu)) * 100.0))
    mem_score = min(100.0, max(0.0, (mem / max(1, thresholds.mem)) * 100.0))
    disk_score = 0.0
    if disk < thresholds.disk:
        disk_score = min(100.0, max(0.0, ((thresholds.disk - disk) / max(1, thresholds.disk)) * 100.0))
    return min(100.0, max(0.0, (cpu_score * 0.42) + (mem_score * 0.42) + (disk_score * 0.16)))


def label_future_window(future: np.ndarray, thresholds: LabelThresholds) -> str:
    cpu = future[:, 0]
    mem = future[:, 1]
    disk = future[:, 2]

    current = pressure_score(float(cpu[0]), float(mem[0]), float(disk[0]), thresholds)
    peak = max(
        pressure_score(float(c), float(m), float(d), thresholds)
        for c, m, d in zip(cpu, mem, disk)
    )

    cpu_peak = float(cpu.max())
    mem_peak = float(mem.max())
    disk_low = float(disk.min())
    pressure_improving = peak >= 55.0 and pressure_score(float(cpu[-1]), float(mem[-1]), float(disk[-1]), thresholds) + 8.0 < current

    if disk_low <= max(3.0, thresholds.disk * 0.35) or cpu_peak >= 96.0 or mem_peak >= 96.0 or peak >= 82.0:
        return "CRITICAL"
    if pressure_improving:
        return "RECOVERY"
    if cpu_peak >= thresholds.cpu or mem_peak >= thresholds.mem or disk_low <= thresholds.disk or peak >= 58.0:
        return "WARNING"
    return "NORMAL"


def label_to_risk(label: str) -> float:
    return {
        "NORMAL": 18.0,
        "RECOVERY": 18.0,
        "WARNING": 66.0,
        "CRITICAL": 92.0,
    }.get(label, 50.0)
