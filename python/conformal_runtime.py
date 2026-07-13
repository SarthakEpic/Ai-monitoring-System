"""Split-conformal event sets for selective runtime prediction."""

from __future__ import annotations

from dataclasses import dataclass
from math import ceil
from typing import Sequence


@dataclass(frozen=True)
class ConformalPrediction:
    labels: tuple[int, ...]
    threshold: float
    abstain: bool


class SplitConformalBinary:
    def __init__(self, threshold: float, alpha: float) -> None:
        self.threshold = threshold
        self.alpha = alpha

    @classmethod
    def fit(cls, probabilities: Sequence[float], labels: Sequence[int], alpha: float = 0.10) -> "SplitConformalBinary":
        if not 0.0 < alpha < 1.0 or len(probabilities) != len(labels) or not probabilities:
            raise ValueError("valid aligned calibration data and alpha are required")
        scores = sorted((1.0 - probability if label else probability) for probability, label in zip(probabilities, labels))
        index = min(len(scores) - 1, max(0, ceil((len(scores) + 1) * (1.0 - alpha)) - 1))
        return cls(scores[index], alpha)

    def predict(self, event_probability: float, allow_empty_set: bool = False) -> ConformalPrediction:
        labels = tuple(label for label, probability in ((0, 1.0 - event_probability), (1, event_probability))
                       if 1.0 - probability <= self.threshold)
        abstain = not labels or (len(labels) == 2)
        if not labels and not allow_empty_set:
            return ConformalPrediction((0, 1), self.threshold, True)
        return ConformalPrediction(labels, self.threshold, abstain)
