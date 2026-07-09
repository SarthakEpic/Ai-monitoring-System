# User Intent Engine

The User Intent Engine answers the question:

```text
What is the user trying to do right now?
```

This matters because a resource autopilot must protect the foreground task before it optimizes anything in the background.

## What It Captures

Every monitoring tick records:

- foreground process PID
- foreground process name
- executable path
- foreground window title
- app kind
- user state
- idle seconds
- focus duration
- fullscreen state
- recent foreground apps

## App Kinds

The current classifier recognizes:

- `BROWSER`
- `IDE`
- `COMMUNICATION`
- `MEDIA`
- `OFFICE`
- `TERMINAL`
- `SHELL`
- `GAME`
- `UNKNOWN`

## User States

User input is classified as:

- `ACTIVE`: keyboard/mouse activity in the last 30 seconds.
- `IDLE`: no input for 30-300 seconds.
- `AWAY`: no input for more than 300 seconds.

## How It Changes Process Safety

The Process Genome Engine now uses user intent:

- foreground process becomes `FOREGROUND_APP`
- fullscreen foreground process becomes `FOREGROUND_FULLSCREEN_APP`
- recent foreground apps become `RECENT_USER_APP`
- same-name process family for browsers, IDEs, media, games, and communication apps becomes `ACTIVE_APP_FAMILY`

These processes are protected or observe-only. They should not become automatic optimization targets.

## Storage

Intent samples are stored in SQLite:

```text
user_intent_samples
```

They are retained for 7 days, matching process genome samples.

## How To Inspect It

After running the app for 30-60 seconds:

```powershell
python user_intent_summary.py --db build\Debug\monitor.db
```

## Current Safety Position

Phase 2 is still observation-only.

The app does not suspend, kill, or modify processes. It only learns which processes deserve protection because they match the user’s current task.
