"""Drift state is monotonic within a window: it can downgrade but never authorize actions."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from typing import Mapping


class DriftLevel(IntEnum):
    NORMAL = 0
    COLLECT_MORE_EVIDENCE = 1
    RECOMMENDATION_ONLY = 2
    DETERMINISTIC_MONITOR_ONLY = 3
    CERTIFICATE_INVALID = 4


@dataclass(frozen=True)
class DriftAssessment:
    level: DriftLevel
    normalized_shift: float
    coverage_drop: float
    reason: str


def assess_drift(reference_means: Mapping[str, float], reference_scales: Mapping[str, float], current_means: Mapping[str, float],
                 expected_coverage: float, observed_coverage: float) -> DriftAssessment:
    shifts = [abs(current_means[name] - mean) / max(reference_scales.get(name, 0.0), 1e-6)
              for name, mean in reference_means.items() if name in current_means]
    shift = sum(shifts) / len(shifts) if shifts else float("inf")
    drop = max(0.0, expected_coverage - observed_coverage)
    if shift >= 6.0 or drop >= 0.30: return DriftAssessment(DriftLevel.CERTIFICATE_INVALID, shift, drop, "severe_distribution_or_coverage_drift")
    if shift >= 4.0 or drop >= 0.20: return DriftAssessment(DriftLevel.DETERMINISTIC_MONITOR_ONLY, shift, drop, "high_drift")
    if shift >= 2.5 or drop >= 0.10: return DriftAssessment(DriftLevel.RECOMMENDATION_ONLY, shift, drop, "moderate_drift")
    if shift >= 1.5 or drop >= 0.05: return DriftAssessment(DriftLevel.COLLECT_MORE_EVIDENCE, shift, drop, "early_drift")
    return DriftAssessment(DriftLevel.NORMAL, shift, drop, "within_reference_distribution")
