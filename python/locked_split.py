"""Immutable locked-test manifest handling for Aegis-99 evaluation."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Sequence

from .grouped_temporal_split import LeakageError


@dataclass(frozen=True)
class LockedSplitManifest:
    schema_version: int
    purpose: str
    episode_ids: tuple[str, ...]
    content_hash: str

    def to_dict(self) -> dict[str, object]:
        return {
            "schema_version": self.schema_version,
            "purpose": self.purpose,
            "episode_ids": list(self.episode_ids),
            "content_hash": self.content_hash,
        }


def _hash_ids(ids: Sequence[str]) -> str:
    canonical = "\n".join(sorted(ids)).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def create_locked_manifest(episode_ids: Iterable[str]) -> LockedSplitManifest:
    unique = tuple(sorted({str(value) for value in episode_ids if str(value)}))
    if not unique:
        raise LeakageError("locked test manifest cannot be empty")
    return LockedSplitManifest(1, "Aegis-99 independent locked test episodes", unique, _hash_ids(unique))


def write_locked_manifest(path: str | Path, manifest: LockedSplitManifest) -> None:
    Path(path).write_text(json.dumps(manifest.to_dict(), indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_locked_manifest(path: str | Path) -> LockedSplitManifest:
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    manifest = LockedSplitManifest(
        schema_version=int(payload.get("schema_version", 0)),
        purpose=str(payload.get("purpose", "")),
        episode_ids=tuple(str(value) for value in payload.get("episode_ids", [])),
        content_hash=str(payload.get("content_hash", "")),
    )
    if manifest.schema_version != 1 or not manifest.purpose or not manifest.episode_ids:
        raise LeakageError("locked manifest has invalid fields")
    if _hash_ids(manifest.episode_ids) != manifest.content_hash:
        raise LeakageError("locked manifest hash mismatch")
    return manifest


def reject_locked_episode_use(rows: Sequence[Mapping[str, object]], manifest: LockedSplitManifest, *, role: str) -> None:
    if role not in {"train", "validation", "calibration"}:
        raise LeakageError(f"role {role} is not a non-locked training role")
    locked_ids = set(manifest.episode_ids)
    overlaps = sorted({str(row.get("episode_id", "")) for row in rows} & locked_ids)
    if overlaps:
        raise LeakageError(f"locked episodes cannot enter {role}: {', '.join(overlaps)}")
