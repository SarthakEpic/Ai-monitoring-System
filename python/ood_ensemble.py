"""Multi-signal OOD score; no one raw model confidence is authoritative."""

from __future__ import annotations

from dataclasses import dataclass
from math import isfinite
from typing import Mapping


@dataclass(frozen=True)
class OodAssessment:
    score: float
    accepted: bool
    reasons: tuple[str, ...]


def assess_ood(values: Mapping[str, float], reference_medians: Mapping[str, float], reference_scales: Mapping[str, float],
               missing_fraction: float, model_disagreement: float, support_match: bool, threshold: float = 0.25) -> OodAssessment:
    reasons: list[str] = []
    distances: list[float] = []
    for name, median in reference_medians.items():
        value, scale = values.get(name), reference_scales.get(name, 0.0)
        if value is None or not isfinite(value) or scale <= 0.0:
            reasons.append(f"missing_or_invalid:{name}")
            continue
        distances.append(min(1.0, abs(value - median) / (6.0 * scale)))
    if missing_fraction > 0.10: reasons.append("missingness")
    if model_disagreement > 0.20: reasons.append("model_disagreement")
    if not support_match: reasons.append("outside_support_envelope")
    feature_score = sum(distances) / len(distances) if distances else 1.0
    score = min(1.0, 0.50 * feature_score + 0.20 * min(1.0, missing_fraction) + 0.20 * min(1.0, model_disagreement) + (0.10 if not support_match else 0.0))
    return OodAssessment(score, score <= threshold and not reasons, tuple(reasons))
