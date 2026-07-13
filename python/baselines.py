"""Transparent baselines required for Aegis-99 episode evaluation."""

from __future__ import annotations

import math
from collections import Counter
from dataclasses import dataclass
from typing import Mapping, Sequence


@dataclass(frozen=True)
class BaselinePrediction:
    label: int
    probability: float


def majority_no_event(labels: Sequence[int]) -> BaselinePrediction:
    if not labels:
        raise ValueError("labels cannot be empty")
    event_rate = sum(labels) / len(labels)
    return BaselinePrediction(int(event_rate >= 0.5), event_rate)


def deterministic_resource_rule(record: Mapping[str, object]) -> BaselinePrediction:
    """Research baseline only, never an outcome label or authorization rule."""
    resources = record.get("resources", {})
    if not isinstance(resources, Mapping):
        return BaselinePrediction(0, 0.0)
    cpu = float(resources.get("cpu_percent", 0.0))
    memory = float(resources.get("memory_percent", 0.0))
    disk_free = float(resources.get("disk_free_percent", 100.0))
    score = min(1.0, max(0.0, 0.45 * cpu / 100.0 + 0.45 * memory / 100.0 + 0.10 * (100.0 - disk_free) / 100.0))
    return BaselinePrediction(int(score >= 0.70), score)


def persistence(previous_label: int | None, fallback_event_rate: float) -> BaselinePrediction:
    probability = fallback_event_rate if previous_label is None else float(previous_label)
    return BaselinePrediction(int(probability >= 0.5), probability)


def ewma_probability(values: Sequence[float], alpha: float = 0.3) -> float:
    if not values:
        return 0.0
    if not 0.0 < alpha <= 1.0:
        raise ValueError("alpha must be in (0, 1]")
    estimate = values[0]
    for value in values[1:]:
        estimate = alpha * value + (1.0 - alpha) * estimate
    return min(1.0, max(0.0, estimate))


def logistic_regression_baseline(*_args: object, **_kwargs: object) -> None:
    """Reserved optional baseline; avoid silently training an untested model.

    The production evaluation runner must explicitly install and record the
    exact scikit-learn version before invoking this optional baseline.
    """
    raise RuntimeError("logistic regression baseline requires the pinned offline ML environment")


def monotonic_boosted_tree_baseline(*_args: object, **_kwargs: object) -> None:
    raise RuntimeError("monotonic boosted-tree baseline requires the pinned offline ML environment")
