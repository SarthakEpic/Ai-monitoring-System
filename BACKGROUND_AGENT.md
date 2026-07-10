# Background Agent

Stage 10 separates the product from the dashboard.

Implemented now:

- Tray icon support when `BACKGROUND_AGENT_ENABLED=1` and `AGENT_TRAY_ICON=1`.
- `--agent` command-line mode to start hidden in the tray.
- Hide-on-minimize support through `AGENT_HIDE_ON_MINIMIZE=1`.
- Silent monitoring state in the runtime model.
- Quick Restore tray command in simulation mode.
- Start-on-boot option through `install_startup.ps1`.

Safety note:

Quick Restore currently records the restore request and exposes it in telemetry. It does not reverse real process changes because Stage 9 still avoids executing process mutations.
