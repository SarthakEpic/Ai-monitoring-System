# Impact Simulation Mode

Stage 11 adds the demo layer this project needs.

The impact simulation engine records:

- Before CPU, RAM, disk free, and risk.
- Estimated after CPU, RAM, disk free, and risk.
- Estimated recovered RAM.
- Estimated CPU relief.
- Number of safe reversible actions prepared.
- Number of user apps touched.
- Foreground process protected during the recommendation.

The goal is to support a startup-style demo:

```text
Before optimization:
RAM 92%
CPU 76%
Disk pressure high

After optimization estimate:
RAM 71%
CPU 38%
Recovered RAM 1.2 GB
Actions prepared: 4
User apps touched: 0
```

These are currently estimates from safe dry-run planning. When real reversible execution is added later, the same table can store measured before/after results.
