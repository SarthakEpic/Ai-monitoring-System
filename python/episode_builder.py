"""Build independent workload episodes from ordered Aegis-99 telemetry frames."""

from __future__ import annotations

from dataclasses import dataclass
from statistics import median
from typing import Any, Mapping, Sequence

from .dataset_schema_v3 import SCHEMA_VERSION, ValidationError


@dataclass(frozen=True)
class EpisodeBuilderConfig:
    maximum_frame_gap_ms: int = 15000
    minimum_frames: int = 3
    baseline_frame_count: int = 5
    default_outcome_interval_ms: int = 5000


@dataclass(frozen=True)
class BuiltEpisode:
    record: Mapping[str, Any]
    frame_count: int


def _resource(frame: Mapping[str, Any], name: str) -> float:
    resources = frame.get("resources", {})
    if not isinstance(resources, Mapping) or name not in resources:
        raise ValidationError(f"telemetry frame is missing resources.{name}")
    try:
        return float(resources[name])
    except (TypeError, ValueError) as error:
        raise ValidationError(f"telemetry frame has invalid resources.{name}") from error


def _median_absolute_deviation(values: Sequence[float], center: float) -> float:
    return float(median([abs(value - center) for value in values])) if values else 0.0


def _percentile(values: Sequence[float], percentile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = round((len(ordered) - 1) * min(1.0, max(0.0, percentile)))
    return float(ordered[index])


def _baseline(frames: Sequence[Mapping[str, Any]], limit: int) -> dict[str, float]:
    selected = list(frames[:max(1, limit)])
    result: dict[str, float] = {}
    for name in ("cpu_percent", "memory_percent", "disk_free_percent"):
        values = [_resource(frame, name) for frame in selected]
        center = float(median(values))
        prefix = name.removesuffix("_percent")
        result[f"{prefix}_median"] = center
        result[f"{prefix}_mad"] = _median_absolute_deviation(values, center)
        result[f"{prefix}_p05"] = _percentile(values, 0.05)
        result[f"{prefix}_p95"] = _percentile(values, 0.95)
    return result


def _frame_key(frame: Mapping[str, Any]) -> tuple[str, str, str, str]:
    foreground = frame.get("foreground", {})
    if not isinstance(foreground, Mapping):
        raise ValidationError("telemetry frame foreground must be an object")
    return (
        str(frame.get("session_id", "")),
        str(frame.get("workload_phase", "UNKNOWN")),
        str(foreground.get("behavior_class", "UNKNOWN")),
        str(foreground.get("app_family", "UNKNOWN")),
    )


def _quality(frames: Sequence[Mapping[str, Any]], minimum_frames: int) -> dict[str, Any]:
    unavailable = []
    for frame in frames:
        health = frame.get("collector_health", {})
        if not isinstance(health, Mapping):
            unavailable.append("collector_health_missing")
            continue
        for collector, status in health.items():
            if str(status).upper() in {"UNAVAILABLE", "FAILED", "INVALID"}:
                unavailable.append(str(collector))
    return {
        "valid": len(frames) >= minimum_frames and not unavailable,
        "frame_count": len(frames),
        "missing_or_unavailable_collectors": sorted(set(unavailable)),
    }


def _build_record(
    frames: Sequence[Mapping[str, Any]],
    index: int,
    start_reason: str,
    end_reason: str,
    config: EpisodeBuilderConfig,
) -> BuiltEpisode:
    first = frames[0]
    last = frames[-1]
    start = int(first["monotonic_timestamp_ms"])
    end = int(last["monotonic_timestamp_ms"])
    if end <= start:
        raise ValidationError("episode timestamps must increase")
    foreground = first.get("foreground", {})
    observation = end - start
    record = {
        "schema_version": SCHEMA_VERSION,
        "monotonic_timestamp_ms": end,
        "wall_timestamp_utc": str(last["wall_timestamp_utc"]),
        "device_id": str(first["device_id"]),
        "session_id": str(first["session_id"]),
        "episode_id": f"{first['session_id']}:{start}:{index}",
        "windows_build_family": str(first["windows_build_family"]),
        "hardware": first["hardware"],
        "collector_health": last["collector_health"],
        "feature_source_versions": last["feature_source_versions"],
        "provenance": str(first.get("provenance", "MEASURED_QOE")),
        "workload_class": str(foreground.get("behavior_class", "UNKNOWN")),
        "start_reason": start_reason,
        "end_reason": end_reason,
        "start_monotonic_ms": start,
        "end_monotonic_ms": end,
        "stable_baseline": _baseline(frames, config.baseline_frame_count),
        "observation_interval_ms": observation,
        "outcome_interval_ms": config.default_outcome_interval_ms,
        "data_quality": _quality(frames, config.minimum_frames),
        "frame_ids": [str(frame.get("frame_id", position)) for position, frame in enumerate(frames)],
        "application_family": str(foreground.get("app_family", "UNKNOWN")),
    }
    return BuiltEpisode(record=record, frame_count=len(frames))


def build_episodes(
    frames: Sequence[Mapping[str, Any]],
    config: EpisodeBuilderConfig = EpisodeBuilderConfig(),
) -> list[BuiltEpisode]:
    """Build non-overlapping episodes from monotonic telemetry frames.

    Boundaries occur on session, foreground behavior/application, workload phase,
    or an explicit timestamp gap. Frames with fewer than the configured minimum
    remain visible as invalid episodes instead of being silently dropped.
    """

    if not frames:
        return []
    ordered = sorted(frames, key=lambda frame: int(frame["monotonic_timestamp_ms"]))
    episodes: list[BuiltEpisode] = []
    current: list[Mapping[str, Any]] = []
    start_reason = "first_frame"
    previous_key: tuple[str, str, str, str] | None = None
    previous_timestamp: int | None = None

    def flush(reason: str) -> None:
        nonlocal current, start_reason
        if current:
            episodes.append(_build_record(current, len(episodes), start_reason, reason, config))
        current = []

    for frame in ordered:
        timestamp = int(frame["monotonic_timestamp_ms"])
        key = _frame_key(frame)
        boundary_reason = ""
        if previous_timestamp is not None:
            if timestamp <= previous_timestamp:
                raise ValidationError("telemetry timestamps must be strictly increasing")
            if timestamp - previous_timestamp > config.maximum_frame_gap_ms:
                boundary_reason = "telemetry_gap"
            elif key[0] != previous_key[0]:
                boundary_reason = "session_changed"
            elif key[1] != previous_key[1]:
                boundary_reason = "workload_phase_changed"
            elif key[2] != previous_key[2] or key[3] != previous_key[3]:
                boundary_reason = "foreground_behavior_changed"
        if boundary_reason:
            flush(boundary_reason)
            start_reason = boundary_reason
        current.append(frame)
        previous_key = key
        previous_timestamp = timestamp

    flush("input_exhausted")
    return episodes
