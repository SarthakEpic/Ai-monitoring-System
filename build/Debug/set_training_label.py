from __future__ import annotations

import argparse
from pathlib import Path


VALID_LABELS = {"auto", "normal", "warning", "critical", "recovery"}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Set the scenario label stored with new monitoring rows."
    )
    parser.add_argument(
        "label",
        choices=sorted(VALID_LABELS),
        help="Label to write into training_label.txt for new samples.",
    )
    parser.add_argument(
        "--file",
        default="training_label.txt",
        help="Label file read by the dashboard. Default: training_label.txt",
    )
    args = parser.parse_args()

    path = Path(args.file)
    path.write_text(args.label + "\n", encoding="utf-8")
    print(f"Training label set to: {args.label}")
    print(f"New rows will store scenario_label='{args.label.upper() if args.label != 'auto' else 'AUTO'}'.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
