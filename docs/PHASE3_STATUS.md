# Phase 3 Implementation Status

## Enforced now

- Requests use a bounded, versioned, local-only named-pipe protocol with expiry and replay rejection.
- Proof, guardian, trusted safety, lease, and canary primitives reject stale, unsafe, neutral, foreground, or protected cases.
- `Aegis99ActuatorService` is a real Windows service host and `install_actuator_service.ps1` registers it as `LocalService`, never `LocalSystem`.
- Native bundle validation rejects altered, oversized, schema-incompatible, unsupported, and unsigned production bundles.
- `NATIVE_BUNDLE` mode blocks the legacy Python/joblib fallback.
- The overview now exposes a hit-tested manual-canary request control. DashboardCommandController requires ManualCanary mode, a certificate, exact target/action/evidence/duration, and a trusted proof reference; it records the exact fail-closed reason when any prerequisite is absent.

## Deliberate fail-closed boundary

The installed service currently rejects every mutation with `trusted_proof_reference_not_connected`: no dashboard/client path can yet supply a service-owned, fully revalidated proof and measured canary transaction. This is intentional until the complete trusted proof ledger, dashboard approval handler, native inference runtime, and integration end-to-end flows exist.

Phase 3 is therefore **IN PROGRESS**. ManualCanary and automatic action remain disabled, and the product remains **NOT_CERTIFIED**.
