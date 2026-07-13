"""Frozen, end-to-end Aegis-99 locked-certification scorer.

This module intentionally has no option that forces a pass.  It validates the
immutable locked manifest, rejects non-measured labels, recomputes exact
one-sided confidence bounds, and derives each slice status from evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from collections import defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable

from locked_split import load_locked_manifest
from selective_risk_certificate import exact_binomial_bounds

MEASURED_PROVENANCE = {"MEASURED_QOE", "CONTROLLED_LAB", "MANUAL_REVIEW"}
POLICY = {
    "schema_version": 1,
    "confidence": 0.95,
    "correct_lower_minimum": 0.99,
    "coverage_lower_minimum": 0.70,
    "critical_recall_lower_minimum": 0.97,
    "severe_harm_upper_maximum": 0.001,
}


class CertificationError(ValueError):
    pass


@dataclass(frozen=True)
class SliceReport:
    support_slice: str
    eligible_episodes: int
    resolved_episodes: int
    correct_outcomes: int
    correct_lower_bound: float
    coverage_lower_bound: float
    critical_events: int
    critical_detected: int
    critical_recall_lower_bound: float
    committed_actions: int
    severe_harmful_actions: int
    severe_harm_upper_bound: float
    rollback_attempts: int
    rollback_successes: int
    protected_or_foreground_violations: int
    status: str
    failures: tuple[str, ...]


def canonical_hash(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


def load_json(path: str | Path) -> Any:
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _require_bool(row: dict[str, Any], name: str) -> bool:
    value = row.get(name)
    if not isinstance(value, bool):
        raise CertificationError(f"episode {row.get('episode_id', '<missing>')} has non-boolean {name}")
    return value


def validate_locked_episodes(rows: Iterable[dict[str, Any]], manifest_path: str | Path) -> list[dict[str, Any]]:
    manifest = load_locked_manifest(manifest_path)
    values = list(rows)
    ids = [str(row.get("episode_id", "")) for row in values]
    if not all(ids) or len(ids) != len(set(ids)):
        raise CertificationError("locked certification episodes require unique non-empty episode_id values")
    if set(ids) != set(manifest.episode_ids):
        raise CertificationError("episode IDs do not exactly match the immutable locked manifest")
    for row in values:
        if str(row.get("label_provenance", "")) not in MEASURED_PROVENANCE:
            raise CertificationError(f"episode {row['episode_id']} uses non-certification label provenance")
        for name in ("eligible", "accepted", "correct", "critical_event", "critical_detected", "committed_action", "severe_harm", "rollback_attempted", "rollback_succeeded", "protected_policy_violation"):
            _require_bool(row, name)
        if row["accepted"] and not row["eligible"]:
            raise CertificationError(f"episode {row['episode_id']} accepted an ineligible case")
        if row["critical_detected"] and not row["critical_event"]:
            raise CertificationError(f"episode {row['episode_id']} records critical detection without a critical event")
        if row["severe_harm"] and not row["committed_action"]:
            raise CertificationError(f"episode {row['episode_id']} records harm without a committed action")
        if row["rollback_succeeded"] and not row["rollback_attempted"]:
            raise CertificationError(f"episode {row['episode_id']} records rollback success without an attempt")
    return values


def score_slice(name: str, rows: list[dict[str, Any]], policy: dict[str, float] = POLICY) -> SliceReport:
    eligible = [row for row in rows if row["eligible"]]
    resolved = [row for row in eligible if row["accepted"]]
    correct = sum(row["correct"] for row in resolved)
    critical = [row for row in eligible if row["critical_event"]]
    actions = [row for row in rows if row["committed_action"]]
    rollbacks = [row for row in rows if row["rollback_attempted"]]
    violations = sum(row["protected_policy_violation"] for row in rows)
    failures: list[str] = []
    correct_lower = 0.0
    coverage_lower = 0.0
    critical_lower = 0.0
    harm_upper = 1.0
    if not eligible:
        failures.append("no_eligible_episodes")
    if not resolved:
        failures.append("no_resolved_episodes")
    else:
        correct_lower, _ = exact_binomial_bounds(correct, len(resolved), policy["confidence"])
        if correct_lower < policy["correct_lower_minimum"]:
            failures.append("correct_outcome_lower_bound_failed")
    if eligible:
        coverage_lower, _ = exact_binomial_bounds(len(resolved), len(eligible), policy["confidence"])
        if coverage_lower < policy["coverage_lower_minimum"]:
            failures.append("coverage_lower_bound_failed")
    if not critical:
        failures.append("no_critical_user_impact_episodes")
    else:
        detected = sum(row["critical_detected"] for row in critical)
        critical_lower, _ = exact_binomial_bounds(detected, len(critical), policy["confidence"])
        if critical_lower < policy["critical_recall_lower_minimum"]:
            failures.append("critical_recall_lower_bound_failed")
    harms = sum(row["severe_harm"] for row in actions)
    if not actions:
        failures.append("no_committed_action_evidence")
    else:
        _, harm_upper = exact_binomial_bounds(harms, len(actions), policy["confidence"])
        if harm_upper >= policy["severe_harm_upper_maximum"]:
            failures.append("severe_harm_upper_bound_failed")
    rollback_successes = sum(row["rollback_succeeded"] for row in rollbacks)
    if not rollbacks:
        failures.append("no_injected_rollback_evidence")
    elif rollback_successes != len(rollbacks):
        failures.append("rollback_failure")
    if violations:
        failures.append("protected_or_foreground_policy_violation")
    return SliceReport(name, len(eligible), len(resolved), correct, correct_lower, coverage_lower, len(critical), sum(row["critical_detected"] for row in critical), critical_lower, len(actions), harms, harm_upper, len(rollbacks), rollback_successes, violations, "CERTIFIED_AUTOMATIC" if not failures else "NOT_CERTIFIED", tuple(failures))


def build_report(rows: list[dict[str, Any]], manifest_path: str | Path, release_manifest: dict[str, Any]) -> dict[str, Any]:
    validated = validate_locked_episodes(rows, manifest_path)
    if release_manifest.get("frozen") is not True or not release_manifest.get("artifact_hashes"):
        raise CertificationError("release manifest is not frozen with artifact hashes")
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in validated:
        grouped[str(row.get("support_slice") or "unspecified")].append(row)
    slices = [asdict(score_slice(name, values)) for name, values in sorted(grouped.items())]
    overall = asdict(score_slice("global", validated))
    status = "CERTIFIED_AUTOMATIC" if overall["status"] == "CERTIFIED_AUTOMATIC" and all(item["status"] == "CERTIFIED_AUTOMATIC" for item in slices) else "NOT_CERTIFIED"
    return {"schema_version": 1, "status": status, "policy": POLICY, "locked_manifest_sha256": hashlib.sha256(Path(manifest_path).read_bytes()).hexdigest(), "release_manifest_hash": canonical_hash(release_manifest), "overall": overall, "slices": slices, "reproduction": "python python/certification_runner.py --episodes <locked_episodes.json> --locked-manifest <locked_manifest.json> --release-manifest <release_manifest.json> --output <report.json>"}


def main() -> int:
    parser = argparse.ArgumentParser(description="Run frozen Aegis-99 locked certification.")
    parser.add_argument("--episodes", required=True)
    parser.add_argument("--locked-manifest", required=True)
    parser.add_argument("--release-manifest", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        rows = load_json(args.episodes)
        if not isinstance(rows, list):
            raise CertificationError("episodes input must be a JSON list")
        report = build_report(rows, args.locked_manifest, load_json(args.release_manifest))
    except (OSError, ValueError, CertificationError) as error:
        report = {"schema_version": 1, "status": "NOT_CERTIFIED", "fatal_error": str(error)}
    Path(args.output).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Certification status: {report['status']}")
    return 0 if report["status"] == "CERTIFIED_AUTOMATIC" else 2


if __name__ == "__main__":
    raise SystemExit(main())
