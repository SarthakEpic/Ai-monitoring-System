"""Deterministic, harmless adversarial failure-mining corpus generator.

It models safety-relevant event sequences; it never starts stress processes or
changes system resources.  Real hardware workload and soak procedures remain
external release evidence and are deliberately reported separately.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

SCENARIOS = (
    ("collector_failure", ("collector_timeout", "stale_telemetry"), "abstain"),
    ("model_crash", ("risk_detected", "inference_crash"), "monitor_only"),
    ("near_threshold_pressure", ("risk_detected",) * 32, "governor_cools_down"),
    ("pid_reuse", ("proof_valid", "pid_reused", "canary_start"), "reject"),
    ("replay", ("request_accepted", "same_sequence"), "reject"),
    ("lease_expiry", ("canary_started", "client_died", "lease_expired"), "rollback"),
    ("canary_harm", ("proof_valid", "canary_started", "protected_harm"), "rollback"),
    ("ood_confident", ("high_confidence", "ood_detected"), "abstain"),
    ("database_loss", ("canary_started", "ledger_unavailable"), "rollback"),
)


def build_corpus(seed: str) -> dict[str, object]:
    cases = []
    for name, events, expected in SCENARIOS:
        digest = hashlib.sha256((seed + name + "|".join(events)).encode("utf-8")).hexdigest()
        cases.append({"id": f"hard-{digest[:16]}", "name": name, "events": list(events), "expected_safe_outcome": expected, "version": 1})
    return {"schema_version": 1, "seed": seed, "cases": cases, "corpus_hash": hashlib.sha256(json.dumps(cases, sort_keys=True).encode("utf-8")).hexdigest()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", default="aegis99-phase4-v1")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    Path(args.output).write_text(json.dumps(build_corpus(args.seed), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
