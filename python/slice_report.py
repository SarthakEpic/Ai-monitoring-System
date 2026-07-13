"""Device/workload/application slice reports for honest model evaluation."""

from __future__ import annotations

from collections import defaultdict
from typing import Mapping, Sequence

from .evaluation_metrics import BinaryEvaluation, evaluate_binary_episode_predictions


def evaluate_slices(
    rows: Sequence[Mapping[str, object]], *, fields: Sequence[str] = ("device_id", "workload_class", "application_family")
) -> dict[str, dict[str, BinaryEvaluation]]:
    grouped: dict[str, dict[str, list[Mapping[str, object]]]] = {field: defaultdict(list) for field in fields}
    for row in rows:
        for field in fields:
            grouped[field][str(row.get(field, "UNKNOWN"))].append(row)
    report: dict[str, dict[str, BinaryEvaluation]] = {}
    for field, slices in grouped.items():
        report[field] = {}
        for value, records in slices.items():
            labels = [int(record["label"]) for record in records]
            probabilities = [float(record["probability"]) for record in records]
            report[field][value] = evaluate_binary_episode_predictions(labels, probabilities)
    return report
