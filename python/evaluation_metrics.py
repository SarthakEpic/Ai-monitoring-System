"""Dependency-light episode-level evaluation and uncertainty reporting."""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Iterable, Sequence


@dataclass(frozen=True)
class BinaryEvaluation:
    true_negative: int
    false_positive: int
    false_negative: int
    true_positive: int
    balanced_accuracy: float
    macro_f1: float
    auroc: float
    auprc: float
    brier_score: float
    log_loss: float
    expected_calibration_error: float
    maximum_calibration_error: float
    critical_recall: float
    critical_recall_wilson_lower: float


def _safe_divide(numerator: float, denominator: float) -> float:
    return numerator / denominator if denominator else 0.0


def wilson_lower_bound(successes: int, total: int, z: float = 1.96) -> float:
    if total <= 0:
        return 0.0
    proportion = successes / total
    denominator = 1.0 + z * z / total
    centre = proportion + z * z / (2.0 * total)
    margin = z * math.sqrt((proportion * (1.0 - proportion) + z * z / (4.0 * total)) / total)
    return max(0.0, (centre - margin) / denominator)


def _auroc(labels: Sequence[int], probabilities: Sequence[float]) -> float:
    positives = sum(labels)
    negatives = len(labels) - positives
    if positives == 0 or negatives == 0:
        return 0.0
    ranked = sorted(zip(probabilities, labels))
    rank_sum = 0.0
    index = 0
    while index < len(ranked):
        end = index + 1
        while end < len(ranked) and ranked[end][0] == ranked[index][0]:
            end += 1
        average_rank = (index + 1 + end) / 2.0
        rank_sum += average_rank * sum(label for _, label in ranked[index:end])
        index = end
    return (rank_sum - positives * (positives + 1) / 2.0) / (positives * negatives)


def _auprc(labels: Sequence[int], probabilities: Sequence[float]) -> float:
    positives = sum(labels)
    if positives == 0:
        return 0.0
    ordered = sorted(zip(probabilities, labels), reverse=True)
    true_positive = 0
    area = 0.0
    previous_recall = 0.0
    for index, (_, label) in enumerate(ordered, start=1):
        true_positive += label
        recall = true_positive / positives
        precision = true_positive / index
        area += precision * (recall - previous_recall)
        previous_recall = recall
    return area


def _calibration(labels: Sequence[int], probabilities: Sequence[float], bins: int) -> tuple[float, float]:
    bucket_counts = [0] * bins
    bucket_probabilities = [0.0] * bins
    bucket_outcomes = [0.0] * bins
    for label, probability in zip(labels, probabilities):
        bucket = min(bins - 1, int(probability * bins))
        bucket_counts[bucket] += 1
        bucket_probabilities[bucket] += probability
        bucket_outcomes[bucket] += label
    expected = maximum = 0.0
    for count, probability_sum, outcome_sum in zip(bucket_counts, bucket_probabilities, bucket_outcomes):
        if not count:
            continue
        gap = abs(probability_sum / count - outcome_sum / count)
        expected += gap * count / len(labels)
        maximum = max(maximum, gap)
    return expected, maximum


def evaluate_binary_episode_predictions(
    labels: Sequence[int], probabilities: Sequence[float], *, threshold: float = 0.5, calibration_bins: int = 10
) -> BinaryEvaluation:
    if not labels or len(labels) != len(probabilities):
        raise ValueError("labels and probabilities must be non-empty and equal length")
    if any(label not in {0, 1} for label in labels):
        raise ValueError("binary labels must be 0 or 1")
    bounded = [min(1.0 - 1e-12, max(1e-12, float(value))) for value in probabilities]
    predicted = [int(value >= threshold) for value in bounded]
    true_negative = sum(label == 0 and value == 0 for label, value in zip(labels, predicted))
    false_positive = sum(label == 0 and value == 1 for label, value in zip(labels, predicted))
    false_negative = sum(label == 1 and value == 0 for label, value in zip(labels, predicted))
    true_positive = sum(label == 1 and value == 1 for label, value in zip(labels, predicted))
    recall_negative = _safe_divide(true_negative, true_negative + false_positive)
    recall_positive = _safe_divide(true_positive, true_positive + false_negative)
    f1_negative = _safe_divide(2 * true_negative, 2 * true_negative + false_positive + false_negative)
    f1_positive = _safe_divide(2 * true_positive, 2 * true_positive + false_positive + false_negative)
    calibration = _calibration(labels, bounded, calibration_bins)
    return BinaryEvaluation(
        true_negative, false_positive, false_negative, true_positive,
        (recall_negative + recall_positive) / 2.0,
        (f1_negative + f1_positive) / 2.0,
        _auroc(labels, bounded), _auprc(labels, bounded),
        sum((label - probability) ** 2 for label, probability in zip(labels, bounded)) / len(labels),
        -sum(label * math.log(probability) + (1 - label) * math.log(1 - probability) for label, probability in zip(labels, bounded)) / len(labels),
        calibration[0], calibration[1], recall_positive,
        wilson_lower_bound(true_positive, true_positive + false_negative),
    )


def risk_coverage_curve(probabilities: Sequence[float], labels: Sequence[int], points: int = 10) -> list[dict[str, float]]:
    if len(probabilities) != len(labels):
        raise ValueError("labels and probabilities must align")
    rows: list[dict[str, float]] = []
    confidences = [abs(value - 0.5) * 2.0 for value in probabilities]
    for point in range(1, points + 1):
        threshold = point / points
        accepted = [index for index, confidence in enumerate(confidences) if confidence >= threshold]
        errors = sum(int((probabilities[index] >= 0.5) != bool(labels[index])) for index in accepted)
        rows.append({"confidence_threshold": threshold, "coverage": _safe_divide(len(accepted), len(labels)), "risk": _safe_divide(errors, len(accepted))})
    return rows
