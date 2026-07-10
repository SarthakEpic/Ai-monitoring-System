# Low-End PC Autopilot Mode

Stage 9 makes the product identity explicit: PredictiveAutoHeal now has a low-end device brain for 4 GB class PCs.

What it does now:

- Detects weak devices from total RAM, or can be forced with `LOW_END_FORCE=1`.
- Protects the foreground app, recent apps, active app families, protected system processes, and security tools.
- Looks harder at background helpers, browser child processes, updater/sync tools, and memory-heavy hidden processes.
- Prefers reversible actions: delay sync, sleep unused browser helpers, trim background working sets, or lower background priority.
- Estimates recovered RAM and CPU relief before any real action exists.
- Logs recommendations into SQLite for proof, review, and future training.

What it does not do yet:

- It does not kill, suspend, or modify processes automatically.
- It does not delete files or change services.
- It does not touch foreground user work.

This is intentional. The stage builds trustworthy recommendations before real execution.
