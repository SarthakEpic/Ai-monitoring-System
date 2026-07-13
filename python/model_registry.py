"""Signed-artifact-ready manifest primitives; model bytes are never trusted by name alone."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from hashlib import sha256
from pathlib import Path
import json
from typing import Mapping


def sha256_file(path: str | Path) -> str:
    digest = sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


@dataclass(frozen=True)
class ModelManifest:
    model_id: str
    model_sha256: str
    feature_schema_hash: str
    stage: str
    feature_count: int
    supported_envelope: Mapping[str, list[str]]
    created_at_utc: str

    @classmethod
    def create(cls, model_path: str | Path, model_id: str, feature_schema_hash: str, stage: str,
               feature_count: int, supported_envelope: Mapping[str, list[str]]) -> "ModelManifest":
        if feature_count <= 0 or not model_id or not feature_schema_hash:
            raise ValueError("model id, feature schema hash, and feature count are required")
        return cls(model_id, sha256_file(model_path), feature_schema_hash, stage, feature_count, dict(supported_envelope), datetime.now(timezone.utc).isoformat())

    def write(self, path: str | Path) -> None:
        Path(path).write_text(json.dumps(asdict(self), indent=2, sort_keys=True) + "\n", encoding="utf-8")
