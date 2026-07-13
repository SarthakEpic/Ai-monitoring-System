# Observer-Effect Governor

The monitor must not make a constrained computer slower while trying to help it.
`ObserverEffectGovernor` evaluates the monitor's own QoE collector p95,
inference p95, process CPU, process I/O, wakeups, working set, and pending
SQLite writes.

When a budget is exceeded it:

- caps optional evidence at Tier 1;
- disables Tier 3 optional evidence;
- slows optional collection through the evidence budget decision;
- records the transition in `runtime_events.jsonl`.

It does not disable severe-pressure/safety evidence, safety monitoring, or the
rollback watchdog path. Recovery requires consecutive healthy observations so
the system does not oscillate at a threshold.

Current configurable defaults in `config.txt`:

- `OBSERVER_MAX_COLLECTOR_P95_MS=15`
- `OBSERVER_MAX_INFERENCE_P95_MS=20`
- `OBSERVER_MAX_PROCESS_CPU_PERCENT=1.0`
- `OBSERVER_MAX_PROCESS_IO_BPS=1048576`
- `OBSERVER_MAX_WAKEUPS_PER_SEC=2.0`
- `OBSERVER_MAX_WORKING_SET_MB=100`
- `OBSERVER_MAX_PENDING_WRITES=64`
- `EVIDENCE_MIN_VALUE_PER_COST=0.20`

Every runtime-health interval persists the latest observer measurements to
`runtime_performance_samples`: collector and inference p95, monitor CPU and
I/O rate, wakeups, working set, pending writes, observer state, and full
storage-batch latency. `tools/collect_baseline.ps1` reads the latest sample
through `runtime_performance_summary.py`. Before the monitor writes a sample,
the baseline explicitly reports `not_available`; it never invents a value.