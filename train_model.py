from __future__ import annotations

import argparse
import json
import os
import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List

import joblib
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from sklearn.model_selection import train_test_split

from labeling import ID_TO_LABEL, LABELS, LABEL_TO_ID, LabelThresholds, label_future_window
from model_features import FEATURE_NAMES, WINDOW, build_features, load_thresholds

HORIZON = 5
PRIORITY_SAMPLE_WEIGHTS = {
    "NORMAL": 1.0,
    "WARNING": 1.0,
    "CRITICAL": 1.5,
    "RECOVERY": 2.35,
}
PRIORITY_AUGMENT_COPIES = {
    "CRITICAL": 2,
    "RECOVERY": 2,
}


def _read_metrics(db_path: str) -> pd.DataFrame:
    conn = sqlite3.connect(db_path)
    try:
        columns = {
            row[1]
            for row in conn.execute("PRAGMA table_info(metrics);").fetchall()
        }
        select_parts = ["time", "cpu", "mem", "disk"]
        if "net_down_kbps" in columns and "net_up_kbps" in columns:
            select_parts.append("COALESCE(net_down_kbps, 0) + COALESCE(net_up_kbps, 0) AS net")
        else:
            select_parts.append("0 AS net")
        select_parts.append("COALESCE(process_count, 0) AS process_count" if "process_count" in columns else "0 AS process_count")
        select_parts.append("COALESCE(top_process, '') AS top_process" if "top_process" in columns else "'' AS top_process")
        select_parts.append("COALESCE(top_process_cpu, 0) AS top_process_cpu" if "top_process_cpu" in columns else "0 AS top_process_cpu")
        select_parts.append("COALESCE(top_process_mem, 0) AS top_process_mem" if "top_process_mem" in columns else "0 AS top_process_mem")

        query = "SELECT " + ", ".join(select_parts) + " FROM metrics ORDER BY time ASC"
        df = pd.read_sql_query(query, conn)
    finally:
        conn.close()
    return df


def _class_distribution(labels: np.ndarray) -> Dict[str, int]:
    return {label: int(np.sum(labels == LABEL_TO_ID[label])) for label in LABELS}


def _sample_weights(labels: np.ndarray) -> np.ndarray:
    return np.asarray(
        [PRIORITY_SAMPLE_WEIGHTS.get(ID_TO_LABEL[int(label)], 1.0) for label in labels],
        dtype=float,
    )


def _augment_priority_samples(X: np.ndarray, y: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    rng = np.random.default_rng(42)
    feature_std = np.std(X, axis=0)
    noise_scale = np.where(feature_std > 0.0, feature_std * 0.015, 0.0)
    extra_X: List[np.ndarray] = []
    extra_y: List[np.ndarray] = []

    for label, copies in PRIORITY_AUGMENT_COPIES.items():
        class_id = LABEL_TO_ID[label]
        class_X = X[y == class_id]
        if len(class_X) == 0:
            continue
        for _ in range(copies):
            noise = rng.normal(0.0, noise_scale, size=class_X.shape)
            extra_X.append(class_X + noise)
            extra_y.append(np.full(len(class_X), class_id, dtype=int))

    if not extra_X:
        return X, y
    return np.vstack([X, *extra_X]), np.concatenate([y, *extra_y])


def _priority_metrics(report: Dict[str, object]) -> Dict[str, float]:
    metrics: Dict[str, float] = {}
    for label in ("CRITICAL", "RECOVERY"):
        label_report = report.get(label, {})
        if isinstance(label_report, dict):
            metrics[f"{label.lower()}_precision"] = float(label_report.get("precision", 0.0))
            metrics[f"{label.lower()}_recall"] = float(label_report.get("recall", 0.0))
            metrics[f"{label.lower()}_f1"] = float(label_report.get("f1-score", 0.0))
    return metrics


def _write_text_report(path: str, report: Dict[str, object]) -> None:
    lines = [
        "AI Model Reliability Report",
        "===========================",
        f"Generated: {report['generated_at']}",
        f"Rows used: {report['rows']}",
        f"Samples: {report['samples']}",
        f"Window: {report['window']}",
        f"Horizon: {report['horizon']}",
        f"Accuracy: {report['accuracy']:.4f}",
        "",
        "Class distribution:",
    ]
    for label, count in report["class_distribution"].items():
        lines.append(f"- {label}: {count}")
    lines.extend(["", "Training sample weights:"])
    for label, weight in report["sample_weights"].items():
        lines.append(f"- {label}: {weight:.2f}")
    lines.extend(["", "Priority augmentation copies:"])
    for label, copies in report["priority_augment_copies"].items():
        lines.append(f"- {label}: {copies}")
    lines.extend(["", "Priority reliability metrics:"])
    for metric, value in report["priority_metrics"].items():
        lines.append(f"- {metric}: {value:.4f}")
    lines.extend(["", "Confusion matrix labels: " + ", ".join(report["labels"]), str(report["confusion_matrix"])])
    lines.extend(["", "Top feature importance:"])
    for item in report["top_features"]:
        lines.append(f"- {item['feature']}: {item['importance']:.4f}")
    lines.extend(["", "Classification report:", report["classification_report_text"]])
    Path(path).write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--db", default="monitor.db")
    parser.add_argument("--model", default="ai_model.joblib")
    parser.add_argument("--meta", default="ai_model_meta.json")
    parser.add_argument("--report-json", default="model_report.json")
    parser.add_argument("--report-txt", default="model_report.txt")
    args = parser.parse_args()

    cpu_th, mem_th, disk_th = load_thresholds("config.txt")
    thresholds = LabelThresholds(cpu=cpu_th, mem=mem_th, disk=disk_th)

    if not os.path.exists(args.db):
        print("monitor.db not found. Run the app first and collect data.")
        return 1

    df = _read_metrics(args.db)
    if len(df) < WINDOW + HORIZON + 30:
        print(f"Not enough rows yet. Need at least {WINDOW + HORIZON + 30}, found {len(df)}.")
        return 1

    arr = df[["cpu", "mem", "disk", "net", "process_count"]].to_numpy(dtype=float)

    X: List[np.ndarray] = []
    y: List[int] = []

    for i in range(WINDOW - 1, len(arr) - HORIZON):
        past = arr[i - WINDOW + 1 : i + 1]
        future = arr[i + 1 : i + HORIZON + 1, :3]
        label = label_future_window(future, thresholds)
        X.append(build_features(past, disk_th, cpu_th, mem_th))
        y.append(LABEL_TO_ID[label])

    X_np = np.vstack(X)
    y_np = np.asarray(y, dtype=int)

    present_classes = sorted(np.unique(y_np).tolist())
    if len(present_classes) < 2:
        print("Training labels ended up with one class only. Collect more varied data and try again.")
        return 1

    stratify = y_np if min(np.bincount(y_np)) >= 2 else None
    X_train, X_test, y_train, y_test = train_test_split(
        X_np,
        y_np,
        test_size=0.25,
        random_state=42,
        stratify=stratify,
    )

    model = RandomForestClassifier(
        n_estimators=640,
        max_depth=14,
        min_samples_leaf=1,
        class_weight={
            LABEL_TO_ID["NORMAL"]: 1.0,
            LABEL_TO_ID["WARNING"]: 1.0,
            LABEL_TO_ID["CRITICAL"]: 1.5,
            LABEL_TO_ID["RECOVERY"]: 2.0,
        },
        random_state=42,
        n_jobs=1,
    )
    X_fit, y_fit = _augment_priority_samples(X_train, y_train)
    model.fit(X_fit, y_fit, sample_weight=_sample_weights(y_fit))

    preds = model.predict(X_test)
    accuracy = accuracy_score(y_test, preds)
    target_names = [ID_TO_LABEL[idx] for idx in present_classes]
    report_dict = classification_report(
        y_test,
        preds,
        labels=present_classes,
        target_names=target_names,
        digits=4,
        zero_division=0,
        output_dict=True,
    )
    report_text = classification_report(
        y_test,
        preds,
        labels=present_classes,
        target_names=target_names,
        digits=4,
        zero_division=0,
    )
    matrix = confusion_matrix(y_test, preds, labels=present_classes).tolist()
    priority_metrics = _priority_metrics(report_dict)

    importances = getattr(model, "feature_importances_", np.zeros(len(FEATURE_NAMES)))
    top_features = sorted(
        [
            {"feature": feature, "importance": float(importance)}
            for feature, importance in zip(FEATURE_NAMES, importances)
        ],
        key=lambda item: item["importance"],
        reverse=True,
    )[:12]

    joblib.dump(model, args.model)

    generated_at = datetime.now(timezone.utc).isoformat()
    meta: Dict[str, object] = {
        "contract": "ai_reliability_v2",
        "generated_at": generated_at,
        "window": WINDOW,
        "horizon": HORIZON,
        "cpu_threshold": cpu_th,
        "mem_threshold": mem_th,
        "disk_threshold": disk_th,
        "rows": len(df),
        "samples": len(X_np),
        "feature_count": len(FEATURE_NAMES),
        "features": FEATURE_NAMES,
        "labels": LABELS,
        "sample_weights": PRIORITY_SAMPLE_WEIGHTS,
        "priority_augment_copies": PRIORITY_AUGMENT_COPIES,
    }

    report: Dict[str, object] = {
        **meta,
        "accuracy": float(accuracy),
        "class_distribution": _class_distribution(y_np),
        "test_class_distribution": _class_distribution(y_test),
        "confusion_matrix": matrix,
        "classification_report": report_dict,
        "classification_report_text": report_text,
        "priority_metrics": priority_metrics,
        "top_features": top_features,
    }

    with open(args.meta, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)
    with open(args.report_json, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    _write_text_report(args.report_txt, report)

    print(f"Rows used: {len(df)}")
    print(f"Samples: {len(X_np)}")
    print(f"Accuracy: {accuracy:.4f}")
    for metric, value in priority_metrics.items():
        print(f"{metric}: {value:.4f}")
    print(report_text)
    print(f"Saved model to {args.model}")
    print(f"Saved meta to {args.meta}")
    print(f"Saved JSON report to {args.report_json}")
    print(f"Saved text report to {args.report_txt}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
