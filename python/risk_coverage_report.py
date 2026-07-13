"""Report risk/coverage curves and their finite-sample bounds."""

from __future__ import annotations

from dataclasses import asdict
from typing import Sequence

from selective_risk_certificate import certify_selective_risk


def build_risk_coverage_report(probabilities: Sequence[float], labels: Sequence[int], thresholds: Sequence[float]) -> list[dict[str, object]]:
    if len(probabilities) != len(labels): raise ValueError("probabilities and labels must align")
    rows = []
    for threshold in thresholds:
        accepted = [(p, y) for p, y in zip(probabilities, labels) if p >= threshold or p <= 1.0 - threshold]
        errors = sum(int((p >= 0.5) != bool(y)) for p, y in accepted)
        certificate = certify_selective_risk(errors, len(accepted), len(labels), 0.10, 0.50)
        row = asdict(certificate)
        row["threshold"] = threshold
        rows.append(row)
    return rows
