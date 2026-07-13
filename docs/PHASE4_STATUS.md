# Phase 4 status

## Implemented release controls

- `python/stress_lab.py` creates a deterministic, versioned safety hard-case corpus.
- `python/certification_runner.py` validates a locked manifest, measured label provenance, a frozen release manifest, and computes one-sided exact binomial bounds.
- `tools/run_locked_certification.ps1` preserves `NOT_CERTIFIED` as a non-zero, non-overridable result.
- python/workload_lab.py provides a bounded, opt-in CPU/memory/storage/process lab; unsupported graphics/network/power paths reject rather than simulate evidence.
- 	ools/freeze_release_candidate.ps1 accepts only a fully signed, hash-verified package and freezes the release policy before certification.
- The exact-certification and Phase 4 corpus tests run in local and CI validation.

## External gates still blocking release

No locked independent campaign, code-signing certificate, signed model bundle,
real installer test campaign, supported Windows/hardware matrix, security scan,
or 72-hour/7-day soak evidence exists in this workspace. The repository must
therefore remain `NOT_CERTIFIED`; `CertifiedAutomatic` must stay disabled.

The stress corpus is intentionally an offline fault-sequence generator, not a
claim that real CPU, memory, storage, graphics, power-transition, AV, or
endpoint-security testing has been completed.

## Reproduction

Run `.\tools\release_readiness.ps1 -Configuration Debug -BuildParallelism 1` to run local validation, generate the SBOM, and emit the deterministic hard-case corpus. This is a readiness check, not a certification command.
