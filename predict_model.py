from __future__ import annotations

import argparse
import json
import sys

from model_features import build_prediction_payload


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--model", default="ai_model.joblib")
    parser.add_argument("--format", choices=["json", "number"], default="json")
    args = parser.parse_args()

    try:
        payload = build_prediction_payload(args.input, args.model)
    except Exception as exc:
        print(f"prediction failed: {exc}", file=sys.stderr)
        return 1

    if args.format == "number":
        print(f"{float(payload['risk']):.4f}")
    else:
        print(json.dumps(payload, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
