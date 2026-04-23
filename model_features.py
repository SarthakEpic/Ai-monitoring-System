import json
import os
from typing import List, Sequence, Tuple

import joblib
import numpy as np

WINDOW = 8
DEFAULT_CPU_TH = 80
DEFAULT_MEM_TH = 85
DEFAULT_DISK_TH = 10


def load_thresholds(config_path: str) -> Tuple[int, int, int]:
    cpu_th, mem_th, disk_th = DEFAULT_CPU_TH, DEFAULT_MEM_TH, DEFAULT_DISK_TH
    if not os.path.exists(config_path):
        return cpu_th, mem_th, disk_th

    with open(config_path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip().upper()
            value = value.strip()
            try:
                if key == "CPU_THRESHOLD":
                    cpu_th = int(value)
                elif key == "MEM_THRESHOLD":
                    mem_th = int(value)
                elif key == "DISK_THRESHOLD":
                    disk_th = int(value)
            except ValueError:
                pass

    return cpu_th, mem_th, disk_th


def slope(arr: np.ndarray) -> float:
    if len(arr) < 2:
        return 0.0
    if np.allclose(arr, arr[0]):
        return 0.0
    x = np.arange(len(arr), dtype=float)
    return float(np.polyfit(x, arr, 1)[0])


def build_features(window: np.ndarray, disk_th: int) -> np.ndarray:
    cpu = window[:, 0]
    mem = window[:, 1]
    disk = window[:, 2]

    def stats(a: np.ndarray) -> List[float]:
        return [
            float(a[-1]),
            float(a.mean()),
            float(a.std(ddof=0)),
            float(a.min()),
            float(a.max()),
            float(a[-1] - a[0]),
            slope(a),
        ]

    disk_pressure = 0.0
    if disk[-1] < disk_th:
        disk_pressure = float(max(0.0, (disk_th - disk[-1]) / max(1, disk_th) * 100.0))

    return np.array(
        stats(cpu)
        + stats(mem)
        + stats(disk)
        + [
            float(cpu[-1] - mem[-1]),
            float(mem[-1] - disk[-1]),
            float(cpu[-1] - disk[-1]),
            float(disk_pressure),
        ],
        dtype=float,
    )


def _validate_history(values: Sequence[float], label: str) -> None:
    if len(values) < WINDOW:
        raise ValueError(f"{label} must contain at least {WINDOW} samples, found {len(values)}.")


def load_runtime_window(input_path: str) -> Tuple[np.ndarray, int]:
    with open(input_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    cpu_history = payload.get("cpu_history", [])
    mem_history = payload.get("mem_history", [])
    disk_history = payload.get("disk_history", [])
    disk_th = int(payload.get("disk_threshold", DEFAULT_DISK_TH))

    _validate_history(cpu_history, "cpu_history")
    _validate_history(mem_history, "mem_history")
    _validate_history(disk_history, "disk_history")

    window = np.column_stack(
        [
            np.asarray(cpu_history[-WINDOW:], dtype=float),
            np.asarray(mem_history[-WINDOW:], dtype=float),
            np.asarray(disk_history[-WINDOW:], dtype=float),
        ]
    )
    return window, disk_th


def predict_probability_from_runtime_file(input_path: str, model_path: str) -> float:
    window, disk_th = load_runtime_window(input_path)
    features = build_features(window, disk_th).reshape(1, -1)
    model = joblib.load(model_path)

    if hasattr(model, "predict_proba"):
        probability = float(model.predict_proba(features)[0][1]) * 100.0
    else:
        probability = float(model.predict(features)[0]) * 100.0

    return max(0.0, min(100.0, probability))
