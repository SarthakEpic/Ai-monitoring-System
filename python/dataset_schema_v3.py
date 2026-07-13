"""Versioned, privacy-minimized contracts for Aegis-99 offline data.

These validators intentionally use only the Python standard library so data
collection tooling can reject invalid records before optional ML packages load.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any, Mapping, Sequence


SCHEMA_VERSION = 3
PRIMARY_OUTCOMES = {"USER_IMPACT_EVENT", "NO_USER_IMPACT_EVENT"}
PROVENANCE_TYPES = {
    "SYNTHETIC",
    "CONTROLLED_LAB",
    "WEAK_HEURISTIC",
    "MANUAL_REVIEW",
    "MEASURED_QOE",
}


class ValidationError(ValueError):
    """Raised when a record violates the v3 data contract."""


class LabelProvenance(str, Enum):
    SYNTHETIC = "SYNTHETIC"
    CONTROLLED_LAB = "CONTROLLED_LAB"
    WEAK_HEURISTIC = "WEAK_HEURISTIC"
    MANUAL_REVIEW = "MANUAL_REVIEW"
    MEASURED_QOE = "MEASURED_QOE"


@dataclass(frozen=True)
class ValidationResult:
    record_type: str
    schema_version: int
    certification_eligible: bool


def _require(record: Mapping[str, Any], key: str, expected_type: type | tuple[type, ...] | None = None) -> Any:
    if key not in record or record[key] in (None, ""):
        raise ValidationError(f"missing required field: {key}")
    value = record[key]
    if expected_type is not None and not isinstance(value, expected_type):
        raise ValidationError(f"field {key} has invalid type: expected {expected_type}, got {type(value)}")
    return value


def _validate_common(record: Mapping[str, Any]) -> None:
    version = _require(record, "schema_version", int)
    if version != SCHEMA_VERSION:
        raise ValidationError(f"expected schema_version {SCHEMA_VERSION}, got {version}")
    _require(record, "monotonic_timestamp_ms", int)
    _require(record, "wall_timestamp_utc", str)
    _require(record, "device_id", str)
    _require(record, "session_id", str)
    _require(record, "episode_id", str)
    _require(record, "windows_build_family", str)
    _require(record, "hardware", Mapping)
    _require(record, "collector_health", Mapping)
    _require(record, "feature_source_versions", Mapping)


def _validate_provenance(record: Mapping[str, Any]) -> LabelProvenance:
    provenance = str(_require(record, "provenance", str)).upper()
    if provenance not in PROVENANCE_TYPES:
        raise ValidationError(f"unsupported provenance: {provenance}")
    return LabelProvenance(provenance)


def certification_eligible(provenance: LabelProvenance | str) -> bool:
    value = provenance if isinstance(provenance, LabelProvenance) else LabelProvenance(str(provenance).upper())
    return value in {LabelProvenance.CONTROLLED_LAB, LabelProvenance.MANUAL_REVIEW, LabelProvenance.MEASURED_QOE}


def validate_telemetry_frame(record: Mapping[str, Any]) -> ValidationResult:
    _validate_common(record)
    _validate_provenance(record)
    _require(record, "foreground", Mapping)
    _require(record, "workload_phase", str)
    resources = _require(record, "resources", Mapping)
    for name in ("cpu_percent", "memory_percent", "disk_free_percent"):
        _require(resources, name, (int, float))
    normalized = _require(record, "normalized_resources", Mapping)
    for name in ("cpu_robust_z", "memory_robust_z", "disk_robust_z"):
        _require(normalized, name, (int, float))
    return ValidationResult("telemetry_frame", SCHEMA_VERSION, False)


def validate_workload_episode(record: Mapping[str, Any]) -> ValidationResult:
    _validate_common(record)
    provenance = _validate_provenance(record)
    _require(record, "workload_class", str)
    _require(record, "start_reason", str)
    _require(record, "end_reason", str)
    start = _require(record, "start_monotonic_ms", int)
    end = _require(record, "end_monotonic_ms", int)
    if end <= start:
        raise ValidationError("episode end must be later than start")
    _require(record, "stable_baseline", Mapping)
    _require(record, "observation_interval_ms", int)
    _require(record, "outcome_interval_ms", int)
    _require(record, "data_quality", Mapping)
    return ValidationResult("workload_episode", SCHEMA_VERSION, certification_eligible(provenance))


def validate_outcome_label(record: Mapping[str, Any], *, for_certification: bool = False) -> ValidationResult:
    _validate_common(record)
    provenance = _validate_provenance(record)
    outcome = str(_require(record, "primary_outcome", str)).upper()
    if outcome not in PRIMARY_OUTCOMES:
        raise ValidationError(f"primary_outcome must be one of {sorted(PRIMARY_OUTCOMES)}")
    _require(record, "label_confidence", (int, float))
    _require(record, "qoe_evidence", Mapping)
    if for_certification and not certification_eligible(provenance):
        raise ValidationError("synthetic and weak-heuristic labels cannot enter certification data")
    return ValidationResult("outcome_label", SCHEMA_VERSION, certification_eligible(provenance))


def validate_action_outcome(record: Mapping[str, Any], *, for_certification: bool = False) -> ValidationResult:
    _validate_common(record)
    provenance = _validate_provenance(record)
    _require(record, "transaction_id", str)
    _require(record, "action_type", str)
    _require(record, "outcome", str)
    _require(record, "rollback", Mapping)
    _require(record, "measured_effect", Mapping)
    if for_certification and not certification_eligible(provenance):
        raise ValidationError("synthetic and weak-heuristic action outcomes cannot enter certification data")
    return ValidationResult("action_outcome", SCHEMA_VERSION, certification_eligible(provenance))


def validate_records(records: Sequence[Mapping[str, Any]], validator: str, *, for_certification: bool = False) -> list[ValidationResult]:
    functions = {
        "telemetry_frame": validate_telemetry_frame,
        "workload_episode": validate_workload_episode,
        "outcome_label": validate_outcome_label,
        "action_outcome": validate_action_outcome,
    }
    if validator not in functions:
        raise ValidationError(f"unknown validator: {validator}")
    function = functions[validator]
    results: list[ValidationResult] = []
    for record in records:
        if validator in {"outcome_label", "action_outcome"}:
            results.append(function(record, for_certification=for_certification))
        else:
            results.append(function(record))
    return results
