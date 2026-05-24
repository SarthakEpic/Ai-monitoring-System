from __future__ import annotations

import json
import os
from typing import Dict, List, Sequence, Tuple

import joblib
import numpy as np

from labeling import LABELS, label_to_risk

WINDOW = 8
DEFAULT_CPU_TH = 80
DEFAULT_MEM_TH = 85
DEFAULT_DISK_TH = 10

STAT_NAMES = ["last", "mean", "std", "min", "max", "delta", "slope", "range"]
SERIES_NAMES = ["cpu", "mem", "disk", "net", "process"]
FEATURE_NAMES = [f"{series}_{stat}" for series in SERIES_NAMES for stat in STAT_NAMES] + [
    "cpu_mem_gap",
    "mem_disk_gap",
    "cpu_disk_gap",
    "disk_pressure",
    "memory_pressure",
    "cpu_pressure",
    "pressure_start",
    "pressure_end",
    "pressure_peak",
    "pressure_delta",
    "pressure_drop",
    "recent_pressure_drop",
    "cpu_recovery",
    "mem_recovery",
    "disk_recovery",
    "critical_pressure_signal",
    "net_spike",
    "process_growth",
    "resource_pressure",
    "recovery_signal",
    "recovery_momentum",
]


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
    if len(arr) < 2 or np.allclose(arr, arr[0]):
        return 0.0
    x = np.arange(len(arr), dtype=float)
    return float(np.polyfit(x, arr, 1)[0])


def _stats(a: np.ndarray) -> List[float]:
    return [
        float(a[-1]),
        float(a.mean()),
        float(a.std(ddof=0)),
        float(a.min()),
        float(a.max()),
        float(a[-1] - a[0]),
        slope(a),
        float(a.max() - a.min()),
    ]


def _pressure(value: float, threshold: int) -> float:
    return float(min(100.0, max(0.0, (value / max(1, threshold)) * 100.0)))


def _disk_pressure(value: float, threshold: int) -> float:
    if value >= threshold:
        return 0.0
    return float(min(100.0, max(0.0, (threshold - value) / max(1, threshold) * 100.0)))


def _resource_pressure(cpu_value: float, mem_value: float, disk_value: float, disk_th: int, cpu_th: int, mem_th: int) -> float:
    return float(
        (_pressure(cpu_value, cpu_th) * 0.42)
        + (_pressure(mem_value, mem_th) * 0.42)
        + (_disk_pressure(disk_value, disk_th) * 0.16)
    )


def build_features(window: np.ndarray, disk_th: int, cpu_th: int = DEFAULT_CPU_TH, mem_th: int = DEFAULT_MEM_TH) -> np.ndarray:
    cpu = window[:, 0]
    mem = window[:, 1]
    disk = window[:, 2]
    net = window[:, 3] if window.shape[1] > 3 else np.zeros(len(window), dtype=float)
    proc = window[:, 4] if window.shape[1] > 4 else np.zeros(len(window), dtype=float)

    disk_pressure = _disk_pressure(float(disk[-1]), disk_th)
    cpu_pressure = _pressure(float(cpu[-1]), cpu_th)
    mem_pressure = _pressure(float(mem[-1]), mem_th)
    pressure_series = np.asarray(
        [
            _resource_pressure(float(c), float(m), float(d), disk_th, cpu_th, mem_th)
            for c, m, d in zip(cpu, mem, disk)
        ],
        dtype=float,
    )
    pressure_start = float(pressure_series[0])
    pressure_end = float(pressure_series[-1])
    pressure_peak = float(pressure_series.max())
    pressure_delta = float(pressure_end - pressure_start)
    pressure_drop = float(max(0.0, pressure_peak - pressure_end))
    recent_pressure_drop = float(max(0.0, pressure_series[-4:-2].mean() - pressure_series[-2:].mean()))
    cpu_recovery = float(max(0.0, cpu[:2].mean() - cpu[-2:].mean()))
    mem_recovery = float(max(0.0, mem[:2].mean() - mem[-2:].mean()))
    disk_recovery = float(max(0.0, disk[-2:].mean() - disk[:2].mean()))
    critical_pressure_signal = float(max(0.0, pressure_peak - 80.0))
    resource_pressure = pressure_end
    recovery_signal = (cpu_recovery * 0.35) + (mem_recovery * 0.35) + (pressure_drop * 0.3)
    recovery_momentum = float(max(0.0, -slope(pressure_series)))

    return np.array(
        _stats(cpu)
        + _stats(mem)
        + _stats(disk)
        + _stats(net)
        + _stats(proc)
        + [
            float(cpu[-1] - mem[-1]),
            float(mem[-1] - disk[-1]),
            float(cpu[-1] - disk[-1]),
            disk_pressure,
            mem_pressure,
            cpu_pressure,
            pressure_start,
            pressure_end,
            pressure_peak,
            pressure_delta,
            pressure_drop,
            recent_pressure_drop,
            cpu_recovery,
            mem_recovery,
            disk_recovery,
            critical_pressure_signal,
            float(max(0.0, net[-1] - np.median(net))),
            float(proc[-1] - proc[0]),
            float(resource_pressure),
            float(recovery_signal),
            float(recovery_momentum),
        ],
        dtype=float,
    )


def _validate_history(values: Sequence[float], label: str) -> None:
    if len(values) < WINDOW:
        raise ValueError(f"{label} must contain at least {WINDOW} samples, found {len(values)}.")


def load_runtime_window(input_path: str) -> Tuple[np.ndarray, Dict[str, int]]:
    with open(input_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    cpu_history = payload.get("cpu_history", [])
    mem_history = payload.get("mem_history", [])
    disk_history = payload.get("disk_history", [])
    net_history = payload.get("net_history", [0.0] * len(cpu_history))
    process_history = payload.get("process_history", [0.0] * len(cpu_history))

    thresholds = {
        "cpu": int(payload.get("cpu_threshold", DEFAULT_CPU_TH)),
        "mem": int(payload.get("mem_threshold", DEFAULT_MEM_TH)),
        "disk": int(payload.get("disk_threshold", DEFAULT_DISK_TH)),
    }

    _validate_history(cpu_history, "cpu_history")
    _validate_history(mem_history, "mem_history")
    _validate_history(disk_history, "disk_history")

    window = np.column_stack(
        [
            np.asarray(cpu_history[-WINDOW:], dtype=float),
            np.asarray(mem_history[-WINDOW:], dtype=float),
            np.asarray(disk_history[-WINDOW:], dtype=float),
            np.asarray(net_history[-WINDOW:], dtype=float),
            np.asarray(process_history[-WINDOW:], dtype=float),
        ]
    )
    return window, thresholds


def explain_prediction(window: np.ndarray, thresholds: Dict[str, int], predicted_class: str) -> str:
    cpu = float(window[-1, 0])
    mem = float(window[-1, 1])
    disk = float(window[-1, 2])
    net = float(window[-1, 3])
    proc_delta = float(window[-1, 4] - window[0, 4])

    reasons: List[str] = []
    if mem >= thresholds["mem"] * 0.95:
        reasons.append("memory near threshold")
    if cpu >= thresholds["cpu"] * 0.95:
        reasons.append("cpu near threshold")
    if disk <= thresholds["disk"]:
        reasons.append("disk space low")
    if slope(window[:, 1]) > 0.8:
        reasons.append("memory rising")
    if slope(window[:, 0]) > 1.5:
        reasons.append("cpu rising")
    if net > max(1024.0, float(np.median(window[:, 3]) * 2.5)):
        reasons.append("network spike")
    if proc_delta >= 8:
        reasons.append("process count rising")
    if predicted_class == "RECOVERY":
        reasons.append("pressure improving")

    if not reasons:
        reasons.append("resource pattern stable" if predicted_class == "NORMAL" else "forecast trend elevated")

    return ", ".join(reasons[:3])


def build_prediction_payload(input_path: str, model_path: str) -> Dict[str, object]:
    window, thresholds = load_runtime_window(input_path)
    features = build_features(window, thresholds["disk"], thresholds["cpu"], thresholds["mem"]).reshape(1, -1)
    model = joblib.load(model_path)
    if hasattr(model, "n_jobs"):
        model.n_jobs = 1

    if not hasattr(model, "predict_proba"):
        raw = float(model.predict(features)[0])
        probability = max(0.0, min(100.0, raw * 100.0))
        predicted_class = "WARNING" if probability >= 55.0 else "NORMAL"
        confidence = 55.0
        class_probabilities = {label: 0.0 for label in LABELS}
    else:
        probabilities = model.predict_proba(features)[0]
        classes = [int(value) for value in getattr(model, "classes_", range(len(probabilities)))]
        class_probabilities = {label: 0.0 for label in LABELS}
        for cls, prob in zip(classes, probabilities):
            if 0 <= cls < len(LABELS):
                class_probabilities[LABELS[cls]] = float(prob)

        predicted_class = max(class_probabilities, key=class_probabilities.get)
        confidence = max(class_probabilities.values()) * 100.0
        probability = sum(class_probabilities[label] * label_to_risk(label) for label in LABELS)

    probability = max(0.0, min(100.0, float(probability)))
    reason = explain_prediction(window, thresholds, predicted_class)

    safe_to_heal = False
    recommended_action = "monitor_only"
    if predicted_class == "CRITICAL" and confidence >= 78.0:
        recommended_action = "prepare_heal_review"
    elif predicted_class == "WARNING":
        recommended_action = "increase_observation"

    return {
        "risk": round(probability, 4),
        "probability": round(probability, 4),
        "confidence": round(float(confidence), 4),
        "class": predicted_class,
        "reason": reason,
        "recommended_action": recommended_action,
        "safe_to_heal": safe_to_heal,
        "class_probabilities": {k: round(v * 100.0, 4) for k, v in class_probabilities.items()},
        "feature_count": len(FEATURE_NAMES),
        "contract": "ai_reliability_v2",
    }


def predict_probability_from_runtime_file(input_path: str, model_path: str) -> float:
    return float(build_prediction_payload(input_path, model_path)["risk"])
