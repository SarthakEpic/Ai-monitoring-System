import argparse
import json
import os
import sqlite3
from typing import Dict, List, Tuple

import joblib
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, classification_report, roc_auc_score
from sklearn.model_selection import train_test_split

WINDOW = 8
HORIZON = 3
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


def future_risk_score(future: np.ndarray, disk_th: int) -> float:
    future_cpu = float(future[:, 0].max())
    future_mem = float(future[:, 1].max())
    future_disk = float(future[:, 2].min())

    cpu_part = future_cpu / 100.0
    mem_part = future_mem / 100.0
    disk_part = 0.0
    if future_disk < disk_th:
        disk_part = max(0.0, (disk_th - future_disk) / max(1, disk_th))

    score = 0.45 * cpu_part + 0.45 * mem_part + 0.10 * disk_part
    return float(score)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--db", default="monitor.db")
    parser.add_argument("--model", default="ai_model.joblib")
    parser.add_argument("--meta", default="ai_model_meta.json")
    args = parser.parse_args()

    cpu_th, mem_th, disk_th = load_thresholds("config.txt")

    if not os.path.exists(args.db):
        print("monitor.db not found. Run the app first and collect data.")
        return 1

    conn = sqlite3.connect(args.db)
    try:
        df = pd.read_sql_query(
            "SELECT time, cpu, mem, disk FROM metrics ORDER BY time ASC",
            conn,
        )
    finally:
        conn.close()

    if len(df) < WINDOW + HORIZON + 20:
        print(f"Not enough rows yet. Need at least {WINDOW + HORIZON + 20}, found {len(df)}.")
        return 1

    arr = df[["cpu", "mem", "disk"]].to_numpy(dtype=float)

    X: List[np.ndarray] = []
    scores: List[float] = []

    for i in range(WINDOW - 1, len(arr) - HORIZON):
        past = arr[i - WINDOW + 1 : i + 1]
        future = arr[i + 1 : i + HORIZON + 1]
        X.append(build_features(past, disk_th))
        scores.append(future_risk_score(future, disk_th))

    cutoff = max(0.50, float(np.quantile(scores, 0.70)))
    y = np.array([1 if s >= cutoff else 0 for s in scores], dtype=int)

    if len(np.unique(y)) < 2:
        print("Training labels ended up with one class only. Collect more varied data and try again.")
        return 1

    X = np.vstack(X)

    feature_names = [
        "cpu_last", "cpu_mean", "cpu_std", "cpu_min", "cpu_max", "cpu_delta", "cpu_slope",
        "mem_last", "mem_mean", "mem_std", "mem_min", "mem_max", "mem_delta", "mem_slope",
        "disk_last", "disk_mean", "disk_std", "disk_min", "disk_max", "disk_delta", "disk_slope",
        "cpu_mem_gap", "mem_disk_gap", "cpu_disk_gap", "disk_pressure",
    ]

    X_train, X_test, y_train, y_test = train_test_split(
        X,
        y,
        test_size=0.25,
        random_state=42,
        stratify=y,
    )

    model = RandomForestClassifier(
        n_estimators=350,
        max_depth=10,
        min_samples_leaf=2,
        class_weight="balanced_subsample",
        random_state=42,
        n_jobs=-1,
    )

    model.fit(X_train, y_train)

    preds = model.predict(X_test)
    probas = model.predict_proba(X_test)[:, 1]

    print(f"Rows used: {len(df)}")
    print(f"Samples: {len(X)}")
    print(f"Positive cutoff: {cutoff:.4f}")
    print(f"Accuracy: {accuracy_score(y_test, preds):.4f}")

    try:
        print(f"ROC-AUC: {roc_auc_score(y_test, probas):.4f}")
    except Exception:
        pass

    print(classification_report(y_test, preds, digits=4))

    joblib.dump(model, args.model)

    meta: Dict[str, object] = {
        "window": WINDOW,
        "horizon": HORIZON,
        "cpu_threshold": cpu_th,
        "mem_threshold": mem_th,
        "disk_threshold": disk_th,
        "label_cutoff": cutoff,
        "rows": len(df),
        "samples": len(X),
        "feature_count": len(feature_names),
        "features": feature_names,
    }

    with open(args.meta, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)

    print(f"Saved model to {args.model}")
    print(f"Saved meta to {args.meta}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())