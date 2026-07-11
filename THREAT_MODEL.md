# Threat Model

## Protected Assets

- User data and unsaved foreground work.
- Foreground responsiveness, audio/video deadlines, and application stability.
- Windows security, accessibility, update, login, shell, and service processes.
- Integrity of policy promotion, action audit, rollback state, and benchmark evidence.
- Private process names, activity sequences, device identity, and workload history.

## Trust Boundaries

- Machine-learning output is untrusted advice.
- Browser extensions and native messaging input are untrusted external input.
- Configuration files are user-controlled but cannot independently bypass all gates.
- PIDs are unstable identifiers; creation time and executable path form the transaction identity.
- SQLite may be unavailable, locked, stale, or corrupted.
- Cooperative integrations may disappear during an action.
- Benchmark JSONL is untrusted until schema and quality validation succeeds.

## Failure and Attack Cases

| Case | Required response |
|---|---|
| PID reused between selection and action | Reject identity mismatch without changing the new process |
| Foreground changes before execution | Re-evaluate and block the target |
| Learned policy is overconfident | Require lower-confidence benefit and persisted offline promotion |
| User toggles only one execution flag | Remain disabled; both switches and all policy gates are required |
| App crashes during an active action | Startup recovery reads unfinished transactions and restores matching identities |
| SQLite pre-action write fails | Do not execute |
| Post-action write fails | Roll back immediately |
| Target exits | Record `TARGET_EXITED`; never modify a reused PID |
| Browser sends malformed/oversized input | Reject framing above 1 MiB and return no action |
| Browser tab becomes active/audible after planning | Extension revalidates immediately before discard |
| BITS job is not current-user enumerated | Reject pause |
| Prefetch confidence/history is insufficient | Reject and allocate no mapping |
| Emergency file `STOP_ACTIONS` exists | Block new online actions |
| Quick Restore requested | Roll back process actions and queue cooperative restores |
| Benchmark contains crashes or touched user apps | Emit `NO_CLAIM` regardless of latency improvement |
| Federated update contains process identity | Do not emit; only clipped/noised deltas and salted category tokens are allowed |

## Deterministic Deny Boundary

System-critical, security, shell, login, accessibility, active/recent user, foreground-family, audio/video deadline, and cross-session/service targets are blocked. Automatic termination, Defender/page-file/Windows Update disabling, registry cleaning, unknown-file deletion, and indiscriminate cache clearing are outside the product design.

## Residual Risks

- Public Windows counters are proxies and do not expose every scheduler or hard-fault causal relationship.
- A process can be important without a visible parent/child dependency.
- Priority and EcoQoS changes can still affect background completion time.
- Browser extension state can be lost if the browser profile is reset.
- Current tests use isolated child processes, not months of diverse real-user workloads.
- Code signing, installer hardening, penetration testing, and independent security review remain external release gates.
