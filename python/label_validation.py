"""Outcome-label gates for leakage-safe Aegis-99 training data."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping, Sequence

from .dataset_schema_v3 import PRIMARY_OUTCOMES, ValidationError, validate_outcome_label


@dataclass(frozen=True)
class LabelValidationReport:
    total: int
    certification_eligible: int
    class_counts: Mapping[str, int]


def validate_outcome_labels(
    labels: Sequence[Mapping[str, object]], *, for_certification: bool = False
) -> LabelValidationReport:
    """Validate labels and reject contradictory labels for one episode.

    Resource pressure is deliberately not read here. A label must carry QoE
    evidence through the v3 contract; certification additionally rejects weak
    heuristic and synthetic provenance.
    """

    if not labels:
        raise ValidationError("at least one outcome label is required")
    outcomes_by_episode: dict[str, str] = {}
    class_counts = {outcome: 0 for outcome in PRIMARY_OUTCOMES}
    eligible = 0
    for label in labels:
        result = validate_outcome_label(label, for_certification=for_certification)
        episode_id = str(label["episode_id"])
        outcome = str(label["primary_outcome"]).upper()
        if outcomes_by_episode.get(episode_id, outcome) != outcome:
            raise ValidationError(f"episode {episode_id} has contradictory primary outcomes")
        outcomes_by_episode[episode_id] = outcome
        class_counts[outcome] += 1
        eligible += int(result.certification_eligible)
    if for_certification and eligible != len(labels):
        raise ValidationError("certification labels must all have measured or reviewed provenance")
    return LabelValidationReport(len(labels), eligible, class_counts)
