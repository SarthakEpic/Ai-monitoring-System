"""Calibration is fitted only on a held-out calibration partition."""

from __future__ import annotations

from dataclasses import dataclass
from math import exp, log
from typing import Iterable, Sequence


def _clip(value: float) -> float:
    return min(1.0 - 1e-6, max(1e-6, float(value)))


def _log_loss(probabilities: Sequence[float], labels: Sequence[int]) -> float:
    if not probabilities:
        return float("inf")
    return -sum(label * log(_clip(probability)) + (1 - label) * log(1 - _clip(probability))
                for probability, label in zip(probabilities, labels)) / len(probabilities)


@dataclass(frozen=True)
class CalibrationResult:
    method: str
    parameters: dict[str, float]
    calibration_log_loss: float


class ProbabilityCalibrator:
    def __init__(self, result: CalibrationResult) -> None:
        self.result = result

    def transform(self, probability: float) -> float:
        probability = _clip(probability)
        if self.result.method == "identity":
            return probability
        if self.result.method == "temperature":
            temperature = self.result.parameters["temperature"]
            logit = log(probability / (1.0 - probability))
            return _clip(1.0 / (1.0 + exp(-logit / temperature)))
        if self.result.method == "sigmoid":
            logit = log(probability / (1.0 - probability))
            return _clip(1.0 / (1.0 + exp(-(self.result.parameters["a"] * logit + self.result.parameters["b"]))))
        raise ValueError(f"unsupported calibration method: {self.result.method}")


def fit_temperature(probabilities: Sequence[float], labels: Sequence[int]) -> ProbabilityCalibrator:
    if len(probabilities) != len(labels) or not probabilities:
        raise ValueError("calibration probabilities and labels must be non-empty and aligned")
    best_temperature = 1.0
    best_loss = float("inf")
    # Deliberately small deterministic search; no locked-test data is consulted.
    for step in range(10, 401):
        temperature = step / 100.0
        candidate = ProbabilityCalibrator(CalibrationResult("temperature", {"temperature": temperature}, 0.0))
        loss = _log_loss([candidate.transform(value) for value in probabilities], labels)
        if loss < best_loss:
            best_temperature, best_loss = temperature, loss
    return ProbabilityCalibrator(CalibrationResult("temperature", {"temperature": best_temperature}, best_loss))


def fit_sigmoid(probabilities: Sequence[float], labels: Sequence[int], iterations: int = 500) -> ProbabilityCalibrator:
    if len(probabilities) != len(labels) or not probabilities:
        raise ValueError("calibration probabilities and labels must be non-empty and aligned")
    a, b = 1.0, 0.0
    logits = [log(_clip(value) / (1.0 - _clip(value))) for value in probabilities]
    for _ in range(iterations):
        transformed = [1.0 / (1.0 + exp(-(a * value + b))) for value in logits]
        error_a = sum((prediction - label) * value for prediction, label, value in zip(transformed, labels, logits)) / len(labels)
        error_b = sum(prediction - label for prediction, label in zip(transformed, labels)) / len(labels)
        a -= 0.05 * error_a
        b -= 0.05 * error_b
    calibrator = ProbabilityCalibrator(CalibrationResult("sigmoid", {"a": a, "b": b}, 0.0))
    return ProbabilityCalibrator(CalibrationResult("sigmoid", {"a": a, "b": b}, _log_loss([calibrator.transform(p) for p in probabilities], labels)))


def select_calibrator(calibration_probabilities: Sequence[float], calibration_labels: Sequence[int]) -> ProbabilityCalibrator:
    """Pick only from calibration-split candidates; callers must retain locked test data."""
    identity = ProbabilityCalibrator(CalibrationResult("identity", {}, _log_loss(calibration_probabilities, calibration_labels)))
    candidates = [identity, fit_temperature(calibration_probabilities, calibration_labels), fit_sigmoid(calibration_probabilities, calibration_labels)]
    return min(candidates, key=lambda candidate: candidate.result.calibration_log_loss)
