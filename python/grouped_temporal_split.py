"""Leakage-resistant, episode-grouped temporal data splits for Aegis-99."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Mapping, Sequence


SPLIT_ROLES = ("train", "validation", "calibration", "locked_test")


class LeakageError(ValueError):
    """Raised when records could leak across evaluation roles."""


@dataclass(frozen=True)
class EpisodeDescriptor:
    episode_id: str
    device_id: str
    session_id: str
    workload_class: str
    application_family: str
    start_monotonic_ms: int
    end_monotonic_ms: int


@dataclass(frozen=True)
class SplitResult:
    roles: Mapping[str, tuple[EpisodeDescriptor, ...]]
    purged_episode_ids: tuple[str, ...]

    def ids(self, role: str) -> set[str]:
        return {episode.episode_id for episode in self.roles.get(role, ())}


def descriptor_from_record(record: Mapping[str, object]) -> EpisodeDescriptor:
    try:
        descriptor = EpisodeDescriptor(
            episode_id=str(record["episode_id"]),
            device_id=str(record["device_id"]),
            session_id=str(record["session_id"]),
            workload_class=str(record.get("workload_class", "UNKNOWN")),
            application_family=str(record.get("application_family", "UNKNOWN")),
            start_monotonic_ms=int(record["start_monotonic_ms"]),
            end_monotonic_ms=int(record["end_monotonic_ms"]),
        )
    except (KeyError, TypeError, ValueError) as error:
        raise LeakageError(f"invalid episode descriptor: {error}") from error
    if not descriptor.episode_id or descriptor.end_monotonic_ms <= descriptor.start_monotonic_ms:
        raise LeakageError("episode must have an ID and a positive interval")
    return descriptor


def _group_key(episode: EpisodeDescriptor, group_by: str) -> str:
    keys = {
        "episode": episode.episode_id,
        "device": episode.device_id,
        "session": episode.session_id,
        "workload": episode.workload_class,
        "application": episode.application_family,
    }
    try:
        return keys[group_by]
    except KeyError as error:
        raise LeakageError(f"unsupported group_by: {group_by}") from error


def validate_episode_descriptors(episodes: Iterable[EpisodeDescriptor]) -> tuple[EpisodeDescriptor, ...]:
    normalized = tuple(episodes)
    seen: dict[str, EpisodeDescriptor] = {}
    for episode in normalized:
        existing = seen.get(episode.episode_id)
        if existing and existing != episode:
            raise LeakageError(f"episode ID has contradictory metadata: {episode.episode_id}")
        seen[episode.episode_id] = episode
    return tuple(seen.values())


def _role_counts(group_count: int, fractions: Mapping[str, float]) -> dict[str, int]:
    if group_count < len(SPLIT_ROLES):
        raise LeakageError("need at least one independent group for every split role")
    if set(fractions) != set(SPLIT_ROLES):
        raise LeakageError("fractions must define train, validation, calibration, and locked_test")
    if abs(sum(fractions.values()) - 1.0) > 1e-9:
        raise LeakageError("split fractions must sum to 1")
    if any(value <= 0.0 for value in fractions.values()):
        raise LeakageError("every split role needs a positive fraction")

    counts = {role: max(1, int(group_count * fractions[role])) for role in SPLIT_ROLES}
    while sum(counts.values()) > group_count:
        role = max(SPLIT_ROLES, key=lambda candidate: (counts[candidate], fractions[candidate]))
        if counts[role] <= 1:
            raise LeakageError("not enough groups to allocate every role")
        counts[role] -= 1
    while sum(counts.values()) < group_count:
        role = max(SPLIT_ROLES, key=lambda candidate: fractions[candidate])
        counts[role] += 1
    return counts


def split_grouped_temporal(
    episodes: Sequence[EpisodeDescriptor],
    *,
    group_by: str = "episode",
    fractions: Mapping[str, float] | None = None,
    purge_gap_ms: int = 0,
) -> SplitResult:
    """Assign whole groups chronologically and purge boundary-adjacent episodes.

    The caller chooses the disjointness axis. Certification workflows should run
    separate evaluations with `device`, `workload`, and `application` grouping.
    """

    normalized = validate_episode_descriptors(episodes)
    fractions = fractions or {
        "train": 0.60,
        "validation": 0.15,
        "calibration": 0.15,
        "locked_test": 0.10,
    }
    grouped: dict[str, list[EpisodeDescriptor]] = {}
    for episode in normalized:
        grouped.setdefault(_group_key(episode, group_by), []).append(episode)
    groups = sorted(grouped.values(), key=lambda values: min(item.start_monotonic_ms for item in values))
    counts = _role_counts(len(groups), fractions)

    roles: dict[str, list[EpisodeDescriptor]] = {role: [] for role in SPLIT_ROLES}
    index = 0
    for role in SPLIT_ROLES:
        for group in groups[index:index + counts[role]]:
            roles[role].extend(group)
        index += counts[role]

    purged: list[str] = []
    if purge_gap_ms > 0:
        previous_end = -1
        for role in SPLIT_ROLES:
            retained: list[EpisodeDescriptor] = []
            for episode in sorted(roles[role], key=lambda item: item.start_monotonic_ms):
                if previous_end >= 0 and episode.start_monotonic_ms < previous_end + purge_gap_ms:
                    purged.append(episode.episode_id)
                    continue
                retained.append(episode)
            if retained:
                previous_end = max(previous_end, max(item.end_monotonic_ms for item in retained))
            roles[role] = retained

    result = SplitResult({role: tuple(roles[role]) for role in SPLIT_ROLES}, tuple(sorted(set(purged))))
    validate_split_result(result, required_disjoint_fields=(group_by,) if group_by != "episode" else ())
    return result


def validate_split_result(
    result: SplitResult,
    *,
    required_disjoint_fields: Sequence[str] = (),
) -> None:
    seen_ids: dict[str, str] = {}
    field_values: dict[str, dict[str, str]] = {field: {} for field in required_disjoint_fields}
    for role in SPLIT_ROLES:
        for episode in result.roles.get(role, ()):
            previous = seen_ids.get(episode.episode_id)
            if previous is not None:
                raise LeakageError(f"episode {episode.episode_id} appears in both {previous} and {role}")
            seen_ids[episode.episode_id] = role
            for field in required_disjoint_fields:
                value = _group_key(episode, field)
                previous_role = field_values[field].get(value)
                if previous_role is not None and previous_role != role:
                    raise LeakageError(f"{field} value {value} leaks between {previous_role} and {role}")
                field_values[field][value] = role


def assert_no_window_overlap(assignments: Mapping[str, Sequence[Mapping[str, object]]]) -> None:
    """Reject window-level data that shares an episode between roles."""

    episode_roles: dict[str, str] = {}
    for role, rows in assignments.items():
        if role not in SPLIT_ROLES:
            raise LeakageError(f"unknown role: {role}")
        for row in rows:
            episode_id = str(row.get("episode_id", ""))
            if not episode_id:
                raise LeakageError("window row is missing episode_id")
            previous = episode_roles.get(episode_id)
            if previous is not None and previous != role:
                raise LeakageError(f"episode {episode_id} has overlapping windows in {previous} and {role}")
            episode_roles[episode_id] = role
