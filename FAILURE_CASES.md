# Failure-Case Register

This register must be updated from real experiments. A failure is evidence, not a result to hide.

| Failure | Detection | Immediate response | Learning use |
|---|---|---|---|
| Wrong process candidate | Foreground/criticality change or user override | Block or roll back | Negative reward; strengthen dependency features |
| Foreground becomes hung | Window response timeout | Immediate rollback | Harmful outcome |
| Page reads spike | QoE counter crosses guardrail | Roll back memory-priority/prefetch action | Context/action penalty |
| CPU contention worsens | Post-action CPU/QoE delta | Roll back | Negative reward |
| Background work starves | Completion penalty exceeds budget | Restore action; lengthen cooldown | Reward penalty |
| Audio/video regression | Dropped-frame/deadline signal | Immediate rollback | Protect workload family |
| Target exits | Process handle/identity check | Mark target exited; never reuse PID | Neutral safety event |
| Rollback API fails | Native return code | Trigger emergency state and user warning | Release blocker |
| Browser tab reloads unexpectedly | Extension result/user report | Restore and remove tab eligibility | Negative cooperative reward |
| BITS job cannot resume | Resume HRESULT | Surface manual recovery instructions | Disable adapter for job class |
| Prefetch increases pressure | Page-read/available-memory regression | Release lease | Negative prefetch reward |
| Optimizer overhead exceeds budget | Self CPU/memory/disk measurements | Reduce sampling or disable module | Release blocker |
| Model uncertainty remains high | Lower-confidence bound | Stay on no-intervention baseline | Collect shadow evidence |
| Cross-device policy regresses | Per-device benchmark comparison | Do not promote global update | Reject federated round |

No failure count may be reset to make a benchmark claim pass.
