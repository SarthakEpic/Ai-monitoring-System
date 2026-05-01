import argparse
import sys

from model_features import predict_probability_from_runtime_file


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--model", default="ai_model.joblib")
    args = parser.parse_args()

    try:
        probability = predict_probability_from_runtime_file(args.input, args.model)
    except Exception as exc:
        print(f"prediction failed: {exc}", file=sys.stderr)
        return 1

    print(f"{probability:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
