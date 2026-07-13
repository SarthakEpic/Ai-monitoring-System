# Aegis99: What To Do Next

## Current status

- Phases 1 through 3 are implemented and covered by local tests.
- Phase 4 tooling is implemented: locked certification scoring, hard-case generation, bounded workload testing, release-freezing tools, and documentation.
- The application is **not certified for automatic actions or public release**. It is currently safe-monitoring / fail-closed software: automatic actions remain disabled when required evidence is missing.
- A free development package can be built now. Public trusted Windows signing and independent real-device certification are later release tasks.

## Do now: continue building and testing

1. Build and run the application locally.

   ```powershell
   cmake --build build --config Debug -- /m:1
   .\build\Debug\PredictiveAutoHeal.exe
   ```

2. Run the complete local checks after making changes.

   ```powershell
   .\tools\run_all_checks.ps1 -Configuration Debug -BuildParallelism 1
   ```

3. Use the dashboard in **Monitor Only** mode. Check that it displays system health, risk explanations, safe recommendations, and blocked reasons correctly.

4. Test only reversible, approved features. Do not try to bypass the safety gates or enable automatic healing without evidence.

5. Keep adding real, correctly labelled test episodes if you want to improve the AI model.

## Do later: when the app features are complete

1. Test on several of your own PCs or virtual machines and collect real workload evidence.
2. Create a locked test-data split and run the Phase 4 certification tool. A result of `NOT_CERTIFIED` means more evidence is needed; do not override it.
3. Review the release-readiness report and freeze the exact release files.
4. Create a development signing certificate only if you need to test installation on another PC. Use it only on devices you control.
5. Before sharing publicly, obtain a publicly trusted code-signing solution, perform independent hardware/soak testing, and complete the certification evidence campaign.

## Free development packaging

Use this only for your own development/testing:

```powershell
.\package_release.ps1 `
  -Configuration Debug `
  -OutputDir build\package-free-dev `
  -DevelopmentDryRun
```

This package is deliberately labelled as a development build. It is not a public production release.

## Important rule

Do not call the app "certified", "fully autonomous", or "safe for public automatic healing" until the Phase 4 evidence and release requirements are genuinely complete.
