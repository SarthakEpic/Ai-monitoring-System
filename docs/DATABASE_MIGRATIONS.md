# SQLite Migration Policy

## Safety Contract

`MetricsStorage` keeps the legacy monitoring schema intact, then invokes the
transactional Aegis migration runner. Phase 1 migrations are additive: they do
not drop tables, rewrite historical metric rows, or make existing
`monitor.db` files unusable.

Migrations `16` through `19` add the Phase 1 v3 storage contract and runtime performance evidence:

- pseudonymous devices, hardware support descriptors, and monitoring sessions;
- workload episodes and compact telemetry summaries;
- QoE outcomes and label provenance;
- action outcomes, collector-health observations, and raw-plus-robust-normalized telemetry baselines;
- bounded monitor self-observation records for collector/inference p95, process CPU/I/O, wakeups, working set, pending writes, observer state, and storage-batch latency.

Each migration begins within `BEGIN IMMEDIATE`, records its version only after all
schema and index operations succeed, and rolls back if any operation fails.
Reopening a database is therefore idempotent. `DatabaseMigrationsTests` checks
that an older metrics table retains its rows and that version 16 is recorded
only once.

## Compatibility

The previous direct `CREATE TABLE IF NOT EXISTS` statements remain in place for
legacy tables. Their historical entries in `schema_migrations` are legacy
markers, not evidence of transactional migrations. Version 16 is the first
Phase 1 Aegis migration with an explicit transaction and regression test. Version 17 adds support descriptors; version 18 adds robust baseline storage; version 19 adds `runtime_performance_samples` for measured observer-effect evidence.

## Operational Rule

Take a normal file backup before a production upgrade. If migration fails, the
application must report storage as unavailable rather than continuing with a
partially upgraded database.
## Privacy And Lifecycle Controls

The runtime stores a SHA-256 pseudonym rather than a raw computer name or hardware identifier. It does not persist window titles, command lines, executable paths, or telemetry uploads in this v3 path. `EPISODE_RETENTION_DAYS` controls compact-summary retention (default: 14 days). The store also exposes a local CSV export and an atomic deletion path for all v3 records belonging to the current pseudonymous device.
