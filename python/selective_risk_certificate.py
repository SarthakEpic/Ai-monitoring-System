"""Finite-sample Wilson bounds used to certify selective predictions."""

from __future__ import annotations

from dataclasses import dataclass
from math import sqrt


def wilson_interval(successes: int, total: int, z: float = 1.96) -> tuple[float, float]:
    if total <= 0 or successes < 0 or successes > total:
        raise ValueError("invalid binomial count")
    proportion = successes / total
    denominator = 1.0 + z * z / total
    center = (proportion + z * z / (2.0 * total)) / denominator
    margin = z * sqrt((proportion * (1.0 - proportion) + z * z / (4.0 * total)) / total) / denominator
    return max(0.0, center - margin), min(1.0, center + margin)


@dataclass(frozen=True)
class SelectiveRiskCertificate:
    accepted: bool
    accepted_count: int
    total_count: int
    error_upper_bound: float
    coverage_lower_bound: float
    reason: str


def certify_selective_risk(accepted_errors: int, accepted_count: int, total_count: int,
                           maximum_error: float, minimum_coverage: float) -> SelectiveRiskCertificate:
    if accepted_count <= 0 or total_count <= 0 or accepted_count > total_count:
        return SelectiveRiskCertificate(False, accepted_count, total_count, 1.0, 0.0, "invalid_or_empty_evidence")
    _, error_upper = wilson_interval(accepted_errors, accepted_count)
    coverage_lower, _ = wilson_interval(accepted_count, total_count)
    accepted = error_upper <= maximum_error and coverage_lower >= minimum_coverage
    reason = "certified" if accepted else "risk_or_coverage_bound_failed"
    return SelectiveRiskCertificate(accepted, accepted_count, total_count, error_upper, coverage_lower, reason)
