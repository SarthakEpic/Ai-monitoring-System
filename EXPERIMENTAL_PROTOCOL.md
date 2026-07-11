# Experimental Protocol

## Research Questions

1. Does PredictiveAutoHeal reduce foreground p95/p99 latency under background CPU, paging, disk, or network pressure?
2. Which reversible action helps each hardware/workload context, and how accurately does the impact model predict the result?
3. What foreground, stability, background-completion, energy, and optimizer-overhead regressions occur?
4. Are results reproducible across 4 GB HDD, 4 GB SATA SSD, older 8 GB, integrated-graphics, Windows 10, and Windows 11 devices?

## Required Hardware Matrix

- 4 GB RAM with HDD.
- 4 GB RAM with SATA SSD.
- 8 GB RAM with an older dual/quad-core processor.
- Integrated-graphics laptop.
- Supported Windows 10 and Windows 11 builds.

Each physical device gets a stable anonymized `device_id`. Do not combine devices into one distribution before reporting per-device results.

## Scenarios

- `student_browser`: 15-25 tabs, video, PDF, cloud sync, browser/editor switching.
- `coding`: IDE, documentation browser, terminal, background build, antivirus/sync contention.
- `video_meeting`: conferencing app, browser tabs, approved background transfer.
- `gaming`: representative low-end game with background launcher/sync pressure.
- `post_boot`: login through responsive desktop and first foreground application launch.

Scenario scripts, application versions, tab/document set, network source, and background load must be frozen for a benchmark campaign.

## Randomization and Repetition

Generate the run order using `benchmark_lab.py plan`. Run baseline and candidate variants in the generated order. Use at least ten cold and ten warm repetitions per device, scenario, and variant. Record aborted or failed runs; never silently rerun only failures.

Cold-run state must be defined without unsafe cache-clearing utilities. Prefer a controlled reboot and fixed stabilization period. Warm runs follow the documented application preparation sequence. Keep OS image, power mode, charger state, display configuration, network path, and application data constant.

## Measurements

Primary metrics are scenario-specific user-experience signals:

- App/tab switching and launch latency: median, p95, p99.
- Input-response or typing-latency proxy.
- Frame time, 1% low FPS, dropped frames, or audio deadline misses where available.
- System page reads, foreground page-fault progress, and disk queue.
- Background task completion penalty.
- Crashes, app reloads, user applications touched, rollback failures, and user overrides.
- Optimizer CPU, memory, disk-write, and launch overhead measured separately.

Do not substitute free-RAM increase for responsiveness.

## Trace Capture

Use Windows Performance Recorder/Analyzer as an independent trace source where permitted:

```powershell
wpr -start GeneralProfile -filemode
# Run exactly one planned workload interval.
wpr -stop benchmark-traces\RUN_ID.etl
```

Record the WPR profile, Windows build, trace start/end, and any capture failure. For browser responsiveness, record a pinned Speedometer 3.x version separately from the scenario workload. Never merge Speedometer and app-switch latency into one metric.

## Quality Gates

A public improvement claim requires:

- Minimum run count and matched baseline/candidate iterations.
- Randomized order positions.
- Consistent OS build, power mode, and thermal classification.
- Zero crashes, foreground app reloads, user apps touched, and rollback failures.
- Mean optimizer CPU at or below 1% and memory at or below 60 MB for the benchmark campaign.
- Positive lower bound of the bootstrap 95% confidence interval.
- Raw records and negative results retained.

A qualified 2x responsiveness claim additionally requires at least 50% reduction in the predefined p95 latency metric. It applies only to the named device, workload, run type, and metric.

## Competitor Comparison

Compare `windows_baseline`, `predictive_autoheal`, `microsoft_pc_manager`, `process_lasso`, and `razer_cortex` only when legally installed and configured. Freeze each product version and configuration, disable overlapping optimizers, randomize all variants together, and report competitor overhead and safety using the same schema. Do not imply competitor inferiority from missing or non-comparable runs.

## Reproduction Package

Publish the randomized plan, raw anonymized JSONL, environment manifest, scenario instructions, application versions, generated Markdown/JSON report, analysis seed, trace inventory, failure log, and commit hash. Independent reproduction remains a separate status in the project ledger.
