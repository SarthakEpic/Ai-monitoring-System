"""Exact finite-sample binomial bounds for selective-risk certification.

The release criteria require one-sided exact (Clopper-Pearson) confidence
bounds.  Wilson intervals remain useful for descriptive dashboards but must
not decide whether a support slice may enter autonomous mode.
"""

from __future__ import annotations

from dataclasses import dataclass
from math import exp, lgamma, log


def _beta_fraction(a: float, b: float, x: float) -> float:
    """Continued fraction for the regularized incomplete beta function."""
    maximum_iterations = 256
    epsilon = 3.0e-14
    minimum = 1.0e-300
    qab, qap, qam = a + b, a + 1.0, a - 1.0
    c = 1.0
    d = 1.0 - qab * x / qap
    d = minimum if abs(d) < minimum else d
    d = 1.0 / d
    value = d
    for iteration in range(1, maximum_iterations + 1):
        twice = 2 * iteration
        numerator = iteration * (b - iteration) * x / ((qam + twice) * (a + twice))
        d = 1.0 + numerator * d
        d = minimum if abs(d) < minimum else d
        c = 1.0 + numerator / c
        c = minimum if abs(c) < minimum else c
        d = 1.0 / d
        value *= d * c

        numerator = -(a + iteration) * (qab + iteration) * x / ((a + twice) * (qap + twice))
        d = 1.0 + numerator * d
        d = minimum if abs(d) < minimum else d
        c = 1.0 + numerator / c
        c = minimum if abs(c) < minimum else c
        d = 1.0 / d
        delta = d * c
        value *= delta
        if abs(delta - 1.0) <= epsilon:
            return value
    raise ArithmeticError("incomplete beta continued fraction did not converge")


def regularized_incomplete_beta(a: float, b: float, x: float) -> float:
    if a <= 0.0 or b <= 0.0:
        raise ValueError("beta parameters must be positive")
    if x <= 0.0:
        return 0.0
    if x >= 1.0:
        return 1.0
    prefix = exp(lgamma(a + b) - lgamma(a) - lgamma(b) + a * log(x) + b * log(1.0 - x))
    if x < (a + 1.0) / (a + b + 2.0):
        return prefix * _beta_fraction(a, b, x) / a
    return 1.0 - prefix * _beta_fraction(b, a, 1.0 - x) / b


def beta_quantile(probability: float, a: float, b: float) -> float:
    if not 0.0 <= probability <= 1.0:
        raise ValueError("probability must be in [0, 1]")
    if probability == 0.0:
        return 0.0
    if probability == 1.0:
        return 1.0
    low, high = 0.0, 1.0
    for _ in range(96):
        midpoint = (low + high) / 2.0
        if regularized_incomplete_beta(a, b, midpoint) < probability:
            low = midpoint
        else:
            high = midpoint
    return (low + high) / 2.0


def exact_binomial_bounds(successes: int, total: int, confidence: float = 0.95) -> tuple[float, float]:
    """Return one-sided exact lower and upper confidence bounds for a proportion."""
    if total <= 0 or successes < 0 or successes > total:
        raise ValueError("invalid binomial count")
    if not 0.0 < confidence < 1.0:
        raise ValueError("confidence must be between zero and one")
    alpha = 1.0 - confidence
    lower = 0.0 if successes == 0 else beta_quantile(alpha, successes, total - successes + 1)
    upper = 1.0 if successes == total else beta_quantile(confidence, successes + 1, total - successes)
    return lower, upper


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
    if (accepted_count <= 0 or total_count <= 0 or accepted_count > total_count or
            accepted_errors < 0 or accepted_errors > accepted_count):
        return SelectiveRiskCertificate(False, accepted_count, total_count, 1.0, 0.0, "invalid_or_empty_evidence")
    coverage_lower, _ = exact_binomial_bounds(accepted_count, total_count)
    _, error_upper = exact_binomial_bounds(accepted_errors, accepted_count)
    accepted = error_upper <= maximum_error and coverage_lower >= minimum_coverage
    reason = "certified" if accepted else "risk_or_coverage_bound_failed"
    return SelectiveRiskCertificate(accepted, accepted_count, total_count, error_upper, coverage_lower, reason)
