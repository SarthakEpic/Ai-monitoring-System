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


def pressure_series(future: np.ndarray, thresholds: LabelThresholds) -> np.ndarray:
    return np.asarray(
        [
            pressure_score(float(cpu), float(mem), float(disk), thresholds)
            for cpu, mem, disk in future[:, :3]
        ],
        dtype=float,
    )


def is_recovery_window(future: np.ndarray, thresholds: LabelThresholds) -> bool:
    pressure = pressure_series(future, thresholds)
    start = float(pressure[0])
    end = float(pressure[-1])
    peak = float(pressure.max())
    early = float(pressure[:2].mean())
    late = float(pressure[-2:].mean())

    cpu = future[:, 0]
    mem = future[:, 1]
    disk = future[:, 2]
    cpu_recovering = float(cpu[:2].mean() - cpu[-2:].mean()) >= 8.0
    mem_recovering = float(mem[:2].mean() - mem[-2:].mean()) >= 6.0
    disk_recovering = float(disk[-2:].mean() - disk[:2].mean()) >= 2.0

    pressure_improving = peak >= 55.0 and end <= start - 8.0 and late <= early - 7.0
    still_critical = end >= 78.0 or float(disk[-1]) <= max(3.0, thresholds.disk * 0.35)
    return pressure_improving and not still_critical and (cpu_recovering or mem_recovering or disk_recovering)


def label_future_window(future: np.ndarray, thresholds: LabelThresholds) -> str:
    cpu = future[:, 0]
    mem = future[:, 1]
    disk = future[:, 2]

    pressure = pressure_series(future, thresholds)
    peak = float(pressure.max())

    cpu_peak = float(cpu.max())
    mem_peak = float(mem.max())
    disk_low = float(disk.min())

    if is_recovery_window(future, thresholds):
        return "RECOVERY"
    if disk_low <= max(3.0, thresholds.disk * 0.35) or cpu_peak >= 96.0 or mem_peak >= 96.0 or peak >= 84.0:
        return "CRITICAL"
    if cpu_peak >= thresholds.cpu or mem_peak >= thresholds.mem or disk_low <= thresholds.disk or peak >= 58.0:
        return "WARNING"
    return "NORMAL"


def label_to_risk(label: str) -> float:
    return {
        "NORMAL": 18.0,
        "RECOVERY": 38.0,
        "WARNING": 66.0,
        "CRITICAL": 92.0,
    }.get(label, 50.0)
