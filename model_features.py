from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

import joblib
import numpy as np

from labeling import LABELS, label_to_risk

WINDOW = 8
DEFAULT_CPU_TH = 80
DEFAULT_MEM_TH = 85
DEFAULT_DISK_TH = 10

STAT_NAMES = ["last", "mean", "std", "min", "max", "delta", "slope", "range"]
SERIES_NAMES = ["cpu", "mem", "disk", "net", "process", "top_cpu", "top_mem"]
FEATURE_NAMES = [f"{series}_{stat}" for series in SERIES_NAMES for stat in STAT_NAMES] + [
    "cpu_mem_gap",
    "mem_disk_gap",
    "cpu_disk_gap",
    "disk_pressure",
    "memory_pressure",
    "cpu_pressure",
    "net_spike",
    "process_growth",
    "top_cpu_pressure",
    "top_mem_pressure",
    "resource_pressure",
    "recovery_signal",
    "cpu_baseline_delta",
    "mem_baseline_delta",
    "disk_baseline_delta",
    "net_baseline_delta",
    "process_baseline_delta",
]

DEFAULT_BASELINE = {
    "cpu": 35.0,
    "mem": 55.0,
    "disk": 50.0,
    "net": 0.0,
    "process": 120.0,
}


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


def load_model_metadata(model_path: str) -> Dict[str, object]:
    path = Path(model_path)
    candidates = [
        path.with_name(path.stem.replace("_model", "_model_meta") + ".json"),
        path.with_name("ai_model_meta.json"),
    ]
    for candidate in candidates:
        if candidate.exists():
            try:
                return json.loads(candidate.read_text(encoding="utf-8"))
            except Exception:
                return {}
    return {}


def normalize_baseline(baseline: Dict[str, object] | None) -> Dict[str, float]:
    merged = dict(DEFAULT_BASELINE)
    if baseline:
        for key in merged:
            try:
                merged[key] = float(baseline.get(key, merged[key]))
            except Exception:
                pass
    return merged


def build_features(
    window: np.ndarray,
    disk_th: int,
    cpu_th: int = DEFAULT_CPU_TH,
    mem_th: int = DEFAULT_MEM_TH,
    baseline: Dict[str, object] | None = None,
) -> np.ndarray:
    cpu = window[:, 0]
    mem = window[:, 1]
    disk = window[:, 2]
    net = window[:, 3] if window.shape[1] > 3 else np.zeros(len(window), dtype=float)
    proc = window[:, 4] if window.shape[1] > 4 else np.zeros(len(window), dtype=float)
    top_cpu = window[:, 5] if window.shape[1] > 5 else np.zeros(len(window), dtype=float)
    top_mem = window[:, 6] if window.shape[1] > 6 else np.zeros(len(window), dtype=float)
    baseline_values = normalize_baseline(baseline)

    disk_pressure = 0.0
    if disk[-1] < disk_th:
        disk_pressure = float(max(0.0, (disk_th - disk[-1]) / max(1, disk_th) * 100.0))

    cpu_pressure = _pressure(float(cpu[-1]), cpu_th)
    mem_pressure = _pressure(float(mem[-1]), mem_th)
    resource_pressure = (cpu_pressure * 0.42) + (mem_pressure * 0.42) + (disk_pressure * 0.16)
    recovery_signal = max(0.0, float(cpu[0] - cpu[-1])) * 0.4 + max(0.0, float(mem[0] - mem[-1])) * 0.4

    return np.array(
        _stats(cpu)
        + _stats(mem)
        + _stats(disk)
        + _stats(net)
        + _stats(proc)
        + _stats(top_cpu)
        + _stats(top_mem)
        + [
            float(cpu[-1] - mem[-1]),
            float(mem[-1] - disk[-1]),
            float(cpu[-1] - disk[-1]),
            disk_pressure,
            mem_pressure,
            cpu_pressure,
            float(max(0.0, net[-1] - np.median(net))),
            float(proc[-1] - proc[0]),
            float(top_cpu[-1]),
            float(min(100.0, max(0.0, top_mem[-1] / 10.0))),
            float(resource_pressure),
            float(recovery_signal),
            float(cpu[-1] - baseline_values["cpu"]),
            float(mem[-1] - baseline_values["mem"]),
            float(disk[-1] - baseline_values["disk"]),
            float(net[-1] - baseline_values["net"]),
            float(proc[-1] - baseline_values["process"]),
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
    top_cpu_history = payload.get("top_process_cpu_history", [0.0] * len(cpu_history))
    top_mem_history = payload.get("top_process_mem_history", [0.0] * len(cpu_history))

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
            np.asarray(top_cpu_history[-WINDOW:], dtype=float),
            np.asarray(top_mem_history[-WINDOW:], dtype=float),
        ]
    )
    return window, thresholds


def diagnose_root_cause(window: np.ndarray, thresholds: Dict[str, int], predicted_class: str) -> Dict[str, object]:
    cpu = float(window[-1, 0])
    mem = float(window[-1, 1])
    disk = float(window[-1, 2])
    net = float(window[-1, 3])
    proc_delta = float(window[-1, 4] - window[0, 4])
    top_cpu = float(window[-1, 5]) if window.shape[1] > 5 else 0.0
    top_mem = float(window[-1, 6]) if window.shape[1] > 6 else 0.0

    candidates: List[tuple[str, float, str]] = []
    if mem >= thresholds["mem"] * 0.95:
        candidates.append(("memory", mem / max(1, thresholds["mem"]), "memory near threshold"))
    if cpu >= thresholds["cpu"] * 0.95:
        candidates.append(("cpu", cpu / max(1, thresholds["cpu"]), "cpu near threshold"))
    if disk <= thresholds["disk"]:
        candidates.append(("disk", (thresholds["disk"] - disk + 1.0) / max(1, thresholds["disk"]), "disk space low"))
    if slope(window[:, 1]) > 0.8:
        candidates.append(("memory", 0.75, "memory rising"))
    if slope(window[:, 0]) > 1.5:
        candidates.append(("cpu", 0.70, "cpu rising"))
    if net > max(1024.0, float(np.median(window[:, 3]) * 2.5)):
        candidates.append(("network", 0.65, "network spike"))
    if proc_delta >= 8:
        candidates.append(("process_count", 0.60, "process count rising"))
    if top_cpu >= 35.0:
        candidates.append(("top_process_cpu", top_cpu / 100.0, "top process cpu pressure"))
    if top_mem >= 700.0:
        candidates.append(("top_process_memory", min(1.0, top_mem / 2000.0), "top process memory pressure"))
    if predicted_class == "RECOVERY":
        candidates.append(("recovery", 0.70, "pressure improving"))

    if not candidates:
        root = "stable" if predicted_class == "NORMAL" else "forecast"
        candidates.append((root, 0.20, "resource pattern stable" if predicted_class == "NORMAL" else "forecast trend elevated"))

    candidates.sort(key=lambda item: item[1], reverse=True)
    primary, score, reason = candidates[0]
    reasons = [item[2] for item in candidates[:3]]
    severity = "critical" if score >= 1.0 or predicted_class == "CRITICAL" else ("warning" if score >= 0.65 else "normal")
    return {
        "primary": primary,
        "severity": severity,
        "score": round(float(min(100.0, score * 100.0)), 4),
        "reason": reason,
        "reasons": reasons,
    }


def build_prediction_payload_from_model(input_path: str, model: object, metadata: Dict[str, object] | None = None) -> Dict[str, object]:
    window, thresholds = load_runtime_window(input_path)
    metadata = metadata or {}
    baseline = metadata.get("baseline", DEFAULT_BASELINE) if isinstance(metadata, dict) else DEFAULT_BASELINE
    features = build_features(window, thresholds["disk"], thresholds["cpu"], thresholds["mem"], baseline).reshape(1, -1)
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
        ordered_probs = sorted(class_probabilities.values(), reverse=True)
        top_prob = ordered_probs[0] if ordered_probs else 0.0
        margin = top_prob - (ordered_probs[1] if len(ordered_probs) > 1 else 0.0)
        confidence = ((top_prob * 0.72) + (margin * 0.28)) * 100.0
        probability = sum(class_probabilities[label] * label_to_risk(label) for label in LABELS)

    probability = max(0.0, min(100.0, float(probability)))
    diagnosis = diagnose_root_cause(window, thresholds, predicted_class)
    reason = ", ".join(diagnosis["reasons"])

    safe_to_heal = False
    recommended_action = "monitor_only"
    if predicted_class == "CRITICAL":
        recommended_action = "prepare_manual_review"
    elif predicted_class == "WARNING":
        recommended_action = "increase_observation"

    return {
        "risk": round(probability, 4),
        "probability": round(probability, 4),
        "confidence": round(float(confidence), 4),
        "class": predicted_class,
        "reason": reason,
        "root_cause": diagnosis,
        "recommended_action": recommended_action,
        "safe_to_heal": safe_to_heal,
        "class_probabilities": {k: round(v * 100.0, 4) for k, v in class_probabilities.items()},
        "feature_count": len(FEATURE_NAMES),
        "model_readiness": metadata.get("readiness_status", "unknown") if isinstance(metadata, dict) else "unknown",
        "model_generated_at": metadata.get("generated_at", "unknown") if isinstance(metadata, dict) else "unknown",
        "contract": "ai_reliability_v2",
    }


def build_prediction_payload(input_path: str, model_path: str) -> Dict[str, object]:
    metadata = load_model_metadata(model_path)
    model = joblib.load(model_path)
    return build_prediction_payload_from_model(input_path, model, metadata)


def predict_probability_from_runtime_file(input_path: str, model_path: str) -> float:
    return float(build_prediction_payload(input_path, model_path)["risk"])
