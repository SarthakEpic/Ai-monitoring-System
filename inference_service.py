from __future__ import annotations

import argparse
import json
import time
from pathlib import Path
from typing import Any, Dict

import joblib

from model_features import build_prediction_payload_from_model, load_model_metadata


def _atomic_write_json(path: Path, payload: Dict[str, Any]) -> None:
    temp_path = path.with_suffix(path.suffix + ".tmp")
    temp_path.write_text(json.dumps(payload, separators=(",", ":")), encoding="utf-8")
    temp_path.replace(path)


def _load_model(model_path: Path) -> tuple[object, Dict[str, Any], float]:
    model = joblib.load(model_path)
    if hasattr(model, "n_jobs"):
        model.n_jobs = 1
    metadata = load_model_metadata(str(model_path))
    return model, metadata, model_path.stat().st_mtime


def main() -> int:
    parser = argparse.ArgumentParser(description="Persistent local inference service for PredictiveAutoHeal.")
    parser.add_argument("--input", required=True, help="Runtime features JSON written by the C++ app.")
    parser.add_argument("--model", required=True, help="Trained joblib model path.")
    parser.add_argument("--output", required=True, help="Prediction JSON output path.")
    parser.add_argument("--poll-ms", type=int, default=1000, help="Polling interval in milliseconds.")
    args = parser.parse_args()

    input_path = Path(args.input)
    model_path = Path(args.model)
    output_path = Path(args.output)
    poll_seconds = max(0.2, args.poll_ms / 1000.0)

    model, metadata, model_mtime = _load_model(model_path)
    last_input_mtime = -1.0

    while True:
        try:
            if model_path.exists() and model_path.stat().st_mtime != model_mtime:
                model, metadata, model_mtime = _load_model(model_path)

            if input_path.exists():
                current_input_mtime = input_path.stat().st_mtime
                if current_input_mtime != last_input_mtime:
                    payload = build_prediction_payload_from_model(str(input_path), model, metadata)
                    payload["service"] = "persistent_python"
                    payload["service_ts"] = int(time.time())
                    _atomic_write_json(output_path, payload)
                    last_input_mtime = current_input_mtime
        except Exception as exc:
            _atomic_write_json(output_path, {
                "error": str(exc),
                "service": "persistent_python",
                "service_ts": int(time.time()),
            })

        time.sleep(poll_seconds)


if __name__ == "__main__":
    raise SystemExit(main())
